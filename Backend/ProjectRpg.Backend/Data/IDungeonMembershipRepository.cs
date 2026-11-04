using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public interface IDungeonMembershipRepository : IGameServerMembershipRepository
{
    Task<bool> IsActiveDungeonSessionMemberAsync(
        Guid dungeonSessionId,
        string steamId,
        Guid characterId,
        DateTimeOffset now,
        CancellationToken cancellationToken);

    Task<bool> AreAuthorizedSecurityTelemetryMembersAsync(
        Guid dungeonSessionId,
        string serverId,
        IReadOnlyList<SecurityTelemetryMember> members,
        DateTimeOffset now,
        TimeSpan postSessionGrace,
        CancellationToken cancellationToken);
}
