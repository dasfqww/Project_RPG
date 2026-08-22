using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.Options;
using Npgsql;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Validation;

WebApplicationBuilder builder = WebApplication.CreateBuilder(args);
ApplyEnvironmentOverrides(builder.Configuration);
ValidateAuthConfiguration(
    builder.Configuration.GetSection(AuthOptions.SectionName)
        .Get<AuthOptions>() ?? new AuthOptions());

builder.Services.Configure<AuthOptions>(
    builder.Configuration.GetSection(AuthOptions.SectionName));
builder.Services.Configure<SteamOptions>(
    builder.Configuration.GetSection(SteamOptions.SectionName));
builder.Services.Configure<StorageOptions>(
    builder.Configuration.GetSection(StorageOptions.SectionName));
builder.Services.Configure<DungeonSessionOptions>(
    builder.Configuration.GetSection(DungeonSessionOptions.SectionName));
builder.Services.Configure<DungeonSettlementOptions>(
    builder.Configuration.GetSection(DungeonSettlementOptions.SectionName));
builder.Services.AddSingleton<TimeProvider>(TimeProvider.System);
builder.Services.AddSingleton<AccessTokenService>();
builder.Services.AddScoped<BearerAuthenticator>();
builder.Services.AddHttpClient<ISteamTicketValidator, SteamTicketValidator>(client =>
{
    client.Timeout = TimeSpan.FromSeconds(10);
});

string storageProvider = builder.Configuration["Storage:Provider"]
    ?? throw new InvalidOperationException("Storage:Provider is required.");
if (string.Equals(storageProvider, "Postgres", StringComparison.OrdinalIgnoreCase))
{
    string? configuredConnectionString =
        builder.Configuration.GetConnectionString("Postgres");
    if (string.IsNullOrWhiteSpace(configuredConnectionString))
    {
        throw new InvalidOperationException(
            "ConnectionStrings:Postgres is required when Storage:Provider is Postgres.");
    }
    string connectionString = configuredConnectionString;
    NpgsqlDataSource dataSource = NpgsqlDataSource.Create(connectionString);
    builder.Services.AddSingleton(dataSource);
    builder.Services.AddSingleton<IGameRepository, PostgresGameRepository>();
    builder.Services.AddSingleton<PostgresItemRepository>();
    builder.Services.AddSingleton<IItemRepository>(provider =>
        provider.GetRequiredService<PostgresItemRepository>());
    builder.Services.AddSingleton<PostgresEconomyRepository>();
    builder.Services.AddSingleton<IEconomyRepository>(provider =>
        provider.GetRequiredService<PostgresEconomyRepository>());
    builder.Services.AddSingleton<
        IDungeonRewardCommitRepository,
        PostgresDungeonRewardCommitRepository>();
}
else if (string.Equals(storageProvider, "Memory", StringComparison.OrdinalIgnoreCase))
{
    if (!builder.Environment.IsDevelopment()
        && !builder.Environment.IsEnvironment("Testing"))
    {
        throw new InvalidOperationException(
            "The Memory storage provider is allowed only in Development or Testing. "
            + "Configure PostgreSQL before starting another environment.");
    }

    builder.Services.AddSingleton<IGameRepository, InMemoryGameRepository>();
    builder.Services.AddSingleton<InMemoryTransactionGate>();
    builder.Services.AddSingleton<InMemoryItemRepository>();
    builder.Services.AddSingleton<IItemRepository>(provider =>
        provider.GetRequiredService<InMemoryItemRepository>());
    builder.Services.AddSingleton<InMemoryEconomyRepository>();
    builder.Services.AddSingleton<IEconomyRepository>(provider =>
        provider.GetRequiredService<InMemoryEconomyRepository>());
    builder.Services.AddSingleton<
        IDungeonRewardCommitRepository,
        InMemoryDungeonRewardCommitRepository>();
}
else
{
    throw new InvalidOperationException(
        $"Unsupported storage provider '{storageProvider}'. Use Memory or Postgres.");
}

builder.Services.AddSingleton<ItemTransactionService>();
builder.Services.AddSingleton<ItemOwnerAccessService>();
builder.Services.AddSingleton<CurrencyTransactionService>();
builder.Services.AddHostedService<DungeonRewardSettlementWorker>();

WebApplication app = builder.Build();

await app.Services.GetRequiredService<IGameRepository>()
    .EnsureCreatedAsync(CancellationToken.None);

