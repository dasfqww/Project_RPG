namespace ProjectRpg.Backend.Domain;

public enum DungeonRewardSettlementState
{
    Pending,
    Processing,
    Completed,
    Failed
}

public sealed record DungeonRewardSettlement(
    Guid DungeonSessionId,
    string ServerId,
    string RewardVersion,
    string CommandFingerprint,
    DungeonRewardSettlementState State,
    IReadOnlyList<Guid> CharacterIds,
    IReadOnlyList<CurrencyChange> Changes,
    IReadOnlyList<DungeonItemReward> ItemRewards,
    int AttemptCount,
    DateTimeOffset NextAttemptAt,
    string? WorkerId,
    DateTimeOffset? ProcessingExpiresAt,
    string? LastError,
    DateTimeOffset CreatedAt,
    DateTimeOffset UpdatedAt,
    DateTimeOffset? CompletedAt);

public sealed record DungeonItemReward(
    string DefinitionType,
    string DefinitionName,
    int DefinitionVersion,
    int Quantity,
    ItemBindState BindState,
    int DurabilityCurrent,
    int DurabilityMaximum,
    IReadOnlyList<string> InstanceTags,
    IReadOnlyList<ItemStatValue> StatValues);

public static class DungeonRewardRules
{
    public const int MaximumItemRewardCount = 16;

    public static bool TryValidateItemReward(
        DungeonItemReward reward,
        out string error)
    {
        if (!IsBoundedText(reward.DefinitionType, 64)
            || !IsBoundedText(reward.DefinitionName, 128)
            || reward.DefinitionVersion < 1
            || reward.Quantity <= 0
            || !Enum.IsDefined(reward.BindState))
        {
            error = "Item reward definition, version, and quantity are invalid.";
            return false;
        }

        if (reward.DurabilityMaximum < 0
            || reward.DurabilityCurrent < 0
            || reward.DurabilityCurrent > reward.DurabilityMaximum)
        {
            error = "Item reward durability is invalid.";
            return false;
        }

        if (reward.InstanceTags.Count > 64
            || reward.StatValues.Count > 128)
        {
            error = "Item reward instance metadata is too large.";
            return false;
        }

        HashSet<string> tags = new(StringComparer.Ordinal);
        foreach (string tag in reward.InstanceTags)
        {
            if (!IsBoundedText(tag, 128) || !tags.Add(tag))
            {
                error = "Item reward tags are invalid or duplicated.";
                return false;
            }
        }

        HashSet<string> statTags = new(StringComparer.Ordinal);
        foreach (ItemStatValue statValue in reward.StatValues)
        {
            if (!IsBoundedText(statValue.StatTag, 128)
                || !double.IsFinite(statValue.Value)
                || !statTags.Add(statValue.StatTag))
            {
                error = "Item reward stat values are invalid or duplicated.";
                return false;
            }
        }

        error = string.Empty;
        return true;
    }

    private static bool IsBoundedText(string value, int maximumLength)
    {
        return !string.IsNullOrWhiteSpace(value)
            && value.Length <= maximumLength
            && !value.Any(char.IsControl);
    }
}
