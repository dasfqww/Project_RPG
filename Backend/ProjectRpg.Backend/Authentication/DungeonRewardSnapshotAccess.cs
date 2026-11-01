using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Authentication;

/// <summary>One explicitly scoped read-only exception for restricted game-server credentials.</summary>
public static class DungeonRewardSnapshotAccess
{
    public static bool IsOwnReadOnlyRoute(string method, PathString path, AuthenticatedPrincipal principal)
    {
        if (!HttpMethods.IsGet(method) || !principal.IsGameServer || !principal.DungeonSessionId.HasValue)
            return false;
        string[] segments = (path.Value ?? string.Empty).Split('/');
        return segments.Length == 6 && segments[0].Length == 0
            && segments[1].Equals("api", StringComparison.OrdinalIgnoreCase)
            && segments[2].Equals("dungeon-sessions", StringComparison.OrdinalIgnoreCase)
            && segments[4].Equals("reward-snapshot", StringComparison.OrdinalIgnoreCase)
            && Guid.TryParse(segments[3], out Guid sessionId) && sessionId == principal.DungeonSessionId
            && Guid.TryParse(segments[5], out Guid characterId) && characterId != Guid.Empty;
    }
}
