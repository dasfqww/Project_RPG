using System.Globalization;
using System.Security.Cryptography;
using System.Text;

namespace ProjectRpg.Backend.Riding;

public enum RidingFailure
{
    InvalidCatalog, InvalidRequest, DefinitionMismatch, ItemUnavailable, WrongInventory,
    RevisionConflict, AssetReserved, ItemLocked, ItemExpired, RosterRevisionOverflow, RosterLimitReached
}

public sealed class RidingRuleException(RidingFailure code) : Exception($"Riding rule rejected: {code}")
{
    public RidingFailure Code { get; } = code;
}

internal static class RidingIdentity
{
    internal static bool Identifier(string? value, int maximum, bool lowerOnly = false) =>
        value is { Length: > 0 } && value.Length <= maximum && AlphaNumeric(value[0], lowerOnly)
        && value.All(c => AlphaNumeric(c, lowerOnly) || c is '.' or '_' or '-');

    private static bool AlphaNumeric(char c, bool lowerOnly) =>
        c is >= 'a' and <= 'z' or >= '0' and <= '9' || !lowerOnly && c is >= 'A' and <= 'Z';

    internal static bool RegionTag(string? value) => value is { Length: > 0 and <= 128 }
        && value.Split('.').All(part => part.Length > 0 && part.All(c => AlphaNumeric(c, false) || c == '_'));

    internal static bool Hash(string? value) => value is { Length: 64 }
        && value.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f');

    internal static string Sha256(string value) => Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(value))).ToLowerInvariant();
    internal static string FloatBits(float value) => BitConverter.SingleToInt32Bits(value).ToString("x8", CultureInfo.InvariantCulture);
}
