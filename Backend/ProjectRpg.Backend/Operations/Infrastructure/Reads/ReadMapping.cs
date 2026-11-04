using System.Text.Json;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Inventory;
using ProjectRpg.Operations.Core.Sessions;

namespace ProjectRpg.Backend.Operations.Infrastructure.Reads;

internal static class ReadMapping
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);
    internal static ProgressionSummary Progression(Guid id, string? json)
    {
        if (json is null) return new(ProgressionState.Missing, null, null);
        try { return Progression(CharacterProgressionSummary.FromStored(id, JsonSerializer.Deserialize<CharacterProgressionState>(json, Json))); }
        catch (JsonException) { return new(ProgressionState.Unavailable, null, null); }
    }
    internal static ProgressionSummary Progression(CharacterProgressionSummary summary) =>
        new(Enum.Parse<ProgressionState>(summary.State), summary.ClassId, summary.Level);
    internal static ItemSummary Item(ItemRecord item) => new(item.ItemId, item.DefinitionType, item.DefinitionName,
        item.DefinitionVersion, item.State.Quantity, item.Revision, item.Location.ContainerType.ToString(),
        item.Location.ContainerId, item.Location.SlotIndex, item.LifecycleState.ToString(),
        item.Metadata.BindState.ToString(), item.Metadata.IsLocked, item.Metadata.ExpiresAtUtc,
        new(item.Metadata.Durability.Current, item.Metadata.Durability.Maximum));
    internal static SettlementSummary? Settlement(DungeonRewardSettlement? settlement) => settlement is null ? null :
        new(settlement.State.ToString(), settlement.AttemptCount, settlement.NextAttemptAt, settlement.CompletedAt,
            string.IsNullOrEmpty(settlement.LastError) ? null : "settlement_error");
    internal static Page<T> Page<T>(IReadOnlyList<T> rows, int limit, Func<T, PagePosition> position)
    {
        T[] items = rows.Take(limit).ToArray();
        bool more = rows.Count > limit;
        return new(Array.AsReadOnly(items), more, more ? position(items[^1]) : null);
    }
}
