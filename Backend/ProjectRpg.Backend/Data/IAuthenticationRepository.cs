using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public interface IAuthenticationRepository
{
    Task UpsertAccountAsync(
        SteamIdentity identity,
        DateTimeOffset authenticatedAt,
        CancellationToken cancellationToken);

    Task StoreSessionAsync(
        string tokenHash,
        string steamId,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken);

    Task<string?> ResolveSessionAsync(
        string tokenHash,
        DateTimeOffset now,
        CancellationToken cancellationToken);

    Task RevokeSessionAsync(string tokenHash, string steamId, CancellationToken cancellationToken);

    Task StoreGameServerCredentialAsync(
        string tokenHash,
        string serverId,
        Guid dungeonSessionId,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken);

    Task<GameServerCredential?> ResolveGameServerCredentialAsync(
        string tokenHash,
        DateTimeOffset now,
        TimeSpan postSessionGrace,
        CancellationToken cancellationToken);
}
