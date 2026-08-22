using System.Security.Cryptography;
using System.Text;
using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Authentication;

public sealed class BearerAuthenticator(
    IGameRepository repository,
    AccessTokenService accessTokenService,
    IOptions<AuthOptions> options,
    TimeProvider timeProvider)
{
    private readonly AuthOptions _options = options.Value;

    public async Task<AuthenticatedPrincipal?> AuthenticateAsync(
        string token,
        CancellationToken cancellationToken)
    {
        if (!string.IsNullOrWhiteSpace(_options.AdminToken)
            && FixedTimeEquals(token, _options.AdminToken))
        {
            return new AuthenticatedPrincipal(
                PrincipalKind.Administrator,
                null,
                null);
        }

        string tokenHash = accessTokenService.Hash(token);
        DateTimeOffset now = timeProvider.GetUtcNow();
        GameServerCredential? gameServerCredential =
            await repository.ResolveGameServerCredentialAsync(
                tokenHash,
                now,
                cancellationToken);
        if (gameServerCredential is not null)
        {
            return new AuthenticatedPrincipal(
                PrincipalKind.GameServer,
                null,
                gameServerCredential.ServerId,
                gameServerCredential.DungeonSessionId);
        }

        string? steamId = await repository.ResolveSessionAsync(
            tokenHash,
            now,
            cancellationToken);
        return steamId is null
            ? null
            : new AuthenticatedPrincipal(
                PrincipalKind.Player,
                steamId,
                null,
                null);
    }

    private static bool FixedTimeEquals(string left, string right)
    {
        byte[] leftHash = SHA256.HashData(Encoding.UTF8.GetBytes(left));
        byte[] rightHash = SHA256.HashData(Encoding.UTF8.GetBytes(right));
        return CryptographicOperations.FixedTimeEquals(leftHash, rightHash);
    }
}
