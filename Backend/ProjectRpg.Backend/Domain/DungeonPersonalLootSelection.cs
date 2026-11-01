using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;

namespace ProjectRpg.Backend.Domain;

/// <summary>One optional exclusive pool alongside unchanged guaranteed rewards.</summary>
public static class DungeonPersonalLootSelection
{
    public const int BucketCount = 10000;

    public static bool IsValidPool(IReadOnlyList<DungeonItemReward> rewards)
        => rewards.Count <= DungeonRewardRules.MaximumItemRewardCount
            && rewards.All(reward => reward.PersonalLootWeight is null
                || (reward.PersonalLootWeight is > 0 and <= BucketCount && reward.MaxStackSize > 0))
            && rewards.Sum(reward => reward.PersonalLootWeight ?? 0) <= BucketCount;

    // Called only for a new settlement while its session lock is held. Existing
    // receipts return before here; no worker/retry draws new randomness.
    public static DungeonItemReward[] Capture(IReadOnlyList<DungeonItemReward> rewards)
    {
        if (!IsValidPool(rewards) || rewards.Any(reward => reward.PersonalLootSeed is not null))
            throw new ArgumentException("Invalid personal loot request or caller-supplied seed.");
        string? seed = rewards.Any(reward => reward.PersonalLootWeight.HasValue)
            ? Convert.ToHexString(RandomNumberGenerator.GetBytes(32)) : null;
        return rewards.Select(reward => reward.PersonalLootWeight.HasValue
            ? reward with { PersonalLootSeed = seed } : reward).ToArray();
    }

    public static int[] SelectIndices(Guid sessionId, Guid characterId, string rewardVersion,
        IReadOnlyList<DungeonItemReward> rewards)
    {
        if (!IsValidPool(rewards)) throw new InvalidDataException("Invalid frozen personal loot pool.");
        if (!rewards.Any(reward => reward.PersonalLootWeight.HasValue))
        {
            if (rewards.Any(reward => reward.PersonalLootSeed is not null))
                throw new InvalidDataException("Unexpected personal loot seed.");
            return Enumerable.Range(0, rewards.Count).ToArray();
        }
        string? seed = rewards.First(reward => reward.PersonalLootWeight.HasValue).PersonalLootSeed;
        if (seed is null || seed.Length != 64 || seed.Any(c => !char.IsAsciiHexDigit(c))
            || rewards.Any(reward => reward.PersonalLootWeight.HasValue
                ? reward.PersonalLootSeed != seed : reward.PersonalLootSeed is not null))
            throw new InvalidDataException("Missing or inconsistent frozen personal loot seed.");
        // Keyed per settlement, bound to the immutable recipient/version. The
        // public session GUID cannot reveal the outcome before the clear.
        byte[] key = Convert.FromHexString(seed);
        ulong limit = ulong.MaxValue - ulong.MaxValue % BucketCount;
        for (int counter = 0; ; counter++)
        {
            string input = FormattableString.Invariant($"dungeon_personal_loot_v1|{sessionId:D}|{characterId:D}|{rewardVersion}|{counter}");
            byte[] hash = HMACSHA256.HashData(key, Encoding.UTF8.GetBytes(input));
            ulong value = BinaryPrimitives.ReadUInt64LittleEndian(hash);
            // Rejection rather than biased modulo reduction.
            if (value < limit) return SelectBucket(rewards, (int)(value % BucketCount));
        }
    }

    // Public pure policy entrypoint for exact bucket-boundary/content checks;
    // no endpoint allows clients to supply this bucket or the captured seed.
    public static int[] SelectBucket(IReadOnlyList<DungeonItemReward> rewards, int bucket)
    {
        if (!IsValidPool(rewards) || bucket is < 0 or >= BucketCount)
            throw new ArgumentException("Invalid loot pool/bucket.");
        List<int> selected = [];
        int end = 0;
        bool optionalSelected = false;
        for (int index = 0; index < rewards.Count; index++)
        {
            if (rewards[index].PersonalLootWeight is not int weight) selected.Add(index);
            else
            {
                end += weight;
                if (!optionalSelected && bucket < end) { selected.Add(index); optionalSelected = true; }
            }
        }
        return selected.ToArray();
    }
}
