namespace ProjectRpg.Backend.Configuration;

public sealed class AuthOptions
{
    public const string SectionName = "Auth";

    public int SessionLifetimeMinutes { get; init; } = 60;
    public int JoinTicketLifetimeSeconds { get; init; } = 60;
    public int GameServerTokenLifetimeMinutes { get; init; } = 12 * 60;
    public string AdminToken { get; init; } = string.Empty;
}

public sealed class SteamOptions
{
    public const string SectionName = "Steam";

    public uint AppId { get; init; } = 480;
    public string PublisherKey { get; init; } = string.Empty;
    public string TicketIdentity { get; init; } = "ProjectRpgBackend";
    public string AuthenticateUserTicketUrl { get; init; } =
        "https://partner.steam-api.com/ISteamUserAuth/AuthenticateUserTicket/v1/";
    public bool AllowDevelopmentAuthentication { get; init; }
}

public sealed class StorageOptions
{
    public const string SectionName = "Storage";

    public string Provider { get; init; } = "Postgres";
}

public sealed class DungeonSessionOptions
{
    public const string SectionName = "DungeonSessions";

    public int MaxPartySize { get; init; } = 4;
    public int WaitingLifetimeMinutes { get; init; } = 5;
    public int ActiveLeaseSeconds { get; init; } = 90;
}

public sealed class DungeonSettlementOptions
{
    public const string SectionName = "DungeonSettlements";

    public int PollIntervalMilliseconds { get; init; } = 250;
    public int ProcessingLeaseSeconds { get; init; } = 30;
    public int MaximumAttempts { get; init; } = 10;
    public int MaximumRetryDelaySeconds { get; init; } = 30;
}
