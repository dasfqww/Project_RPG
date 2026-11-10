using System.Collections.Immutable;
using Npgsql;
using ProjectRpg.Backend.Narrative;

namespace ProjectRpg.Backend.Data;

public sealed partial class PostgresGameRepository : INarrativeHistoryRepository
{
    public async Task<ImmutableArray<NarrativeState>> LoadAccountNarrativeHistoryAsync(
        Guid characterId, string steamId, CancellationToken cancellationToken)
    {
        await using var connection = await dataSource.OpenConnectionAsync(cancellationToken);
        // Ownership and all account states come from ONE MVCC statement snapshot. No row locks or inserts.
        await using NpgsqlCommand query = new("""
            WITH selected_owner AS (
                SELECT steam_id FROM characters WHERE character_id = @id AND steam_id = @owner
            )
            SELECT c.character_id, n.state::TEXT
            FROM characters c
            JOIN selected_owner o ON c.steam_id = o.steam_id
            LEFT JOIN character_narrative n ON n.character_id = c.character_id
            ORDER BY c.character_id;
            """, connection);
        query.Parameters.AddWithValue("id", characterId);
        query.Parameters.AddWithValue("owner", steamId);
        var states = ImmutableArray.CreateBuilder<NarrativeState>();
        await using var reader = await query.ExecuteReaderAsync(cancellationToken);
        while (await reader.ReadAsync(cancellationToken))
        {
            Guid id = reader.GetGuid(0);
            states.Add(reader.IsDBNull(1) ? NarrativeState.Create(id) : ReadNarrative(reader.GetString(1), id));
        }
        if (states.Count == 0) throw new NarrativeConflictException("character_not_owned");
        return states.ToImmutable();
    }
}