app.UseMiddleware<ApiAuthenticationMiddleware>();

app.MapGet("/health", () => Results.Ok(new { status = "ok" }));

app.MapPost(
    "/api/auth/steam-ticket",
    async (
        SteamTicketRequest request,
        ISteamTicketValidator steamTicketValidator,
        IGameRepository repository,
        AccessTokenService accessTokenService,
        IOptions<AuthOptions> authOptions,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        if (string.IsNullOrWhiteSpace(request.Ticket))
        {
            return Results.BadRequest(new { error = "Steam ticket is required." });
        }

        try
        {
            SteamIdentity identity = await steamTicketValidator.ValidateAsync(
                request.Ticket,
                cancellationToken);
            DateTimeOffset now = timeProvider.GetUtcNow();
            await repository.UpsertAccountAsync(identity, now, cancellationToken);

            int lifetimeMinutes = Math.Clamp(
                authOptions.Value.SessionLifetimeMinutes,
                5,
                24 * 60);
            IssuedAccessToken accessToken = accessTokenService.Issue(
                TimeSpan.FromMinutes(lifetimeMinutes));
            await repository.StoreSessionAsync(
                accessToken.TokenHash,
                identity.SteamId,
                accessToken.ExpiresAt,
                cancellationToken);

            return Results.Ok(new SteamTicketResponse(
                accessToken.Token,
                accessToken.ExpiresAt,
                identity.SteamId));
        }
        catch (SteamTicketRejectedException exception)
        {
            return Results.Problem(
                statusCode: StatusCodes.Status401Unauthorized,
                title: "Steam authentication failed",
                detail: exception.Message);
        }
        catch (SteamAuthenticationUnavailableException exception)
        {
            return Results.Problem(
                statusCode: StatusCodes.Status503ServiceUnavailable,
                title: "Steam authentication is unavailable",
                detail: exception.Message);
        }
    });

app.MapGet(
    "/api/characters",
    async (HttpContext context, IGameRepository repository, CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        IReadOnlyList<GameCharacter> characters = await repository.GetCharactersAsync(
            principal.SteamId,
            cancellationToken);
        return Results.Ok(characters.Select(ToCharacterResponse));
    });

app.MapGet(
    "/api/rosters",
    async (
        HttpContext context,
        IGameRepository repository,
        CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        IReadOnlyList<Roster> rosters = await repository.GetRostersAsync(
            principal.SteamId,
            cancellationToken);
        return Results.Ok(rosters.Select(ToRosterResponse));
    });

app.MapPost(
    "/api/characters",
    async (
        CreateCharacterRequest request,
        HttpContext context,
        IGameRepository repository,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        string? validationError = RequestValidators.ValidateCharacterName(
            request.Name,
            out string normalizedName);
        if (validationError is not null)
        {
            return Results.BadRequest(new { error = validationError });
        }

        try
        {
            GameCharacter character = await repository.CreateCharacterAsync(
                principal.SteamId,
                normalizedName,
                timeProvider.GetUtcNow(),
                cancellationToken);
            return Results.Created(
                $"/api/characters/{character.CharacterId}",
                ToCharacterResponse(character));
        }
        catch (DuplicateCharacterNameException exception)
        {
            return Results.Conflict(new { error = exception.Message });
        }
    });

app.MapGet(
    "/api/characters/{characterId:guid}/active-dungeon-session",
    async (
        Guid characterId,
        HttpContext context,
        IGameRepository repository,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        string? ownerSteamId = await repository.GetCharacterOwnerAsync(
            characterId,
            cancellationToken);
        if (ownerSteamId is null)
        {
            return Results.NotFound();
        }

        if (!string.Equals(
            ownerSteamId,
            principal.SteamId,
            StringComparison.Ordinal))
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        DungeonSession? session =
            await repository.GetActiveDungeonSessionForCharacterAsync(
                characterId,
                principal.SteamId,
                timeProvider.GetUtcNow(),
                cancellationToken);
        return session is null
            ? Results.NoContent()
            : Results.Ok(ToDungeonSessionResponse(session));
    });

