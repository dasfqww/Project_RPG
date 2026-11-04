using ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;
using Npgsql;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Wallets;

namespace ProjectRpg.Backend.Operations.Infrastructure.Reads;

public sealed class PostgresWalletReadStore(NpgsqlDataSource source, OperationsStorageTrace trace) : IWalletReadStore
{
    public Task<Result<WalletSnapshot>> ReadAsync(Guid characterId, CancellationToken ct) =>
        PostgresReadSession.RunAsync(source, trace, async session =>
        {
            string account;
            Guid roster;
            await using (NpgsqlCommand owner = session.Command("SELECT steam_id,roster_id FROM characters WHERE character_id=@id;"))
            {
                owner.Parameters.AddWithValue("id", characterId);
                await using NpgsqlDataReader reader = await owner.ExecuteReaderAsync(ct);
                if (!await reader.ReadAsync(ct)) return Result<WalletSnapshot>.Fail(Failures.CharacterMissing);
                account = reader.GetString(0); roster = reader.GetGuid(1);
            }
            await using NpgsqlCommand command = session.Command("""
                SELECT d.currency_code,d.display_name,d.scope,
                    CASE d.scope WHEN 'Account' THEN @account WHEN 'Roster' THEN @roster ELSE @character END owner_id,
                    COALESCE(b.balance,0),COALESCE(b.revision,0)
                FROM currency_definitions d LEFT JOIN currency_balances b ON b.currency_code=d.currency_code
                    AND b.owner_type=d.scope AND b.owner_id=
                    CASE d.scope WHEN 'Account' THEN @account WHEN 'Roster' THEN @roster ELSE @character END
                WHERE d.enabled=TRUE ORDER BY d.currency_code;
                """);
            command.Parameters.AddWithValue("account", account);
            command.Parameters.AddWithValue("roster", roster.ToString("D"));
            command.Parameters.AddWithValue("character", characterId.ToString("D"));
            List<WalletLine> rows = [];
            await using (NpgsqlDataReader reader = await command.ExecuteReaderAsync(ct))
                while (await reader.ReadAsync(ct)) rows.Add(new(reader.GetString(0), reader.GetString(1), reader.GetString(2),
                    reader.GetString(3), reader.GetInt64(4), reader.GetInt64(5)));
            return Result<WalletSnapshot>.Success(new(characterId, roster, rows.AsReadOnly()));
        }, ct);
}
