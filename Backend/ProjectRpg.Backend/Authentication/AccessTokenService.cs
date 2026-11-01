using System.Security.Cryptography;
using System.Text;

namespace ProjectRpg.Backend.Authentication;

public sealed record IssuedAccessToken(
    string Token,
    string TokenHash,
    DateTimeOffset ExpiresAt,
    DateTimeOffset IssuedAt);

public sealed class AccessTokenService(TimeProvider timeProvider)
{
    public IssuedAccessToken Issue(TimeSpan lifetime)
    {
        byte[] tokenBytes = RandomNumberGenerator.GetBytes(32);
        string token = Convert.ToBase64String(tokenBytes)
            .TrimEnd('=')
            .Replace('+', '-')
            .Replace('/', '_');
        DateTimeOffset issuedAt = timeProvider.GetUtcNow();
        DateTimeOffset expiresAt = issuedAt.Add(lifetime);
        return new IssuedAccessToken(token, Hash(token), expiresAt, issuedAt);
    }

    public string Hash(string token)
    {
        byte[] hash = SHA256.HashData(Encoding.UTF8.GetBytes(token));
        return Convert.ToHexString(hash);
    }
}
