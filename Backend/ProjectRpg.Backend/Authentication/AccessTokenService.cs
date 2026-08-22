using System.Security.Cryptography;
using System.Text;

namespace ProjectRpg.Backend.Authentication;

public sealed record IssuedAccessToken(
    string Token,
    string TokenHash,
    DateTimeOffset ExpiresAt);

public sealed class AccessTokenService(TimeProvider timeProvider)
{
    public IssuedAccessToken Issue(TimeSpan lifetime)
    {
        byte[] tokenBytes = RandomNumberGenerator.GetBytes(32);
        string token = Convert.ToBase64String(tokenBytes)
            .TrimEnd('=')
            .Replace('+', '-')
            .Replace('/', '_');
        DateTimeOffset expiresAt = timeProvider.GetUtcNow().Add(lifetime);
        return new IssuedAccessToken(token, Hash(token), expiresAt);
    }

    public string Hash(string token)
    {
        byte[] hash = SHA256.HashData(Encoding.UTF8.GetBytes(token));
        return Convert.ToHexString(hash);
    }
}
