using System.Globalization;
using System.Security.Cryptography;
using System.Text;

namespace ProjectRpg.Backend.Domain.Trading;

// A consent version, not an authorization token. Terms remain server-owned and immutable.
public sealed record AuctionRegistrationPolicy(AuctionTerms Terms, DateTimeOffset ObservedAt)
{
    public string PolicyTag => Tag(Terms);
    public static string Tag(AuctionTerms terms)
    {
        string canonical = string.Join('|', "auction_registration_policy_v1", terms.CurrencyCode,
            terms.MinimumTotalPrice.ToString(CultureInfo.InvariantCulture), terms.MaximumTotalPrice.ToString(CultureInfo.InvariantCulture),
            terms.MinimumDuration.Ticks.ToString(CultureInfo.InvariantCulture), terms.MaximumDuration.Ticks.ToString(CultureInfo.InvariantCulture),
            terms.FeeBasisPoints.ToString(CultureInfo.InvariantCulture));
        return Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(canonical)));
    }
    public static bool ValidTag(string tag) => tag.Length == 64 && tag.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f');
}
