using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Contracts;

public static class DungeonSessionResponses
{
    public static DungeonSessionResponse From(DungeonSession session) => new(
        session.DungeonSessionId, session.DungeonId, session.Difficulty, session.State.ToString(),
        session.ServerId, session.ServerAddress, session.ExpiresAt,
        session.Members.Select(member => new DungeonSessionMemberResponse(member.CharacterId,
            member.JoinedAt, member.LeaseExpiresAt, member.CharacterName)).ToArray(),
        LeaderCharacterId: session.LeaderCharacterId, LobbyRevision: session.LobbyRevision,
        DepartureRequestId: session.DepartureRequestId, DepartureRequestRevision: session.DepartureRequestRevision,
        DepartureRequestedAt: session.DepartureRequestedAt);
}
