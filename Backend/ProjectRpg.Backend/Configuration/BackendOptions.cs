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
    public int ReadinessTimeoutSeconds { get; init; } = 3;
}

public sealed class ApiRateLimitOptions
{
    public const string SectionName = "RateLimits";

    public bool Enabled { get; init; } = true;
    public int WindowSeconds { get; init; } = 60;
    public int AuthenticationPermitLimit { get; init; } = 60;
    public int IngressPermitLimit { get; init; } = 6000;
    public int PlayerPermitLimit { get; init; } = 600;
    public int GameServerPermitLimit { get; init; } = 6000;
    public int AdministratorPermitLimit { get; init; } = 3000;
}

public sealed class DungeonSessionOptions
{
    public const string SectionName = "DungeonSessions";

    public int MaxPartySize { get; init; } = 4;
    public int MinimumDeparturePartySize { get; init; } = 1;
    public int WaitingLifetimeMinutes { get; init; } = 5;
    public int ServerStartupLeaseSeconds { get; init; } = 300;
    public int ActiveLeaseSeconds { get; init; } = 90;

    // Only the backend chooses this bound. Running servers still use the
    // shorter active lease on start/heartbeat; token expiry is independent.
    public DateTimeOffset GetServerStartupLeaseExpiry(DateTimeOffset now) =>
        now.AddSeconds(Math.Clamp(ServerStartupLeaseSeconds, 30, 15 * 60));
}

public sealed class DungeonSettlementOptions
{
    public const string SectionName = "DungeonSettlements";

    public int PollIntervalMilliseconds { get; init; } = 250;
    public int ProcessingLeaseSeconds { get; init; } = 30;
    public int MaximumAttempts { get; init; } = 10;
    public int MaximumRetryDelaySeconds { get; init; } = 30;
}

public sealed class SecurityTelemetryOptions
{
    public const string SectionName = "SecurityTelemetry";

    /** Allows an assigned game server to drain immutable audit events after finish. */
    public int PostSessionGraceSeconds { get; init; } = 5 * 60;
}
