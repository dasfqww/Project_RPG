using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;

namespace ProjectRpg.Backend.Domain;

/// <summary>Persistent reward identities. Hash inputs and byte layout are a storage compatibility contract.</summary>
public static class DungeonRewardIdentity
{
    public static Guid CurrencyRequestId(Guid dungeonSessionId, Guid characterId, string rewardVersion)
        => GuidFromHash(Hash($"dungeon_reward|{dungeonSessionId:D}|{characterId:D}|{rewardVersion}"));

    public static Guid ItemRequestId(Guid dungeonSessionId, Guid characterId, string rewardVersion)
        => GuidFromHash(Hash($"dungeon_item_reward|{dungeonSessionId:D}|{characterId:D}|{rewardVersion}"));

    public static (Guid InstanceId, int GenerationSeed) ItemInstance(
        Guid dungeonSessionId, Guid characterId, string rewardVersion, int rewardIndex)
    {
        byte[] hash = Hash($"dungeon_reward_item|{dungeonSessionId:D}|{characterId:D}|{rewardVersion}|{rewardIndex}");
        return (GuidFromHash(hash), BinaryPrimitives.ReadInt32LittleEndian(hash.AsSpan(16, 4)) & int.MaxValue);
    }

    private static byte[] Hash(string source) => SHA256.HashData(Encoding.UTF8.GetBytes(source));

    private static Guid GuidFromHash(byte[] hash)
    {
        Span<byte> bytes = stackalloc byte[16];
        hash.AsSpan(0, 16).CopyTo(bytes);
        bytes[0] |= 1;
        return new Guid(bytes);
    }
}
