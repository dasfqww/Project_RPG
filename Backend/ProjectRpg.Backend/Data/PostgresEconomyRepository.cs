using System.Text.Json;
using System.Text.Json.Serialization;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed class PostgresEconomyRepository(NpgsqlDataSource dataSource)
    : IEconomyRepository
{
    private static readonly JsonSerializerOptions ReceiptJsonOptions =
        CreateJsonOptions();

    public async Task UpsertDefinitionAsync(
        CurrencyDefinition definition,
        DateTimeOffset updatedAt,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);
        try
        {
            string? existingScope;
            await using (NpgsqlCommand lockDefinition = new(
                """
                SELECT scope
                FROM currency_definitions
                WHERE currency_code = @currency_code
                FOR UPDATE;
                """,
                connection,
                transaction))
            {
                lockDefinition.Parameters.AddWithValue(
                    "currency_code",
                    definition.CurrencyCode);
                existingScope = await lockDefinition.ExecuteScalarAsync(
                    cancellationToken) as string;
            }

            if (existingScope is not null
                && !string.Equals(
                    existingScope,
                    definition.Scope.ToString(),
                    StringComparison.Ordinal))
            {
                throw new InvalidOperationException(
                    "A currency definition scope cannot change after creation.");
            }

            long highestStoredBalance;
            await using (NpgsqlCommand readHighestBalance = new(
                """
                SELECT COALESCE(MAX(balance), 0)
                FROM currency_balances
                WHERE currency_code = @currency_code;
                """,
                connection,
                transaction))
            {
                readHighestBalance.Parameters.AddWithValue(
                    "currency_code",
                    definition.CurrencyCode);
                highestStoredBalance = Convert.ToInt64(
                    await readHighestBalance.ExecuteScalarAsync(
                        cancellationToken));
            }

            if (highestStoredBalance > definition.MaxBalance)
            {
                throw new InvalidOperationException(
                    $"MaxBalance cannot be lower than the existing balance " +
                    $"'{highestStoredBalance}'.");
            }

            const string upsertSql = """
                INSERT INTO currency_definitions (
                    currency_code, display_name, scope, max_balance, enabled,
                    created_at, updated_at)
                VALUES (
                    @currency_code, @display_name, @scope, @max_balance, @enabled,
                    @updated_at, @updated_at)
                ON CONFLICT (currency_code) DO UPDATE SET
                    display_name = EXCLUDED.display_name,
                    max_balance = EXCLUDED.max_balance,
                    enabled = EXCLUDED.enabled,
                    updated_at = EXCLUDED.updated_at;
                """;
            await using NpgsqlCommand upsert = new(
                upsertSql,
                connection,
                transaction);
            upsert.Parameters.AddWithValue(
                "currency_code",
                definition.CurrencyCode);
            upsert.Parameters.AddWithValue("display_name", definition.DisplayName);
            upsert.Parameters.AddWithValue("scope", definition.Scope.ToString());
            upsert.Parameters.AddWithValue("max_balance", definition.MaxBalance);
            upsert.Parameters.AddWithValue("enabled", definition.Enabled);
            upsert.Parameters.AddWithValue("updated_at", updatedAt);
            await upsert.ExecuteNonQueryAsync(cancellationToken);
            await transaction.CommitAsync(cancellationToken);
        }
        catch
        {
            await RollbackQuietlyAsync(transaction, cancellationToken);
            throw;
        }
    }

    public async Task<IReadOnlyList<CurrencyDefinition>> GetDefinitionsAsync(
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT currency_code, display_name, scope, max_balance, enabled
            FROM currency_definitions
            ORDER BY currency_code;
            """;
        List<CurrencyDefinition> definitions = [];
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        await using NpgsqlDataReader reader =
            await command.ExecuteReaderAsync(cancellationToken);
        while (await reader.ReadAsync(cancellationToken))
        {
            definitions.Add(ReadDefinition(reader));
        }

        return definitions;
    }

    public async Task<CurrencyWallet> LoadWalletAsync(
        CharacterEconomyContext context,
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT
                definition.currency_code,
                definition.display_name,
                definition.scope,
                definition.max_balance,
                definition.enabled,
                COALESCE(balance.balance, 0),
                COALESCE(balance.revision, 0),
                CASE definition.scope
                    WHEN 'Account' THEN @account_id
                    WHEN 'Roster' THEN @roster_id
                    WHEN 'Character' THEN @character_id
                END AS owner_id
            FROM currency_definitions AS definition
            LEFT JOIN currency_balances AS balance
                ON balance.owner_type = definition.scope
                AND balance.owner_id = CASE definition.scope
                    WHEN 'Account' THEN @account_id
                    WHEN 'Roster' THEN @roster_id
                    WHEN 'Character' THEN @character_id
                END
                AND balance.currency_code = definition.currency_code
            WHERE definition.enabled = TRUE
            ORDER BY definition.currency_code;
            """;
        List<CurrencyBalance> balances = [];
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("account_id", context.SteamId);
        command.Parameters.AddWithValue("roster_id", context.RosterId.ToString("D"));
        command.Parameters.AddWithValue(
            "character_id",
            context.CharacterId.ToString("D"));
        await using NpgsqlDataReader reader =
            await command.ExecuteReaderAsync(cancellationToken);
        while (await reader.ReadAsync(cancellationToken))
        {
            CurrencyDefinition definition = ReadDefinition(reader);
            CurrencyOwnerRef owner = new(
                definition.Scope,
                reader.GetString(7));
            balances.Add(new CurrencyBalance(
                definition,
                owner,
                reader.GetInt64(5),
                reader.GetInt64(6)));
        }

        return new CurrencyWallet(
            context.CharacterId,
            context.RosterId,
            context.SteamId,
            balances);
    }

    public async Task<CurrencyTransactionResult?> TryGetTransactionResultAsync(
        Guid requestId,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        return await ReadReceiptAsync(
            connection,
            null,
            requestId,
            cancellationToken);
    }

    public async Task<CurrencyTransactionResult> CommitAsync(
        CurrencyTransactionRequest request,
        CharacterEconomyContext context,
        DateTimeOffset committedAt,
        CancellationToken cancellationToken)
    {
        IReadOnlyList<CurrencyTransactionResult> results =
            await CommitBatchAsync(
                [new CurrencyBatchEntry(request, context)],
                committedAt,
                cancellationToken);
        return results.Single();
    }

    public async Task<IReadOnlyList<CurrencyTransactionResult>> CommitBatchAsync(
        IReadOnlyList<CurrencyBatchEntry> entries,
        DateTimeOffset committedAt,
        CancellationToken cancellationToken)
    {
        if (entries.Count == 0)
        {
            return [];
        }

        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);
        try
        {
            List<CurrencyTransactionResult> transactionResults = [];
            foreach (CurrencyBatchEntry entry in entries
                .OrderBy(value => value.Request.RequestId))
            {
                CurrencyTransactionRequest request = entry.Request;
                CurrencyTransactionResult? existing = await ReadReceiptAsync(
                    connection,
                    transaction,
                    request.RequestId,
                    cancellationToken);
                if (existing is not null)
                {
                    if (!IsSameCommand(existing, request))
                    {
                        await transaction.RollbackAsync(cancellationToken);
                        return [Failure(
                            request,
                            CurrencyTransactionStatus.IdempotencyConflict,
                            committedAt)];
                    }

                    transactionResults.Add(existing with
                    {
                        Status = CurrencyTransactionStatus.AlreadyCommitted
                    });
                    continue;
                }

                if (!EconomyRules.TryValidateTransaction(request, out _))
                {
                    await transaction.RollbackAsync(cancellationToken);
                    return [Failure(
                        request,
                        CurrencyTransactionStatus.InvalidRequest,
                        committedAt)];
                }

                List<CurrencyChangeResult> changeResults = [];
                foreach (CurrencyChange change in request.Changes
                    .OrderBy(value => value.CurrencyCode, StringComparer.Ordinal))
                {
                    CurrencyDefinition? definition = await ReadDefinitionAsync(
                        connection,
                        transaction,
                        change.CurrencyCode,
                        cancellationToken);
                    if (definition is null)
                    {
                        await transaction.RollbackAsync(cancellationToken);
                        return [Failure(
                            request,
                            CurrencyTransactionStatus.DefinitionNotFound,
                            committedAt)];
                    }

                    if (!definition.Enabled)
                    {
                        await transaction.RollbackAsync(cancellationToken);
                        return [Failure(
                            request,
                            CurrencyTransactionStatus.CurrencyDisabled,
                            committedAt)];
                    }

                    CurrencyOwnerRef owner = new(
                        definition.Scope,
                        EconomyRules.ResolveOwnerId(
                            definition.Scope,
                            entry.Context));
                    await EnsureBalanceRowAsync(
                        connection,
                        transaction,
                        owner,
                        definition.CurrencyCode,
                        committedAt,
                        cancellationToken);
                    (long previous, long previousRevision) =
                        await LockBalanceAsync(
                            connection,
                            transaction,
                            owner,
                            definition.CurrencyCode,
                            cancellationToken);
                    long next;
                    try
                    {
                        next = checked(previous + change.Delta);
                    }
                    catch (OverflowException)
                    {
                        await transaction.RollbackAsync(cancellationToken);
                        return [Failure(
                            request,
                            CurrencyTransactionStatus.BalanceLimitExceeded,
                            committedAt)];
                    }

                    if (next < 0)
                    {
                        await transaction.RollbackAsync(cancellationToken);
                        return [Failure(
                            request,
                            CurrencyTransactionStatus.InsufficientBalance,
                            committedAt)];
                    }

                    if (next > definition.MaxBalance && next >= previous)
                    {
                        await transaction.RollbackAsync(cancellationToken);
                        return [Failure(
                            request,
                            CurrencyTransactionStatus.BalanceLimitExceeded,
                            committedAt)];
                    }

                    long revision = previousRevision + 1;
                    await WriteBalanceAsync(
                        connection,
                        transaction,
                        owner,
                        definition.CurrencyCode,
                        next,
                        revision,
                        committedAt,
                        cancellationToken);
                    changeResults.Add(new CurrencyChangeResult(
                        definition.CurrencyCode,
                        definition.Scope,
                        owner.OwnerId,
                        change.Delta,
                        previous,
                        next,
                        revision));
                }

                CurrencyTransactionResult result = new(
                    CurrencyTransactionStatus.Committed,
                    request.RequestId,
                    request.CharacterId,
                    request.Operation,
                    request.CommandFingerprint,
                    request.Reason,
                    changeResults,
                    committedAt);
                await InsertReceiptAsync(
                    connection,
                    transaction,
                    result,
                    cancellationToken);
                transactionResults.Add(result);
            }

            await transaction.CommitAsync(cancellationToken);
            return transactionResults;
        }
        catch (PostgresException exception)
            when (exception.SqlState == PostgresErrorCodes.UniqueViolation)
        {
            await RollbackQuietlyAsync(transaction, cancellationToken);
            List<CurrencyTransactionResult> replayed = [];
            foreach (CurrencyBatchEntry entry in entries
                .OrderBy(value => value.Request.RequestId))
            {
                CurrencyTransactionResult? existing =
                    await TryGetTransactionResultAsync(
                        entry.Request.RequestId,
                        cancellationToken);
                if (existing is null
                    || !IsSameCommand(existing, entry.Request))
                {
                    throw;
                }

                replayed.Add(existing with
                {
                    Status = CurrencyTransactionStatus.AlreadyCommitted
                });
            }

            return replayed;
        }
        catch
        {
            await RollbackQuietlyAsync(transaction, cancellationToken);
            throw;
        }
    }

    internal async Task<IReadOnlyList<CurrencyTransactionResult>>
        CommitBatchInTransactionAsync(
            NpgsqlConnection connection,
            NpgsqlTransaction transaction,
            IReadOnlyList<CurrencyBatchEntry> entries,
            DateTimeOffset committedAt,
            CancellationToken cancellationToken)
    {
        List<CurrencyTransactionResult> transactionResults = [];
        foreach (CurrencyBatchEntry entry in entries
            .OrderBy(value => value.Request.RequestId))
        {
            CurrencyTransactionRequest request = entry.Request;
            CurrencyTransactionResult? existing = await ReadReceiptAsync(
                connection,
                transaction,
                request.RequestId,
                cancellationToken);
            if (existing is not null)
            {
                if (!IsSameCommand(existing, request))
                {
                    return [Failure(
                        request,
                        CurrencyTransactionStatus.IdempotencyConflict,
                        committedAt)];
                }

                transactionResults.Add(existing with
                {
                    Status = CurrencyTransactionStatus.AlreadyCommitted
                });
                continue;
            }

            if (!EconomyRules.TryValidateTransaction(request, out _))
            {
                return [Failure(
                    request,
                    CurrencyTransactionStatus.InvalidRequest,
                    committedAt)];
            }

            List<CurrencyChangeResult> changeResults = [];
            foreach (CurrencyChange change in request.Changes
                .OrderBy(value => value.CurrencyCode, StringComparer.Ordinal))
            {
                CurrencyDefinition? definition = await ReadDefinitionAsync(
                    connection,
                    transaction,
                    change.CurrencyCode,
                    cancellationToken);
                if (definition is null)
                {
                    return [Failure(
                        request,
                        CurrencyTransactionStatus.DefinitionNotFound,
                        committedAt)];
                }

                if (!definition.Enabled)
                {
                    return [Failure(
                        request,
                        CurrencyTransactionStatus.CurrencyDisabled,
                        committedAt)];
                }

                CurrencyOwnerRef owner = new(
                    definition.Scope,
                    EconomyRules.ResolveOwnerId(definition.Scope, entry.Context));
                await EnsureBalanceRowAsync(
                    connection,
                    transaction,
                    owner,
                    definition.CurrencyCode,
                    committedAt,
                    cancellationToken);
                (long previous, long previousRevision) = await LockBalanceAsync(
                    connection,
                    transaction,
                    owner,
                    definition.CurrencyCode,
                    cancellationToken);
                long next;
                try
                {
                    next = checked(previous + change.Delta);
                }
                catch (OverflowException)
                {
                    return [Failure(
                        request,
                        CurrencyTransactionStatus.BalanceLimitExceeded,
                        committedAt)];
                }

                if (next < 0)
                {
                    return [Failure(
                        request,
                        CurrencyTransactionStatus.InsufficientBalance,
                        committedAt)];
                }

                if (next > definition.MaxBalance && next >= previous)
                {
                    return [Failure(
                        request,
                        CurrencyTransactionStatus.BalanceLimitExceeded,
                        committedAt)];
                }

                long revision = previousRevision + 1;
                await WriteBalanceAsync(
                    connection,
                    transaction,
                    owner,
                    definition.CurrencyCode,
                    next,
                    revision,
                    committedAt,
                    cancellationToken);
                changeResults.Add(new CurrencyChangeResult(
                    definition.CurrencyCode,
                    definition.Scope,
                    owner.OwnerId,
                    change.Delta,
                    previous,
                    next,
                    revision));
            }

            CurrencyTransactionResult result = new(
                CurrencyTransactionStatus.Committed,
                request.RequestId,
                request.CharacterId,
                request.Operation,
                request.CommandFingerprint,
                request.Reason,
                changeResults,
                committedAt);
            await InsertReceiptAsync(
                connection,
                transaction,
                result,
                cancellationToken);
            transactionResults.Add(result);
        }

        return transactionResults;
    }

    private static bool IsSameCommand(
        CurrencyTransactionResult result,
        CurrencyTransactionRequest request)
    {
        if (result.CharacterId != request.CharacterId
            || !string.Equals(result.Operation, request.Operation, StringComparison.Ordinal)
            || !string.Equals(
                result.CommandFingerprint,
                request.CommandFingerprint,
                StringComparison.Ordinal)
            || !string.Equals(result.Reason, request.Reason, StringComparison.Ordinal)
            || result.Changes.Count != request.Changes.Count)
        {
            return false;
        }

        Dictionary<string, long> requestedChanges = request.Changes
            .ToDictionary(
                change => change.CurrencyCode,
                change => change.Delta,
                StringComparer.Ordinal);
        return result.Changes.All(change =>
            requestedChanges.TryGetValue(change.CurrencyCode, out long delta)
            && delta == change.Delta);
    }

    private static async Task<CurrencyDefinition?> ReadDefinitionAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        string currencyCode,
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT currency_code, display_name, scope, max_balance, enabled
            FROM currency_definitions
            WHERE currency_code = @currency_code
            FOR SHARE;
            """;
        await using NpgsqlCommand command = new(sql, connection, transaction);
        command.Parameters.AddWithValue("currency_code", currencyCode);
        await using NpgsqlDataReader reader =
            await command.ExecuteReaderAsync(cancellationToken);
        return await reader.ReadAsync(cancellationToken)
            ? ReadDefinition(reader)
            : null;
    }

    private static async Task EnsureBalanceRowAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        CurrencyOwnerRef owner,
        string currencyCode,
        DateTimeOffset updatedAt,
        CancellationToken cancellationToken)
    {
        const string sql = """
            INSERT INTO currency_balances (
                owner_type, owner_id, currency_code, balance, revision, updated_at)
            VALUES (
                @owner_type, @owner_id, @currency_code, 0, 0, @updated_at)
            ON CONFLICT (owner_type, owner_id, currency_code) DO NOTHING;
            """;
        await using NpgsqlCommand command = new(sql, connection, transaction);
        command.Parameters.AddWithValue("owner_type", owner.Scope.ToString());
        command.Parameters.AddWithValue("owner_id", owner.OwnerId);
        command.Parameters.AddWithValue("currency_code", currencyCode);
        command.Parameters.AddWithValue("updated_at", updatedAt);
        await command.ExecuteNonQueryAsync(cancellationToken);
    }

    private static async Task<(long Balance, long Revision)> LockBalanceAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        CurrencyOwnerRef owner,
        string currencyCode,
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT balance, revision
            FROM currency_balances
            WHERE owner_type = @owner_type
              AND owner_id = @owner_id
              AND currency_code = @currency_code
            FOR UPDATE;
            """;
        await using NpgsqlCommand command = new(sql, connection, transaction);
        command.Parameters.AddWithValue("owner_type", owner.Scope.ToString());
        command.Parameters.AddWithValue("owner_id", owner.OwnerId);
        command.Parameters.AddWithValue("currency_code", currencyCode);
        await using NpgsqlDataReader reader =
            await command.ExecuteReaderAsync(cancellationToken);
        if (!await reader.ReadAsync(cancellationToken))
        {
            throw new InvalidDataException(
                "The currency balance row could not be locked.");
        }

        return (reader.GetInt64(0), reader.GetInt64(1));
    }

    private static async Task WriteBalanceAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        CurrencyOwnerRef owner,
        string currencyCode,
        long balance,
        long revision,
        DateTimeOffset updatedAt,
        CancellationToken cancellationToken)
    {
        const string sql = """
            UPDATE currency_balances
            SET balance = @balance,
                revision = @revision,
                updated_at = @updated_at
            WHERE owner_type = @owner_type
              AND owner_id = @owner_id
              AND currency_code = @currency_code;
            """;
        await using NpgsqlCommand command = new(sql, connection, transaction);
        command.Parameters.AddWithValue("balance", balance);
        command.Parameters.AddWithValue("revision", revision);
        command.Parameters.AddWithValue("updated_at", updatedAt);
        command.Parameters.AddWithValue("owner_type", owner.Scope.ToString());
        command.Parameters.AddWithValue("owner_id", owner.OwnerId);
        command.Parameters.AddWithValue("currency_code", currencyCode);
        int affected = await command.ExecuteNonQueryAsync(cancellationToken);
        if (affected != 1)
        {
            throw new InvalidOperationException(
                "The locked currency balance was not updated.");
        }
    }

    private static async Task InsertReceiptAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        CurrencyTransactionResult result,
        CancellationToken cancellationToken)
    {
        const string sql = """
            INSERT INTO currency_transaction_receipts (
                request_id, character_id, operation, command_fingerprint,
                reason, result_changes, committed_at)
            VALUES (
                @request_id, @character_id, @operation, @command_fingerprint,
                @reason, @result_changes, @committed_at);
            """;
        await using NpgsqlCommand command = new(sql, connection, transaction);
        command.Parameters.AddWithValue("request_id", result.RequestId);
        command.Parameters.AddWithValue("character_id", result.CharacterId);
        command.Parameters.AddWithValue("operation", result.Operation);
        command.Parameters.AddWithValue(
            "command_fingerprint",
            result.CommandFingerprint);
        command.Parameters.AddWithValue("reason", result.Reason);
        command.Parameters.AddWithValue(
            "result_changes",
            NpgsqlDbType.Jsonb,
            JsonSerializer.Serialize(result.Changes, ReceiptJsonOptions));
        command.Parameters.AddWithValue("committed_at", result.CommittedAt);
        await command.ExecuteNonQueryAsync(cancellationToken);
    }

    private static async Task<CurrencyTransactionResult?> ReadReceiptAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction? transaction,
        Guid requestId,
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT
                request_id, character_id, operation, command_fingerprint,
                reason, result_changes::TEXT, committed_at
            FROM currency_transaction_receipts
            WHERE request_id = @request_id;
            """;
        await using NpgsqlCommand command = transaction is null
            ? new NpgsqlCommand(sql, connection)
            : new NpgsqlCommand(sql, connection, transaction);
        command.Parameters.AddWithValue("request_id", requestId);
        await using NpgsqlDataReader reader =
            await command.ExecuteReaderAsync(cancellationToken);
        if (!await reader.ReadAsync(cancellationToken))
        {
            return null;
        }

        CurrencyChangeResult[] changes =
            JsonSerializer.Deserialize<CurrencyChangeResult[]>(
                reader.GetString(5),
                ReceiptJsonOptions)
            ?? throw new InvalidDataException(
                "A currency transaction receipt contains invalid changes.");
        return new CurrencyTransactionResult(
            CurrencyTransactionStatus.Committed,
            reader.GetGuid(0),
            reader.GetGuid(1),
            reader.GetString(2),
            reader.GetString(3),
            reader.GetString(4),
            changes,
            reader.GetFieldValue<DateTimeOffset>(6));
    }

    private static CurrencyDefinition ReadDefinition(NpgsqlDataReader reader)
    {
        string scopeValue = reader.GetString(2);
        if (!Enum.TryParse(scopeValue, out CurrencyScope scope))
        {
            throw new InvalidDataException(
                $"Stored currency scope '{scopeValue}' is invalid.");
        }

        return new CurrencyDefinition(
            reader.GetString(0),
            reader.GetString(1),
            scope,
            reader.GetInt64(3),
            reader.GetBoolean(4));
    }

    private static CurrencyTransactionResult Failure(
        CurrencyTransactionRequest request,
        CurrencyTransactionStatus status,
        DateTimeOffset committedAt)
    {
        return new CurrencyTransactionResult(
            status,
            request.RequestId,
            request.CharacterId,
            request.Operation,
            request.CommandFingerprint,
            request.Reason,
            [],
            committedAt);
    }

    private static async Task RollbackQuietlyAsync(
        NpgsqlTransaction transaction,
        CancellationToken cancellationToken)
    {
        try
        {
            await transaction.RollbackAsync(cancellationToken);
        }
        catch (Exception)
        {
            // Preserve the original database exception.
        }
    }

    private static JsonSerializerOptions CreateJsonOptions()
    {
        JsonSerializerOptions options = new(JsonSerializerDefaults.Web);
        options.Converters.Add(new JsonStringEnumConverter());
        return options;
    }
}
