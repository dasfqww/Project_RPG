using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Authentication;

// Restricted credentials may read exactly their own membership snapshot.
// This does not restore admission, heartbeat, economy or settlement authority.
public static class DungeonMembershipSnapshotAccess
{
    public static bool IsOwnReadOnlyRoute(string method, PathString path, AuthenticatedPrincipal principal)
    {
        if (!HttpMethods.IsGet(method) || !principal.IsGameServer || !principal.DungeonSessionId.HasValue)
            return false;
        string[] segments = (path.Value ?? string.Empty).Split('/');
        return segments.Length == 5 && segments[0].Length == 0
            && segments[1].Equals("api", StringComparison.OrdinalIgnoreCase)
            && segments[2].Equals("dungeon-sessions", StringComparison.OrdinalIgnoreCase)
            && segments[4].Equals("membership-snapshot", StringComparison.OrdinalIgnoreCase)
            && Guid.TryParse(segments[3], out Guid sessionId) && sessionId != Guid.Empty
            && sessionId == principal.DungeonSessionId;
    }
}
