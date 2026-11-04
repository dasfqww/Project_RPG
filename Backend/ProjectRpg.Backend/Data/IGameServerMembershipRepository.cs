namespace ProjectRpg.Backend.Data;

public interface IGameServerMembershipRepository
{
    Task<bool> IsAuthorizedGameServerSessionMemberAsync(Guid dungeonSessionId, string serverId,
        string steamId, Guid characterId, DateTimeOffset now, CancellationToken cancellationToken);
}
