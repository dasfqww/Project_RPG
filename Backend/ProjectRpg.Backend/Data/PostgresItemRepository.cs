using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed class PostgresItemRepository(
    NpgsqlDataSource dataSource,
    ILogger<PostgresItemRepository> logger) : IItemRepository
{
    private const string ItemColumns = """
        item_id, definition_type, definition_name, definition_version,
        owner_type, owner_id, container_type, container_id, slot_index,
        generation_seed, quantity, instance_tags, stat_values, revision,
        lifecycle_state, bind_state, durability_current, durability_maximum,
        expires_at, creation_source, is_locked
        """;

    private static readonly JsonSerializerOptions ReceiptJsonOptions = CreateJsonOptions();

    public async Task<ItemRecord?> FindAsync(
        Guid itemId,
        CancellationToken cancellationToken)
    {
        string sql = $"""
            SELECT {ItemColumns}
            FROM item_records
            WHERE item_id = @item_id;
            """;
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue("item_id", itemId);
        await using NpgsqlDataReader reader =
            await command.ExecuteReaderAsync(cancellationToken);
        return await reader.ReadAsync(cancellationToken)
            ? ReadItemRecord(reader)
            : null;
    }

    public async Task<IReadOnlyList<ItemRecord>> FindByOwnerAsync(
        ItemOwnerRef owner,
        bool includeTerminal,
        int limit,
        CancellationToken cancellationToken)
    {
        string sql = $"""
            SELECT {ItemColumns}
            FROM item_records
            WHERE owner_type = @owner_type
              AND owner_id = @owner_id
              AND (@include_terminal OR lifecycle_state = 'Active')
            ORDER BY container_type, container_id, slot_index, item_id
            LIMIT @limit;
            """;
        List<ItemRecord> records = [];
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlCommand command = new(sql, connection);
        command.Parameters.AddWithValue(
            "owner_type",
            owner.Type.ToString());
        command.Parameters.AddWithValue("owner_id", owner.OwnerId);
        command.Parameters.AddWithValue(
            "include_terminal",
            includeTerminal);
        command.Parameters.AddWithValue("limit", limit);
        await using NpgsqlDataReader reader =
            await command.ExecuteReaderAsync(cancellationToken);
        while (await reader.ReadAsync(cancellationToken))
        {
            records.Add(ReadItemRecord(reader));
        }

        return records;
    }

    public async Task<ItemRepositoryCommitResult?> TryGetCommitResultAsync(
        Guid requestId,
        CancellationToken cancellationToken)
    {
        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        return await ReadReceiptAsync(
            connection,
            transaction: null,
            requestId,
            cancellationToken);
    }

    public async Task<ItemRepositoryCommitResult> CommitAsync(
        ItemRepositoryCommitRequest request,
        DateTimeOffset committedAt,
        CancellationToken cancellationToken)
    {
        if (!ItemRecordRules.TryValidateCommit(request, out _))
        {
            return Failure(
                request,
                ItemRepositoryCommitStatus.InvalidRequest,
                committedAt);
        }

        await using NpgsqlConnection connection =
            await dataSource.OpenConnectionAsync(cancellationToken);
        await using NpgsqlTransaction transaction =
            await connection.BeginTransactionAsync(cancellationToken);
        try
        {
            await AcquireRequestLockAsync(
                connection,
                transaction,
                request.RequestId,
                cancellationToken);

            ItemRepositoryCommitResult? receipt = await ReadReceiptAsync(
                connection,
                transaction,
                request.RequestId,
                cancellationToken);
            if (receipt is not null)
            {
                await transaction.CommitAsync(cancellationToken);
                return receipt with
                {
                    Status = ItemRepositoryCommitStatus.AlreadyCommitted
                };
            }

            Dictionary<Guid, ItemRecord?> storedRecords = [];
            foreach (ItemRecordMutation mutation in request.Mutations
                .OrderBy(value => value.NewRecord.ItemId))
            {
                ItemRecord? stored = await ReadItemForUpdateAsync(
                    connection,
                    transaction,
                    mutation.NewRecord.ItemId,
                    cancellationToken);
                storedRecords.Add(mutation.NewRecord.ItemId, stored);

                if (mutation.ExpectedRevision == 0 && stored is not null)
                {
                    await transaction.RollbackAsync(cancellationToken);
                    return Failure(
                        request,
                        ItemRepositoryCommitStatus.RevisionConflict,
                        committedAt);
                }

                if (mutation.ExpectedRevision > 0 && stored is null)
                {
                    await transaction.RollbackAsync(cancellationToken);
                    return Failure(
                        request,
                        ItemRepositoryCommitStatus.NotFound,
                        committedAt);
                }

                if (stored is not null
                    && stored.Revision != mutation.ExpectedRevision)
                {
                    await transaction.RollbackAsync(cancellationToken);
                    return Failure(
                        request,
                        ItemRepositoryCommitStatus.RevisionConflict,
                        committedAt);
                }
            }

            ItemRecord[] writtenRecords = request.Mutations
                .Select(mutation => ItemRecordRules.SnapshotWithRevision(
                    mutation.NewRecord,
                    mutation.ExpectedRevision + 1))
                .ToArray();
            if (writtenRecords.Any(record =>
                !ItemRecordRules.TryValidateRecord(record, out _)))
            {
                await transaction.RollbackAsync(cancellationToken);
                return Failure(
                    request,
                    ItemRepositoryCommitStatus.ValidationFailed,
                    committedAt);
            }

            // Release every old active location inside this transaction first.
            // This allows an atomic two-item slot swap without exposing staging
            // state to another transaction.
            foreach ((Guid itemId, ItemRecord? stored) in storedRecords)
            {
                if (stored is not null)
                {
                    await StageExistingRecordAsync(
                        connection,
                        transaction,
                        itemId,
                        cancellationToken);
                }
            }

            foreach (ItemRecordMutation mutation in request.Mutations)
            {
                ItemRecord record = writtenRecords.Single(
                    value => value.ItemId == mutation.NewRecord.ItemId);
                await WriteRecordAsync(
                    connection,
                    transaction,
                    record,
                    isInsert: mutation.ExpectedRevision == 0,
                    committedAt,
                    cancellationToken);
            }

            ItemRepositoryCommitResult result = new(
                ItemRepositoryCommitStatus.Committed,
                request.RequestId,
                request.Operation,
                request.CommandFingerprint,
                request.Actor,
                request.AffectedQuantity,
                writtenRecords,
                committedAt);
            await InsertReceiptAsync(
                connection,
                transaction,
                result,
                cancellationToken);
            await transaction.CommitAsync(cancellationToken);
            return result;
        }
        catch (PostgresException exception)
            when (exception.SqlState == PostgresErrorCodes.UniqueViolation)
        {
            await RollbackQuietlyAsync(transaction, cancellationToken);
            ItemRepositoryCommitStatus status =
                string.Equals(
                    exception.ConstraintName,
                    "ux_item_records_active_location",
                    StringComparison.Ordinal)
                    ? ItemRepositoryCommitStatus.LocationConflict
                    : ItemRepositoryCommitStatus.RevisionConflict;
            return Failure(request, status, committedAt);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception exception)
        {
            await RollbackQuietlyAsync(transaction, cancellationToken);
            logger.LogError(
                exception,
                "Item commit {RequestId} failed.",
                request.RequestId);
            return Failure(
                request,
                ItemRepositoryCommitStatus.InternalError,
                committedAt);
        }
    }

    internal async Task<ItemRepositoryCommitResult> CommitInTransactionAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        ItemRepositoryCommitRequest request,
        DateTimeOffset committedAt,
        CancellationToken cancellationToken)
    {
        if (!ItemRecordRules.TryValidateCommit(request, out _))
        {
            return Failure(
                request,
                ItemRepositoryCommitStatus.InvalidRequest,
                committedAt);
        }

        await AcquireRequestLockAsync(
            connection,
            transaction,
            request.RequestId,
            cancellationToken);
        ItemRepositoryCommitResult? receipt = await ReadReceiptAsync(
            connection,
            transaction,
            request.RequestId,
            cancellationToken);
        if (receipt is not null)
        {
            return IsSameCommand(receipt, request)
                ? receipt with
                {
                    Status = ItemRepositoryCommitStatus.AlreadyCommitted
                }
                : Failure(
                    request,
                    ItemRepositoryCommitStatus.IdempotencyConflict,
                    committedAt);
        }

        Dictionary<Guid, ItemRecord?> storedRecords = [];
        foreach (ItemRecordMutation mutation in request.Mutations
            .OrderBy(value => value.NewRecord.ItemId))
        {
            ItemRecord? stored = await ReadItemForUpdateAsync(
                connection,
                transaction,
                mutation.NewRecord.ItemId,
                cancellationToken);
            storedRecords.Add(mutation.NewRecord.ItemId, stored);
            if (mutation.ExpectedRevision == 0 && stored is not null)
            {
                return Failure(
                    request,
                    ItemRepositoryCommitStatus.RevisionConflict,
                    committedAt);
            }

            if (mutation.ExpectedRevision > 0 && stored is null)
            {
                return Failure(
                    request,
                    ItemRepositoryCommitStatus.NotFound,
                    committedAt);
            }

            if (stored is not null
                && stored.Revision != mutation.ExpectedRevision)
            {
                return Failure(
                    request,
                    ItemRepositoryCommitStatus.RevisionConflict,
                    committedAt);
            }
        }

        ItemRecord[] writtenRecords = request.Mutations
            .Select(mutation => ItemRecordRules.SnapshotWithRevision(
                mutation.NewRecord,
                mutation.ExpectedRevision + 1))
            .ToArray();
        if (writtenRecords.Any(record =>
            !ItemRecordRules.TryValidateRecord(record, out _)))
        {
            return Failure(
                request,
                ItemRepositoryCommitStatus.ValidationFailed,
                committedAt);
        }

        foreach ((Guid itemId, ItemRecord? stored) in storedRecords)
        {
            if (stored is not null)
            {
                await StageExistingRecordAsync(
                    connection,
                    transaction,
                    itemId,
                    cancellationToken);
            }
        }

        foreach (ItemRecordMutation mutation in request.Mutations)
        {
            ItemRecord record = writtenRecords.Single(
                value => value.ItemId == mutation.NewRecord.ItemId);
            await WriteRecordAsync(
                connection,
                transaction,
                record,
                isInsert: mutation.ExpectedRevision == 0,
                committedAt,
                cancellationToken);
        }

        ItemRepositoryCommitResult result = new(
            ItemRepositoryCommitStatus.Committed,
            request.RequestId,
            request.Operation,
            request.CommandFingerprint,
            request.Actor,
            request.AffectedQuantity,
            writtenRecords,
            committedAt);
        await InsertReceiptAsync(
            connection,
            transaction,
            result,
            cancellationToken);
        return result;
    }

    private static bool IsSameCommand(
        ItemRepositoryCommitResult result,
        ItemRepositoryCommitRequest request)
    {
        return string.Equals(
                result.Operation,
                request.Operation,
                StringComparison.Ordinal)
            && string.Equals(
                result.CommandFingerprint,
                request.CommandFingerprint,
                StringComparison.Ordinal)
            && result.Actor == request.Actor;
    }

    private static async Task AcquireRequestLockAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        Guid requestId,
        CancellationToken cancellationToken)
    {
        byte[] hash = SHA256.HashData(requestId.ToByteArray());
        long lockKey = BinaryPrimitives.ReadInt64LittleEndian(hash);
        await using NpgsqlCommand command = new(
            "SELECT pg_advisory_xact_lock(@lock_key);",
            connection,
            transaction);
        command.Parameters.AddWithValue("lock_key", lockKey);
        await command.ExecuteNonQueryAsync(cancellationToken);
    }

    private static async Task<ItemRecord?> ReadItemForUpdateAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        Guid itemId,
        CancellationToken cancellationToken)
    {
        string sql = $"""
            SELECT {ItemColumns}
            FROM item_records
            WHERE item_id = @item_id
            FOR UPDATE;
            """;
        await using NpgsqlCommand command = new(
            sql,
            connection,
            transaction);
        command.Parameters.AddWithValue("item_id", itemId);
        await using NpgsqlDataReader reader =
            await command.ExecuteReaderAsync(cancellationToken);
        return await reader.ReadAsync(cancellationToken)
            ? ReadItemRecord(reader)
            : null;
    }

    private static async Task StageExistingRecordAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        Guid itemId,
        CancellationToken cancellationToken)
    {
        const string sql = """
            UPDATE item_records
            SET lifecycle_state = 'Destroyed',
                container_type = 'Terminal',
                container_id = '',
                slot_index = -1,
                quantity = 0,
                is_locked = TRUE
            WHERE item_id = @item_id;
            """;
        await using NpgsqlCommand command = new(
            sql,
            connection,
            transaction);
        command.Parameters.AddWithValue("item_id", itemId);
        await command.ExecuteNonQueryAsync(cancellationToken);
    }

    private static async Task WriteRecordAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        ItemRecord record,
        bool isInsert,
        DateTimeOffset updatedAt,
        CancellationToken cancellationToken)
    {
        const string insertSql = """
            INSERT INTO item_records (
                item_id, definition_type, definition_name, definition_version,
                owner_type, owner_id, container_type, container_id, slot_index,
                generation_seed, quantity, instance_tags, stat_values, revision,
                lifecycle_state, bind_state, durability_current,
                durability_maximum, expires_at, creation_source, is_locked,
                created_at, updated_at)
            VALUES (
                @item_id, @definition_type, @definition_name, @definition_version,
                @owner_type, @owner_id, @container_type, @container_id, @slot_index,
                @generation_seed, @quantity, @instance_tags, @stat_values, @revision,
                @lifecycle_state, @bind_state, @durability_current,
                @durability_maximum, @expires_at, @creation_source, @is_locked,
                @updated_at, @updated_at);
            """;
        const string updateSql = """
            UPDATE item_records
            SET definition_type = @definition_type,
                definition_name = @definition_name,
                definition_version = @definition_version,
                owner_type = @owner_type,
                owner_id = @owner_id,
                container_type = @container_type,
                container_id = @container_id,
                slot_index = @slot_index,
                generation_seed = @generation_seed,
                quantity = @quantity,
                instance_tags = @instance_tags,
                stat_values = @stat_values,
                revision = @revision,
                lifecycle_state = @lifecycle_state,
                bind_state = @bind_state,
                durability_current = @durability_current,
                durability_maximum = @durability_maximum,
                expires_at = @expires_at,
                creation_source = @creation_source,
                is_locked = @is_locked,
                updated_at = @updated_at
            WHERE item_id = @item_id;
            """;
        await using NpgsqlCommand command = new(
            isInsert ? insertSql : updateSql,
            connection,
            transaction);
        AddRecordParameters(command, record, updatedAt);
        int affected = await command.ExecuteNonQueryAsync(cancellationToken);
        if (affected != 1)
        {
            throw new InvalidOperationException(
                $"Expected to write one item row, wrote {affected}.");
        }
    }

    private static void AddRecordParameters(
        NpgsqlCommand command,
        ItemRecord record,
        DateTimeOffset updatedAt)
    {
        command.Parameters.AddWithValue("item_id", record.ItemId);
        command.Parameters.AddWithValue(
            "definition_type",
            record.DefinitionType);
        command.Parameters.AddWithValue(
            "definition_name",
            record.DefinitionName);
        command.Parameters.AddWithValue(
            "definition_version",
            record.DefinitionVersion);
        command.Parameters.AddWithValue(
            "owner_type",
            record.Owner.Type.ToString());
        command.Parameters.AddWithValue("owner_id", record.Owner.OwnerId);
        command.Parameters.AddWithValue(
            "container_type",
            record.Location.ContainerType.ToString());
        command.Parameters.AddWithValue(
            "container_id",
            record.Location.ContainerId);
        command.Parameters.AddWithValue(
            "slot_index",
            record.Location.SlotIndex);
        command.Parameters.AddWithValue(
            "generation_seed",
            record.State.GenerationSeed);
        command.Parameters.AddWithValue(
            "quantity",
            record.State.Quantity);
        command.Parameters.Add(
            "instance_tags",
            NpgsqlDbType.Jsonb).Value = JsonSerializer.Serialize(
                record.State.InstanceTags,
                ReceiptJsonOptions);
        command.Parameters.Add(
            "stat_values",
            NpgsqlDbType.Jsonb).Value = JsonSerializer.Serialize(
                record.State.StatValues,
                ReceiptJsonOptions);
        command.Parameters.AddWithValue("revision", record.Revision);
        command.Parameters.AddWithValue(
            "lifecycle_state",
            record.LifecycleState.ToString());
        command.Parameters.AddWithValue(
            "bind_state",
            record.Metadata.BindState.ToString());
        command.Parameters.AddWithValue(
            "durability_current",
            record.Metadata.Durability.Current);
        command.Parameters.AddWithValue(
            "durability_maximum",
            record.Metadata.Durability.Maximum);
        command.Parameters.Add(
            "expires_at",
            NpgsqlDbType.TimestampTz).Value =
            record.Metadata.ExpiresAtUtc is DateTimeOffset expiresAt
                ? expiresAt.ToUniversalTime()
                : DBNull.Value;
        command.Parameters.AddWithValue(
            "creation_source",
            record.Metadata.CreationSource);
        command.Parameters.AddWithValue(
            "is_locked",
            record.Metadata.IsLocked);
        command.Parameters.AddWithValue(
            "updated_at",
            updatedAt.ToUniversalTime());
    }

    private static async Task InsertReceiptAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction transaction,
        ItemRepositoryCommitResult result,
        CancellationToken cancellationToken)
    {
        const string sql = """
            INSERT INTO item_transaction_receipts (
                request_id, operation, command_fingerprint,
                actor_type, actor_id, affected_quantity,
                result_records, committed_at)
            VALUES (
                @request_id, @operation, @command_fingerprint,
                @actor_type, @actor_id, @affected_quantity,
                @result_records, @committed_at);
            """;
        await using NpgsqlCommand command = new(
            sql,
            connection,
            transaction);
        command.Parameters.AddWithValue("request_id", result.RequestId);
        command.Parameters.AddWithValue("operation", result.Operation);
        command.Parameters.AddWithValue(
            "command_fingerprint",
            result.CommandFingerprint);
        command.Parameters.AddWithValue(
            "actor_type",
            result.Actor.Type.ToString());
        command.Parameters.AddWithValue(
            "actor_id",
            result.Actor.OwnerId);
        command.Parameters.AddWithValue(
            "affected_quantity",
            result.AffectedQuantity);
        command.Parameters.Add(
            "result_records",
            NpgsqlDbType.Jsonb).Value = JsonSerializer.Serialize(
                result.Records,
                ReceiptJsonOptions);
        command.Parameters.AddWithValue(
            "committed_at",
            result.CommittedAt.ToUniversalTime());
        await command.ExecuteNonQueryAsync(cancellationToken);
    }

    private static async Task<ItemRepositoryCommitResult?> ReadReceiptAsync(
        NpgsqlConnection connection,
        NpgsqlTransaction? transaction,
        Guid requestId,
        CancellationToken cancellationToken)
    {
        const string sql = """
            SELECT operation, command_fingerprint, actor_type, actor_id,
                   affected_quantity, result_records, committed_at
            FROM item_transaction_receipts
            WHERE request_id = @request_id;
            """;
        await using NpgsqlCommand command = new(
            sql,
            connection,
            transaction);
        command.Parameters.AddWithValue("request_id", requestId);
        await using NpgsqlDataReader reader =
            await command.ExecuteReaderAsync(cancellationToken);
        if (!await reader.ReadAsync(cancellationToken))
        {
            return null;
        }

        ItemRecord[] records =
            JsonSerializer.Deserialize<ItemRecord[]>(
                reader.GetString(5),
                ReceiptJsonOptions)
            ?? [];
        return new ItemRepositoryCommitResult(
            ItemRepositoryCommitStatus.Committed,
            requestId,
            reader.GetString(0),
            reader.GetString(1),
            new ItemOwnerRef(
                ParseEnum<ItemOwnerType>(reader.GetString(2)),
                reader.GetString(3)),
            reader.GetInt32(4),
            records,
            reader.GetFieldValue<DateTimeOffset>(6));
    }

    private static ItemRecord ReadItemRecord(NpgsqlDataReader reader)
    {
        string[] instanceTags =
            JsonSerializer.Deserialize<string[]>(
                reader.GetString(11),
                ReceiptJsonOptions)
            ?? [];
        ItemStatValue[] statValues =
            JsonSerializer.Deserialize<ItemStatValue[]>(
                reader.GetString(12),
                ReceiptJsonOptions)
            ?? [];
        Guid itemId = reader.GetGuid(0);
        ItemInstanceState state = new(
            itemId,
            reader.GetInt32(9),
            reader.GetInt32(10),
            instanceTags,
            statValues);
        ItemRecordMetadata metadata = new(
            ParseEnum<ItemBindState>(reader.GetString(15)),
            new ItemDurability(
                reader.GetInt32(16),
                reader.GetInt32(17)),
            reader.IsDBNull(18)
                ? null
                : reader.GetFieldValue<DateTimeOffset>(18),
            reader.GetString(19),
            reader.GetBoolean(20));
        return new ItemRecord(
            reader.GetString(1),
            reader.GetString(2),
            reader.GetInt32(3),
            new ItemOwnerRef(
                ParseEnum<ItemOwnerType>(reader.GetString(4)),
                reader.GetString(5)),
            new ItemLocation(
                ParseEnum<ItemContainerType>(reader.GetString(6)),
                reader.GetString(7),
                reader.GetInt32(8)),
            state,
            reader.GetInt64(13),
            ParseEnum<ItemLifecycleState>(reader.GetString(14)),
            metadata);
    }

    private static TEnum ParseEnum<TEnum>(string value)
        where TEnum : struct, Enum
    {
        return Enum.TryParse(value, out TEnum result)
            ? result
            : throw new InvalidDataException(
                $"Stored item enum value '{value}' is invalid.");
    }

    private static ItemRepositoryCommitResult Failure(
        ItemRepositoryCommitRequest request,
        ItemRepositoryCommitStatus status,
        DateTimeOffset committedAt)
    {
        return new ItemRepositoryCommitResult(
            status,
            request.RequestId,
            request.Operation,
            request.CommandFingerprint,
            request.Actor,
            request.AffectedQuantity,
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
            // The original database exception is the actionable failure.
        }
    }

    private static JsonSerializerOptions CreateJsonOptions()
    {
        JsonSerializerOptions options =
            new(JsonSerializerDefaults.Web);
        options.Converters.Add(new JsonStringEnumConverter());
        return options;
    }
}