app.MapPost(
    "/api/dungeon-sessions",
    async (
        CreateDungeonSessionRequest request,
        HttpContext context,
        IGameRepository repository,
        AccessTokenService accessTokenService,
        IOptions<AuthOptions> authOptions,
        IOptions<DungeonSessionOptions> sessionOptions,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        string? validationError = RequestValidators.ValidateDungeonSelection(
            request.DungeonId,
            request.Difficulty,
            out string dungeonId,
            out string difficulty);
        if (validationError is not null)
        {
            return Results.BadRequest(new { error = validationError });
        }

        string? ownerSteamId = await repository.GetCharacterOwnerAsync(
            request.CharacterId,
            cancellationToken);
        if (ownerSteamId is null)
        {
            return Results.NotFound();
        }

        if (!string.Equals(
            ownerSteamId,
            principal.SteamId,
            StringComparison.Ordinal))
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        DateTimeOffset now = timeProvider.GetUtcNow();
        int waitingLifetimeMinutes = Math.Clamp(
            sessionOptions.Value.WaitingLifetimeMinutes,
            1,
            60);
        try
        {
            DungeonSession session = await repository.CreateDungeonSessionAsync(
                principal.SteamId,
                request.CharacterId,
                dungeonId,
                difficulty,
                now,
                now.AddMinutes(waitingLifetimeMinutes),
                cancellationToken);
            return Results.Created(
                $"/api/dungeon-sessions/{session.DungeonSessionId}",
                ToDungeonSessionResponse(session));
        }
        catch (Exception exception) when (IsDungeonDomainException(exception))
        {
            return ToDungeonErrorResult(exception);
        }
    });

app.MapGet(
    "/api/dungeon-sessions/{dungeonSessionId:guid}",
    async (
        Guid dungeonSessionId,
        HttpContext context,
        IGameRepository repository,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        DungeonSession? session = await repository.GetDungeonSessionAsync(
            dungeonSessionId,
            timeProvider.GetUtcNow(),
            cancellationToken);
        if (session is null)
        {
            return Results.NotFound();
        }

        bool isAuthorizedServer = principal.IsGameServer
            && session.ServerId is not null
            && principal.MatchesGameServer(session.ServerId);
        if (!isAuthorizedServer
            && !principal.IsAdministrator
            && (principal.SteamId is null
                || !session.Members.Any(member =>
                    member.SteamId == principal.SteamId)))
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        return Results.Ok(ToDungeonSessionResponse(session));
    });

app.MapPost(
    "/api/dungeon-sessions/{dungeonSessionId:guid}/members",
    async (
        Guid dungeonSessionId,
        JoinDungeonSessionRequest request,
        HttpContext context,
        IGameRepository repository,
        IOptions<DungeonSessionOptions> sessionOptions,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        string? ownerSteamId = await repository.GetCharacterOwnerAsync(
            request.CharacterId,
            cancellationToken);
        if (ownerSteamId is null)
        {
            return Results.NotFound();
        }

        if (!string.Equals(
            ownerSteamId,
            principal.SteamId,
            StringComparison.Ordinal))
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        DateTimeOffset now = timeProvider.GetUtcNow();
        int waitingLifetimeMinutes = Math.Clamp(
            sessionOptions.Value.WaitingLifetimeMinutes,
            1,
            60);
        int maxPartySize = Math.Clamp(
            sessionOptions.Value.MaxPartySize,
            1,
            8);
        try
        {
            DungeonSession session = await repository.JoinDungeonSessionAsync(
                dungeonSessionId,
                principal.SteamId,
                request.CharacterId,
                maxPartySize,
                now,
                now.AddMinutes(waitingLifetimeMinutes),
                cancellationToken);
            return Results.Ok(ToDungeonSessionResponse(session));
        }
        catch (Exception exception) when (IsDungeonDomainException(exception))
        {
            return ToDungeonErrorResult(exception);
        }
    });

