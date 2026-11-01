namespace ProjectRpg.Backend.Domain;

public sealed class DungeonDepartureConflictException(string code) : Exception(code);

public static class DungeonDeparturePolicy
{
    public const long MaximumRevision = 9_007_199_254_740_991; // exact JSON integer in UE
    public static long NextRevision(DungeonSession session) => session.LobbyRevision is >= 0 and < MaximumRevision
        ? session.LobbyRevision + 1 : throw new DungeonDepartureConflictException("dungeon_lobby_revision_exhausted");

    // Receipt shape only, not authorization. Apply must still recheck the current
    // member/lease and receipt under the repository gate/transaction.
    public static bool IsCommittedRequestReplay(DungeonSession session, Guid requestId, long expectedRevision) =>
        requestId != Guid.Empty && expectedRevision is >= 1 and <= MaximumRevision
        && session.DepartureRequestId == requestId && session.DepartureRequestRevision == expectedRevision
        && session.DepartureRequestedAt.HasValue
        && session.State is DungeonSessionState.Waiting or DungeonSessionState.Loading or DungeonSessionState.InProgress;

    public static DungeonSession Apply(DungeonSession session, string steamId, Guid characterId,
        Guid requestId, long expectedRevision, bool cancel, int minimumPartySize, DateTimeOffset now)
    {
        if (session.LeaderCharacterId != characterId || !session.Members.Any(member =>
            member.CharacterId == characterId && member.SteamId == steamId && member.LeaseExpiresAt > now))
            throw new DungeonSessionMembershipException(session.DungeonSessionId);
        if (requestId == Guid.Empty || expectedRevision is < 1 or > MaximumRevision)
            throw new DungeonDepartureConflictException("invalid_departure_command");
        bool same = session.DepartureRequestId == requestId && session.DepartureRequestRevision == expectedRevision;
        if (!cancel && IsCommittedRequestReplay(session, requestId, expectedRevision))
            return session; // lost response after a committed request/claim; never renew its lease
        if (session.State != DungeonSessionState.Waiting || session.ExpiresAt <= now)
            throw new DungeonSessionStateConflictException(session.DungeonSessionId, session.State);
        if (cancel)
        {
            if (!same) throw new DungeonDepartureConflictException("departure_request_mismatch");
            return session.DepartureRequestedAt is null ? session : session with
            { DepartureRequestedAt = null, LobbyRevision = NextRevision(session), UpdatedAt = now };
        }
        if (same) throw new DungeonDepartureConflictException("departure_request_cancelled");
        if (session.LobbyRevision != expectedRevision || session.DepartureRequestedAt.HasValue)
            throw new DungeonDepartureConflictException("dungeon_lobby_revision_conflict");
        if (session.Members.Count < Math.Clamp(minimumPartySize, 1, 8)
            || session.Members.Any(member => member.LeaseExpiresAt <= now))
            throw new DungeonDepartureConflictException("dungeon_party_not_ready");
        return session with { DepartureRequestId = requestId, DepartureRequestRevision = expectedRevision,
            DepartureRequestedAt = now, LobbyRevision = NextRevision(session), UpdatedAt = now };
    }

    public static bool CanAllocate(DungeonSession session, int minimumPartySize, DateTimeOffset now) =>
        session.DepartureRequestedAt.HasValue && session.LeaderCharacterId.HasValue
        && session.DepartureRequestId is Guid command && command != Guid.Empty
        && session.DepartureRequestRevision is long basis && basis >= 1 && basis < session.LobbyRevision
        && session.Members.Count >= Math.Clamp(minimumPartySize, 1, 8)
        && session.Members.All(member => member.LeaseExpiresAt > now)
        && session.Members.Any(member => member.CharacterId == session.LeaderCharacterId);

    public static DungeonSession WithRoster(DungeonSession session, IReadOnlyList<DungeonSessionMember> members, DateTimeOffset now) =>
        session with { Members = members, LobbyRevision = NextRevision(session), DepartureRequestedAt = null,
            UpdatedAt = now, LeaderCharacterId = session.LeaderCharacterId is null ? null
                : members.Any(member => member.CharacterId == session.LeaderCharacterId) ? session.LeaderCharacterId
                : members.OrderBy(member => member.JoinedAt).ThenBy(member => member.CharacterId.ToString("D"), StringComparer.Ordinal)
                    .Select(member => (Guid?)member.CharacterId).FirstOrDefault() };
}