app.MapDelete(
    "/api/dungeon-sessions/{dungeonSessionId:guid}/members/{characterId:guid}",
    async (
        Guid dungeonSessionId,
        Guid characterId,
        HttpContext context,
        IGameRepository repository,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        string? ownerSteamId = await repository.GetCharacterOwnerAsync(
            characterId,
            cancellationToken);
        if (!string.Equals(
            ownerSteamId,
            principal.SteamId,
            StringComparison.Ordinal))
        {
            return ownerSteamId is null
                ? Results.NotFound()
                : Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        try
        {
            DungeonSession session = await repository.LeaveDungeonSessionAsync(
                dungeonSessionId,
                principal.SteamId,
                characterId,
                timeProvider.GetUtcNow(),
                cancellationToken);
            return Results.Ok(ToDungeonSessionResponse(session));
        }
        catch (Exception exception) when (IsDungeonDomainException(exception))
        {
            return ToDungeonErrorResult(exception);
        }
    });

app.MapPost(
    "/api/dungeon-sessions/claim",
    async (
        ClaimDungeonServerRequest request,
        HttpContext context,
        IGameRepository repository,
        AccessTokenService accessTokenService,
        IOptions<AuthOptions> authOptions,
        IOptions<DungeonSessionOptions> sessionOptions,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        if (!principal.IsAdministrator)
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        string? validationError = RequestValidators.ValidateServerId(
            request.ServerId,
            out string serverId);
        if (validationError is not null)
        {
            return Results.BadRequest(new { error = validationError });
        }

        validationError = RequestValidators.ValidateServerAddress(
            request.ServerAddress,
            out string serverAddress);
        if (validationError is not null)
        {
            return Results.BadRequest(new { error = validationError });
        }

        DateTimeOffset now = timeProvider.GetUtcNow();
        DungeonSession? session =
            await repository.ClaimNextDungeonSessionAsync(
                serverId,
                serverAddress,
                now,
                GetActiveLeaseExpiry(now, sessionOptions.Value),
                cancellationToken);
        if (session is null)
        {
            return Results.NoContent();
        }

        IssuedAccessToken gameServerToken = IssueGameServerToken(
            accessTokenService,
            authOptions.Value);
        await repository.StoreGameServerCredentialAsync(
            gameServerToken.TokenHash,
            serverId,
            session.DungeonSessionId,
            gameServerToken.ExpiresAt,
            cancellationToken);
        return Results.Ok(ToDungeonSessionResponse(session) with
        {
            GameServerAccessToken = gameServerToken.Token,
            GameServerAccessTokenExpiresAt = gameServerToken.ExpiresAt
        });
    });

app.MapPost(
    "/api/dungeon-sessions/{dungeonSessionId:guid}/activate",
    async (
        Guid dungeonSessionId,
        AssignDungeonServerRequest request,
        HttpContext context,
        IGameRepository repository,
        AccessTokenService accessTokenService,
        IOptions<AuthOptions> authOptions,
        IOptions<DungeonSessionOptions> sessionOptions,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        if (!principal.IsAdministrator)
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        string? validationError = RequestValidators.ValidateServerId(
            request.ServerId,
            out string serverId);
        if (validationError is not null)
        {
            return Results.BadRequest(new { error = validationError });
        }

        validationError = RequestValidators.ValidateServerAddress(
            request.ServerAddress,
            out string serverAddress);
        if (validationError is not null)
        {
            return Results.BadRequest(new { error = validationError });
        }

        DateTimeOffset now = timeProvider.GetUtcNow();
        DateTimeOffset leaseExpiresAt = GetActiveLeaseExpiry(
            now,
            sessionOptions.Value);
        try
        {
            DungeonSession session = await repository.AssignDungeonServerAsync(
                dungeonSessionId,
                serverId,
                serverAddress,
                now,
                leaseExpiresAt,
                cancellationToken);
            IssuedAccessToken gameServerToken = IssueGameServerToken(
                accessTokenService,
                authOptions.Value);
            await repository.StoreGameServerCredentialAsync(
                gameServerToken.TokenHash,
                serverId,
                session.DungeonSessionId,
                gameServerToken.ExpiresAt,
                cancellationToken);
            return Results.Ok(ToDungeonSessionResponse(session) with
            {
                GameServerAccessToken = gameServerToken.Token,
                GameServerAccessTokenExpiresAt = gameServerToken.ExpiresAt
            });
        }
        catch (Exception exception) when (IsDungeonDomainException(exception))
        {
            return ToDungeonErrorResult(exception);
        }
    });

app.MapPost(
    "/api/dungeon-sessions/{dungeonSessionId:guid}/start",
    async (
        Guid dungeonSessionId,
        DungeonSessionServerRequest request,
        HttpContext context,
        IGameRepository repository,
        IOptions<DungeonSessionOptions> sessionOptions,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        string? validationError = RequestValidators.ValidateServerId(
            request.ServerId,
            out string serverId);
        if (validationError is not null)
        {
            return Results.BadRequest(new { error = validationError });
        }

        if (!principal.MatchesGameServer(serverId, dungeonSessionId))
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        DateTimeOffset now = timeProvider.GetUtcNow();
        try
        {
            DungeonSession session = await repository.StartDungeonSessionAsync(
                dungeonSessionId,
                serverId,
                now,
                GetActiveLeaseExpiry(now, sessionOptions.Value),
                cancellationToken);
            return Results.Ok(ToDungeonSessionResponse(session));
        }
        catch (Exception exception) when (IsDungeonDomainException(exception))
        {
            return ToDungeonErrorResult(exception);
        }
    });

app.MapPost(
    "/api/dungeon-sessions/{dungeonSessionId:guid}/heartbeat",
    async (
        Guid dungeonSessionId,
        DungeonSessionServerRequest request,
        HttpContext context,
        IGameRepository repository,
        IOptions<DungeonSessionOptions> sessionOptions,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        string? validationError = RequestValidators.ValidateServerId(
            request.ServerId,
            out string serverId);
        if (validationError is not null)
        {
            return Results.BadRequest(new { error = validationError });
        }

        if (!principal.MatchesGameServer(serverId, dungeonSessionId))
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        DateTimeOffset now = timeProvider.GetUtcNow();
        try
        {
            DungeonSession session =
                await repository.HeartbeatDungeonSessionAsync(
                    dungeonSessionId,
                    serverId,
                    now,
                    GetActiveLeaseExpiry(now, sessionOptions.Value),
                    cancellationToken);
            return Results.Ok(ToDungeonSessionResponse(session));
        }
        catch (Exception exception) when (IsDungeonDomainException(exception))
        {
            return ToDungeonErrorResult(exception);
        }
    });

app.MapPost(
    "/api/dungeon-sessions/{dungeonSessionId:guid}/settle-rewards",
    async (
        Guid dungeonSessionId,
        SettleDungeonRewardsRequest request,
        HttpContext context,
        IGameRepository repository,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        string? validationError = RequestValidators.ValidateServerId(
            request.ServerId,
            out string serverId);
        if (validationError is not null)
        {
            return Results.BadRequest(new { error = validationError });
        }

        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        if (!principal.MatchesGameServer(serverId, dungeonSessionId))
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        string rewardVersion = request.RewardVersion?.Trim() ?? string.Empty;
        if (!EconomyRules.IsSimpleIdentifier(rewardVersion, 64))
        {
            return Results.BadRequest(new
            {
                error = "RewardVersion must be a simple identifier of at most 64 characters."
            });
        }

        if (request.Changes is null
            || request.Changes.Count > EconomyRules.MaximumChangeCount
            || request.Changes.Any(change => change is null))
        {
            return Results.BadRequest(new
            {
                error = $"Changes is required, cannot contain null entries, and cannot exceed {EconomyRules.MaximumChangeCount} entries."
            });
        }

        List<CurrencyChange> changes = [];
        HashSet<string> currencyCodes = new(StringComparer.Ordinal);
        foreach (CurrencyChangeContract contract in request.Changes!)
        {
            string currencyCode = contract.CurrencyCode?.Trim()
                ?? string.Empty;
            if (!EconomyRules.IsSimpleIdentifier(currencyCode, 64)
                || contract.Delta <= 0
                || !currencyCodes.Add(currencyCode))
            {
                return Results.BadRequest(new
                {
                    error = "Dungeon rewards require unique valid currency codes and strictly positive deltas."
                });
            }

            changes.Add(new CurrencyChange(currencyCode, contract.Delta));
        }

        IReadOnlyList<DungeonItemRewardContract?> itemRewardContracts =
            request.ItemRewards ?? [];
        if (itemRewardContracts.Count > DungeonRewardRules.MaximumItemRewardCount
            || itemRewardContracts.Any(reward => reward is null))
        {
            return Results.BadRequest(new
            {
                error = $"ItemRewards cannot contain null entries or exceed {DungeonRewardRules.MaximumItemRewardCount} entries."
            });
        }

        List<DungeonItemReward> itemRewards = [];
        long totalItemQuantity = 0;
        foreach (DungeonItemRewardContract contract in itemRewardContracts!)
        {
            string definitionType = contract.DefinitionType?.Trim()
                ?? string.Empty;
            string definitionName = contract.DefinitionName?.Trim()
                ?? string.Empty;
            if (!Enum.TryParse(
                    contract.BindState,
                    ignoreCase: true,
                    out ItemBindState bindState)
                || !Enum.IsDefined(bindState))
            {
                return Results.BadRequest(new
                {
                    error = "Every item reward requires a valid BindState."
                });
            }

            if (contract.InstanceTags?.Any(tag => tag is null) == true
                || contract.StatValues?.Any(value => value is null) == true)
            {
                return Results.BadRequest(new
                {
                    error = "Item reward tags and stat values cannot contain null entries."
                });
            }

            string[] instanceTags = (contract.InstanceTags ?? [])
                .Select(tag => tag!.Trim())
                .OrderBy(tag => tag, StringComparer.Ordinal)
                .ToArray();
            ItemStatValue[] statValues = (contract.StatValues ?? [])
                .Select(value => new ItemStatValue(
                    value!.StatTag?.Trim() ?? string.Empty,
                    value.Value))
                .OrderBy(value => value.StatTag, StringComparer.Ordinal)
                .ToArray();
            DungeonItemReward reward = new(
                definitionType,
                definitionName,
                contract.DefinitionVersion,
                contract.Quantity,
                bindState,
                contract.DurabilityCurrent,
                contract.DurabilityMaximum,
                instanceTags,
                statValues);
            if (!DungeonRewardRules.TryValidateItemReward(reward, out string error))
            {
                return Results.BadRequest(new { error });
            }

            itemRewards.Add(reward);
            totalItemQuantity += reward.Quantity;
            if (totalItemQuantity > int.MaxValue)
            {
                return Results.BadRequest(new
                {
                    error = "The total item reward quantity cannot exceed Int32.MaxValue."
                });
            }
        }

        CurrencyChange[] canonicalChanges = changes
            .OrderBy(change => change.CurrencyCode, StringComparer.Ordinal)
            .ToArray();
        string fingerprint = BuildDungeonSettlementFingerprint(
            dungeonSessionId,
            rewardVersion,
            canonicalChanges,
            itemRewards);
        try
        {
            DungeonRewardSettlement settlement =
                await repository.EnqueueDungeonRewardSettlementAsync(
                    dungeonSessionId,
                    serverId,
                    rewardVersion,
                    fingerprint,
                    canonicalChanges,
                    itemRewards,
                    timeProvider.GetUtcNow(),
                    cancellationToken);
            return Results.Json(
                new DungeonRewardSettlementResponse(
                    settlement.DungeonSessionId,
                    settlement.State.ToString(),
                    settlement.RewardVersion,
                    settlement.CharacterIds.Count,
                    settlement.UpdatedAt),
                statusCode: settlement.State is (
                    DungeonRewardSettlementState.Completed
                    or DungeonRewardSettlementState.Failed)
                    ? StatusCodes.Status200OK
                    : StatusCodes.Status202Accepted);
        }
        catch (Exception exception) when (IsDungeonDomainException(exception))
        {
            return ToDungeonErrorResult(exception);
        }
    });

app.MapPost(
    "/api/dungeon-sessions/{dungeonSessionId:guid}/finish",
    async (
        Guid dungeonSessionId,
        FinishDungeonSessionRequest request,
        HttpContext context,
        IGameRepository repository,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        string? validationError = RequestValidators.ValidateServerId(
            request.ServerId,
            out string serverId);
        if (validationError is not null)
        {
            return Results.BadRequest(new { error = validationError });
        }

        if (!Enum.TryParse(
                request.Outcome,
                ignoreCase: true,
                out DungeonSessionState outcome)
            || outcome is not (
                DungeonSessionState.Cleared
                or DungeonSessionState.Failed))
        {
            return Results.BadRequest(new
            {
                error = "Outcome must be Cleared or Failed."
            });
        }

        if (outcome == DungeonSessionState.Cleared)
        {
            return Results.Conflict(new
            {
                error = "Cleared sessions must use the settle-rewards endpoint."
            });
        }

        if (!principal.IsAdministrator
            && !principal.MatchesGameServer(serverId, dungeonSessionId))
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        try
        {
            DungeonSession session = await repository.FinishDungeonSessionAsync(
                dungeonSessionId,
                serverId,
                outcome,
                timeProvider.GetUtcNow(),
                cancellationToken);
            return Results.Ok(ToDungeonSessionResponse(session));
        }
        catch (Exception exception) when (IsDungeonDomainException(exception))
        {
            return ToDungeonErrorResult(exception);
        }
    });

app.MapPost(
    "/api/join-tickets",
    async (
        CreateJoinTicketRequest request,
        HttpContext context,
        IGameRepository repository,
        AccessTokenService accessTokenService,
        IOptions<AuthOptions> authOptions,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        DateTimeOffset now = timeProvider.GetUtcNow();
        bool isActiveMember =
            await repository.IsActiveDungeonSessionMemberAsync(
                request.DungeonSessionId,
                principal.SteamId,
                request.CharacterId,
                now,
                cancellationToken);
        if (!isActiveMember)
        {
            return Results.Conflict(new
            {
                error = "Character is not an active member of this dungeon session."
            });
        }

        int lifetimeSeconds = Math.Clamp(
            authOptions.Value.JoinTicketLifetimeSeconds,
            10,
            5 * 60);
        IssuedAccessToken joinTicket = accessTokenService.Issue(
            TimeSpan.FromSeconds(lifetimeSeconds));
        await repository.StoreJoinTicketAsync(
            joinTicket.TokenHash,
            principal.SteamId,
            request.CharacterId,
            request.DungeonSessionId,
            joinTicket.ExpiresAt,
            cancellationToken);

        return Results.Ok(new CreateJoinTicketResponse(
            request.DungeonSessionId,
            request.CharacterId,
            joinTicket.Token,
            joinTicket.ExpiresAt));
    });

app.MapPost(
    "/api/join-tickets/consume",
    async (
        ConsumeJoinTicketRequest request,
        HttpContext context,
        IGameRepository repository,
        AccessTokenService accessTokenService,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        if (!principal.IsGameServer)
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        if (string.IsNullOrWhiteSpace(request.JoinTicket)
            || RequestValidators.ValidateServerId(
                request.ServerId,
                out string serverId) is not null)
        {
            return Results.Unauthorized();
        }

        if (!principal.DungeonSessionId.HasValue
            || !principal.MatchesGameServer(
                serverId,
                principal.DungeonSessionId.Value))
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        string joinTicket = request.JoinTicket.Trim();
        if (joinTicket.Length is < 32 or > 256)
        {
            return Results.Unauthorized();
        }

        ConsumedJoinTicket? consumedTicket =
            await repository.ConsumeJoinTicketAsync(
                accessTokenService.Hash(joinTicket),
                serverId,
                principal.DungeonSessionId.Value,
                timeProvider.GetUtcNow(),
                cancellationToken);
        return consumedTicket is null
            ? Results.Unauthorized()
            : Results.Ok(new ConsumeJoinTicketResponse(
                consumedTicket.DungeonSessionId,
                consumedTicket.CharacterId,
                consumedTicket.SteamId));
    });

app.MapGet(
    "/api/loadInventory",
    async (
        Guid characterId,
        HttpContext context,
        IGameRepository repository,
        CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        string? ownerSteamId = await repository.GetCharacterOwnerAsync(
            characterId,
            cancellationToken);
        if (ownerSteamId is null)
        {
            return Results.NotFound();
        }

        if (!principal.IsGameServer
            && !string.Equals(principal.SteamId, ownerSteamId, StringComparison.Ordinal))
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        IReadOnlyList<InventoryItem> inventory = await repository.LoadInventoryAsync(
            characterId,
            cancellationToken);
        return Results.Ok(new LoadInventoryResponse(
            inventory.Select(ToInventoryContract).ToArray()));
    });

app.MapPost(
    "/api/saveInventory",
    async (
        SaveInventoryRequest request,
        HttpContext context,
        IGameRepository repository,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        if (!principal.IsGameServer)
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        string? ownerSteamId = await repository.GetCharacterOwnerAsync(
            request.CharacterId,
            cancellationToken);
        if (ownerSteamId is null)
        {
            return Results.NotFound();
        }

        string? validationError = RequestValidators.ValidateInventory(request.Inventory);
        if (validationError is not null)
        {
            return Results.BadRequest(new { error = validationError });
        }

        InventoryItem[] inventory = request.Inventory
            .Select(item => new InventoryItem(
                item.ItemId,
                item.Quantity,
                item.SlotIndex,
                item.Category,
                item.InstanceId))
            .ToArray();
        await repository.SaveInventoryAsync(
            request.CharacterId,
            inventory,
            timeProvider.GetUtcNow(),
            cancellationToken);
        return Results.NoContent();
    });

app.MapItemApi();
app.MapEconomyApi();

await app.RunAsync();

static CharacterResponse ToCharacterResponse(GameCharacter character) => new(
    character.CharacterId,
    character.RosterId,
    character.Name,
    character.CreatedAt);

static RosterResponse ToRosterResponse(Roster roster) => new(
    roster.RosterId,
    roster.WorldId,
    roster.CreatedAt);

static DungeonSessionResponse ToDungeonSessionResponse(
    DungeonSession session) => new(
        session.DungeonSessionId,
        session.DungeonId,
        session.Difficulty,
        session.State.ToString(),
        session.ServerId,
        session.ServerAddress,
        session.ExpiresAt,
        session.Members.Select(member => new DungeonSessionMemberResponse(
            member.CharacterId,
            member.JoinedAt,
            member.LeaseExpiresAt)).ToArray());

static InventoryItemContract ToInventoryContract(InventoryItem item) => new(
    item.ItemId,
    item.Quantity,
    item.SlotIndex,
    item.Category,
    item.InstanceId);

static DateTimeOffset GetActiveLeaseExpiry(
    DateTimeOffset now,
    DungeonSessionOptions options)
{
    int activeLeaseSeconds = Math.Clamp(
        options.ActiveLeaseSeconds,
        30,
        10 * 60);
    return now.AddSeconds(activeLeaseSeconds);
}

static IssuedAccessToken IssueGameServerToken(
    AccessTokenService accessTokenService,
    AuthOptions options)
{
    int lifetimeMinutes = Math.Clamp(
        options.GameServerTokenLifetimeMinutes,
        5,
        24 * 60);
    return accessTokenService.Issue(TimeSpan.FromMinutes(lifetimeMinutes));
}

static string BuildDungeonSettlementFingerprint(
    Guid dungeonSessionId,
    string rewardVersion,
    IReadOnlyList<CurrencyChange> changes,
    IReadOnlyList<DungeonItemReward> itemRewards)
{
    string canonical = JsonSerializer.Serialize(new
    {
        kind = "dungeon_reward",
        dungeonSessionId,
        rewardVersion,
        changes,
        itemRewards
    });
    return Convert.ToHexString(
            SHA256.HashData(Encoding.UTF8.GetBytes(canonical)))
        .ToLowerInvariant();
}

static bool IsDungeonDomainException(Exception exception) =>
    exception is CharacterSessionConflictException
        or DungeonSessionNotFoundException
        or DungeonSessionNotJoinableException
        or DungeonSessionFullException
        or DungeonSessionServerMismatchException
        or DungeonSessionStateConflictException
        or DungeonSessionMembershipException
        or DungeonRewardSettlementConflictException;

static IResult ToDungeonErrorResult(Exception exception) => exception switch
{
    DungeonSessionNotFoundException =>
        Results.NotFound(new { error = exception.Message }),
    DungeonSessionMembershipException =>
        Results.Json(
            new { error = exception.Message },
            statusCode: StatusCodes.Status403Forbidden),
    _ => Results.Conflict(new { error = exception.Message })
};

static void ApplyEnvironmentOverrides(ConfigurationManager configuration)
{
    SetIfPresent(
        "PROJECT_RPG_BACKEND_ADMIN_TOKEN",
        value => configuration["Auth:AdminToken"] = value);
    SetIfPresent(
        "STEAM_WEB_API_PUBLISHER_KEY",
        value => configuration["Steam:PublisherKey"] = value);
    SetIfPresent(
        "STEAM_APP_ID",
        value => configuration["Steam:AppId"] = value);
    SetIfPresent(
        "STEAM_TICKET_IDENTITY",
        value => configuration["Steam:TicketIdentity"] = value);
    SetIfPresent(
        "POSTGRES_CONNECTION_STRING",
        value =>
        {
            configuration["ConnectionStrings:Postgres"] = value;
            configuration["Storage:Provider"] = "Postgres";
        });

    static void SetIfPresent(string name, Action<string> apply)
    {
        string? value = Environment.GetEnvironmentVariable(name);
        if (!string.IsNullOrWhiteSpace(value))
        {
            apply(value);
        }
    }
}

static void ValidateAuthConfiguration(AuthOptions options)
{
    const int minimumTokenLength = 32;
    if (!string.IsNullOrWhiteSpace(options.AdminToken)
        && options.AdminToken.Trim().Length < minimumTokenLength)
    {
        throw new InvalidOperationException(
            $"Auth:AdminToken must contain at least {minimumTokenLength} characters.");
    }

}
