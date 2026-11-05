using System.Security.Cryptography;
using ProjectRpg.Backend.Operations.Hosting;
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

const string migrateSchemaArgument = "--migrate-schema";
const string validateNarrativeArgument = "--validate-narrative-catalog";
int migrateSchemaCount = args.Count(argument => argument == migrateSchemaArgument);
int validateNarrativeCount = args.Count(argument => argument == validateNarrativeArgument);
if (migrateSchemaCount > 1)
    throw new InvalidOperationException("Specify --migrate-schema only once.");
if (validateNarrativeCount > 1)
    throw new InvalidOperationException("Specify --validate-narrative-catalog only once.");
if (migrateSchemaCount != 0 && validateNarrativeCount != 0)
    throw new InvalidOperationException("Choose one startup mode: migration or narrative validation.");
WebApplicationBuilder builder = WebApplication.CreateBuilder(
    args.Where(argument => argument != migrateSchemaArgument && argument != validateNarrativeArgument).ToArray());
ApplyEnvironmentOverrides(builder.Configuration);
// Validate the selected immutable content before migration, listeners or workers start.
var narrativeCatalog = ProjectRpg.Backend.Narrative.NarrativeCatalogLoader.LoadConfigured(
    builder.Configuration, builder.Environment);
if (validateNarrativeCount == 1)
{
    Console.WriteLine($"Narrative catalog valid: version {narrativeCatalog.Version}, "
        + $"quests {narrativeCatalog.Quests.Count}, chapters {narrativeCatalog.Chapters.Count}, "
        + $"dialogues {narrativeCatalog.Dialogues.Count}. No host or database started.");
    return;
}
if (migrateSchemaCount == 1)
{
    // A deployment job, not a web host: no listeners or settlement workers start.
    if (!string.Equals(builder.Configuration["Storage:Provider"], "Postgres",
        StringComparison.OrdinalIgnoreCase))
        throw new InvalidOperationException("--migrate-schema requires PostgreSQL storage.");
    string? migrationConnection = builder.Configuration.GetConnectionString("Postgres");
    if (string.IsNullOrWhiteSpace(migrationConnection))
        throw new InvalidOperationException("--migrate-schema requires ConnectionStrings:Postgres.");
    await using NpgsqlDataSource migrationSource = NpgsqlDataSource.Create(migrationConnection);
    int applied = await PostgresSchemaMigrations.ApplyAsync(migrationSource,
        await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, CancellationToken.None),
        CancellationToken.None);
    Console.WriteLine($"PostgreSQL migrations ready; applied {applied} version(s). No web host started.");
    return;
}
ValidateAuthConfiguration(
    builder.Configuration.GetSection(AuthOptions.SectionName)
        .Get<AuthOptions>() ?? new AuthOptions());
// Validate before connecting/migrating storage. No empty/permissive fallback.
var dungeonContentCatalog = DungeonDevelopmentProfiles.Resolve(
    builder.Configuration, builder.Environment, AppContext.BaseDirectory);
builder.Services.AddSingleton(dungeonContentCatalog);
builder.Services.AddSingleton(new ProjectRpg.Backend.Narrative.DungeonStoryAdmission(dungeonContentCatalog, narrativeCatalog));

builder.Services.Configure<AuthOptions>(
    builder.Configuration.GetSection(AuthOptions.SectionName));
builder.Services.Configure<SteamOptions>(
    builder.Configuration.GetSection(SteamOptions.SectionName));
builder.Services.Configure<StorageOptions>(
    builder.Configuration.GetSection(StorageOptions.SectionName));
builder.Services.Configure<ApiRateLimitOptions>(
    builder.Configuration.GetSection(ApiRateLimitOptions.SectionName));
builder.Services.Configure<DungeonSessionOptions>(
    builder.Configuration.GetSection(DungeonSessionOptions.SectionName));
builder.Services.Configure<DungeonSettlementOptions>(
    builder.Configuration.GetSection(DungeonSettlementOptions.SectionName));
builder.Services.Configure<SecurityTelemetryOptions>(
    builder.Configuration.GetSection(SecurityTelemetryOptions.SectionName));
builder.Services.AddSingleton<TimeProvider>(TimeProvider.System);
builder.Services.AddSingleton<AccessTokenService>();
builder.Services.AddSingleton<ApiRateLimiter>();
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
    builder.Services.AddSingleton<
        IStorageReadinessProbe,
        PostgresStorageReadinessProbe>();
    builder.Services.AddSingleton<IGameRepository, PostgresGameRepository>();
    builder.Services.AddSingleton<PostgresItemRepository>();
    builder.Services.AddSingleton<IItemRepository>(provider =>
        provider.GetRequiredService<PostgresItemRepository>());
    builder.Services.AddSingleton<IDungeonPickupCommitRepository>(provider =>
        new PostgresDungeonPickupCommitRepository(
            (PostgresGameRepository)provider.GetRequiredService<IGameRepository>(),
            provider.GetRequiredService<PostgresItemRepository>(), provider.GetRequiredService<TimeProvider>()));
    builder.Services.AddSingleton<PostgresEconomyRepository>();
    builder.Services.AddSingleton<IEconomyRepository>(provider =>
        provider.GetRequiredService<PostgresEconomyRepository>());
    builder.Services.AddSingleton<ITradeReservationRepository, PostgresTradeReservationRepository>();
    builder.Services.AddSingleton<
        IDungeonRewardCommitRepository,
        PostgresDungeonRewardCommitRepository>();
    builder.Services.AddSingleton<
        ISecurityTelemetryRepository,
        PostgresSecurityTelemetryRepository>();
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

    builder.Services.AddSingleton<
        IStorageReadinessProbe,
        MemoryStorageReadinessProbe>();
    builder.Services.AddSingleton<IGameRepository>(provider =>
        new InMemoryGameRepository(
            provider.GetRequiredService<InMemoryTransactionGate>(),
            provider.GetRequiredService<InMemoryItemRepository>()));
    builder.Services.AddSingleton<InMemoryTransactionGate>();
    builder.Services.AddSingleton<InMemoryItemRepository>();
    builder.Services.AddSingleton<IItemRepository>(provider =>
        provider.GetRequiredService<InMemoryItemRepository>());
    builder.Services.AddSingleton<IDungeonPickupCommitRepository>(provider =>
        new InMemoryDungeonPickupCommitRepository(
            (InMemoryGameRepository)provider.GetRequiredService<IGameRepository>(),
            provider.GetRequiredService<InMemoryItemRepository>(), provider.GetRequiredService<TimeProvider>()));
    builder.Services.AddSingleton<InMemoryEconomyRepository>();
    builder.Services.AddSingleton<IEconomyRepository>(provider =>
        provider.GetRequiredService<InMemoryEconomyRepository>());
    builder.Services.AddSingleton<ITradeReservationRepository, InMemoryTradeReservationRepository>();
    builder.Services.AddSingleton<
        IDungeonRewardCommitRepository,
        InMemoryDungeonRewardCommitRepository>();
    builder.Services.AddSingleton<
        ISecurityTelemetryRepository,
        InMemorySecurityTelemetryRepository>();
}
else
{
    throw new InvalidOperationException(
        $"Unsupported storage provider '{storageProvider}'. Use Memory or Postgres.");
}

builder.Services.AddGameRepositoryContracts();
// No production definitions are tradable until an explicitly authored allowlist is connected.
builder.Services.AddSingleton(new ProjectRpg.Backend.Domain.Trading.TradeItemPolicy([]));
builder.Services.AddSingleton<ItemTransactionService>();
builder.Services.AddSingleton(narrativeCatalog);
builder.Services.AddSingleton<INarrativeRepository>(provider =>
    (INarrativeRepository)provider.GetRequiredService<IGameRepository>());
builder.Services.AddSingleton<ProjectRpg.Backend.Narrative.QuestManager>();
builder.Services.AddSingleton<ProjectRpg.Backend.Narrative.ScenarioManager>();
builder.Services.AddSingleton<ProjectRpg.Backend.Narrative.DialogueManager>();
builder.Services.AddSingleton<ProjectRpg.Backend.Narrative.NarrativeCoordinator>();
builder.Services.AddSingleton<ICharacterProgressionRepository>(provider =>
    (ICharacterProgressionRepository)provider.GetRequiredService<IGameRepository>());
builder.Services.AddSingleton<IEquipmentProgressionRepository>(provider =>
    (IEquipmentProgressionRepository)provider.GetRequiredService<IGameRepository>());
builder.Services.AddSingleton<DungeonPickupCommitService>();
builder.Services.AddSingleton<IDungeonPickupIntentRepository>(provider =>
    (IDungeonPickupIntentRepository)provider.GetRequiredService<IGameRepository>());
builder.Services.AddSingleton<ItemOwnerAccessService>();
builder.Services.AddSingleton<CurrencyTransactionService>();
builder.Services.AddHostedService<DungeonRewardSettlementWorker>();

builder.Services.AddOperations(builder.Configuration, builder.Environment);
WebApplication app = builder.Build();

ValidateApiRateLimitConfiguration(
    app.Services.GetRequiredService<IOptions<ApiRateLimitOptions>>().Value);
if (string.Equals(storageProvider, "Postgres", StringComparison.OrdinalIgnoreCase)
    && !app.Environment.IsDevelopment() && !app.Environment.IsEnvironment("Testing"))
{
    await PostgresSchemaMigrations.ValidateAsync(app.Services.GetRequiredService<NpgsqlDataSource>(),
        await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, CancellationToken.None),
        CancellationToken.None);
    app.Logger.LogInformation("PostgreSQL migration history matches this release (no startup DDL).");
}
else
{
    // Preserve the existing local development / test schema-upgrade contract.
    await app.Services.GetRequiredService<IGameRepository>()
        .EnsureCreatedAsync(CancellationToken.None);
}

app.UseOperations();
app.MapOperations();
app.UseMiddleware<ApiIngressRateLimitMiddleware>();
app.UseMiddleware<ApiAuthenticationMiddleware>();
app.UseMiddleware<AuthenticatedApiRateLimitMiddleware>();

app.MapHealthApi();

app.MapPost(
    "/api/auth/steam-ticket",
    async (
        SteamTicketRequest request,
        ISteamTicketValidator steamTicketValidator,
        IAuthenticationRepository repository,
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
                identity.SteamId,
                accessToken.IssuedAt));
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
    async (HttpContext context, ICharacterRepository repository, ICharacterProgressionRepository progression,
        CancellationToken cancellationToken) =>
    {
        context.Response.Headers.CacheControl = "no-store";
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        IReadOnlyList<GameCharacter> characters = await repository.GetCharactersAsync(
            principal.SteamId,
            cancellationToken);
        IReadOnlyDictionary<Guid, CharacterProgressionSummary> summaries = await progression.GetStoredProgressionSummariesAsync(
            principal.SteamId, cancellationToken);
        return Results.Ok(characters.Select(character => ToCharacterResponse(character,
            summaries.GetValueOrDefault(character.CharacterId, CharacterProgressionSummary.Unavailable))));
    });

app.MapGet(
    "/api/rosters",
    async (
        HttpContext context,
        ICharacterRepository repository,
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

app.MapGet("/api/character-creation-options", (HttpContext context) =>
{
    context.Response.Headers.CacheControl = "no-store";
    AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
    return principal.Kind == PrincipalKind.Player && principal.SteamId is not null
        ? Results.Ok(CharacterCreationCatalog.Options) : Results.StatusCode(StatusCodes.Status403Forbidden);
});

app.MapPost(
    "/api/characters",
    async (
        CreateCharacterRequest request,
        HttpContext context,
        ICharacterRepository repository,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        if (principal.Kind != PrincipalKind.Player || principal.SteamId is null)
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }

        if (!CharacterCreationCatalog.IsSupported(request.ClassId))
        {
            return Results.BadRequest(new { error = "unsupported_character_class" });
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
        DungeonContentCatalog contentCatalog,
        ProjectRpg.Backend.Narrative.DungeonStoryAdmission storyAdmission,
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

        // Do not let the older syntax validator's Trim silently alias an ID.
        validationError = contentCatalog.GetSelectionError(request.DungeonId, request.Difficulty);
        if (validationError is not null)
            return Results.BadRequest(new { error = validationError });

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
            string? storyError = await storyAdmission.GetErrorAsync(repository, request.CharacterId,
                principal.SteamId, dungeonId, cancellationToken);
            if (storyError is not null) return Results.Conflict(new { error = storyError });
            if (!await CharacterProgressionApiEndpoints.CanEnterAsync(repository, request.CharacterId,
                    dungeonId, difficulty, now, cancellationToken))
                return Results.Conflict(new { error = "progression_prerequisite_not_met" });
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
        context.Response.Headers.CacheControl = "no-store";
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        // A server label is not a globally unique dungeon identity. Reject a
        // foreign or unscoped server credential before querying its session.
        if (principal.IsGameServer && principal.DungeonSessionId != dungeonSessionId)
            return Results.StatusCode(StatusCodes.Status403Forbidden);
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
            && principal.MatchesGameServer(session.ServerId, dungeonSessionId);
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
        ProjectRpg.Backend.Narrative.DungeonStoryAdmission storyAdmission,
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
            DungeonSession? targetSession = await repository.GetDungeonSessionAsync(dungeonSessionId, now, cancellationToken);
            if (targetSession is not null)
            {
                string? storyError = await storyAdmission.GetErrorAsync(repository, request.CharacterId,
                    principal.SteamId, targetSession.DungeonId, cancellationToken);
                if (storyError is not null) return Results.Conflict(new { error = storyError });
            }
            if (targetSession is not null && !await CharacterProgressionApiEndpoints.CanEnterAsync(repository,
                    request.CharacterId, targetSession.DungeonId, targetSession.Difficulty, now, cancellationToken))
                return Results.Conflict(new { error = "progression_prerequisite_not_met" });
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
        DungeonContentCatalog contentCatalog,
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
                sessionOptions.Value.GetServerStartupLeaseExpiry(now),
                cancellationToken,
                contentCatalog.AllowedSelections, requireDeparture: true,
                minimumPartySize: Math.Clamp(sessionOptions.Value.MinimumDeparturePartySize, 1,
                    Math.Clamp(sessionOptions.Value.MaxPartySize, 1, 8)));
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
            GameServerAccessTokenExpiresAt = gameServerToken.ExpiresAt,
            ServerMapPackage = contentCatalog.GetMapPackage(session.DungeonId, session.Difficulty)
        });
    });

app.MapPost(
    "/api/dungeon-sessions/{dungeonSessionId:guid}/activate",
    async (
        Guid dungeonSessionId,
        AssignDungeonServerRequest request,
        HttpContext context,
        IGameRepository repository,
        DungeonContentCatalog contentCatalog,
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
        DateTimeOffset leaseExpiresAt = sessionOptions.Value.GetServerStartupLeaseExpiry(now);
        // ID/difficulty are immutable repository fields. Removed/legacy content
        // remains readable/recoverable, but cannot acquire a new assignment.
        DungeonSession? candidate = await repository.GetDungeonSessionAsync(
            dungeonSessionId, now, cancellationToken);
        if (candidate is null)
            return Results.NotFound(new { error = "dungeon_session_not_found" });
        if (contentCatalog.GetSelectionError(candidate.DungeonId, candidate.Difficulty) is not null)
            return Results.Conflict(new { error = "dungeon_content_not_available" });
        try
        {
            DungeonSession session = await repository.AssignDungeonServerAsync(
                dungeonSessionId,
                serverId,
                serverAddress,
                now,
                leaseExpiresAt,
                cancellationToken, requireDeparture: true,
                minimumPartySize: Math.Clamp(sessionOptions.Value.MinimumDeparturePartySize, 1,
                    Math.Clamp(sessionOptions.Value.MaxPartySize, 1, 8)));
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
                GameServerAccessTokenExpiresAt = gameServerToken.ExpiresAt,
                ServerMapPackage = contentCatalog.GetMapPackage(session.DungeonId, session.Difficulty)
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
            FirstSliceEquipmentProgressionState? equipmentProgression = null;
            if (contract.EquipmentProgression is { } growth)
            {
                if (growth.GrowthBandId is null || growth.RuleVersion is not int ruleVersion || growth.Level is not int level)
                    return Results.BadRequest(new { error = "EquipmentProgression requires GrowthBandId, RuleVersion and Level." });
                equipmentProgression = new(growth.GrowthBandId, ruleVersion, level);
            }
            DungeonItemReward reward = new(
                definitionType,
                definitionName,
                contract.DefinitionVersion,
                contract.Quantity,
                bindState,
                contract.DurabilityCurrent,
                contract.DurabilityMaximum,
                instanceTags,
                statValues,
                contract.MaxStackSize,
                equipmentProgression,
                contract.PersonalLootWeight);
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

        if (!DungeonPersonalLootSelection.IsValidPool(itemRewards))
            return Results.BadRequest(new { error = "Personal loot weights must be positive and sum to at most 10000." });

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

app.MapPlayerLogoutApi();
app.MapJoinTicketApi();

app.MapGet(
    "/api/loadInventory",
    async (
        Guid characterId,
        HttpContext context,
        IGameRepository repository,
        TimeProvider timeProvider,
        CancellationToken cancellationToken) =>
    {
        context.Response.Headers.CacheControl = "no-store";
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        string? ownerSteamId = await repository.GetCharacterOwnerAsync(
            characterId,
            cancellationToken);
        if (ownerSteamId is null)
        {
            return Results.NotFound();
        }

        bool allowed = principal.Kind == PrincipalKind.Player
            && string.Equals(principal.SteamId, ownerSteamId, StringComparison.Ordinal);
        if (principal.IsGameServer && principal.DungeonSessionId is Guid sessionId
            && !string.IsNullOrWhiteSpace(principal.ServerId))
            allowed = await repository.IsAuthorizedGameServerSessionMemberAsync(sessionId, principal.ServerId,
                ownerSteamId, characterId, timeProvider.GetUtcNow(), cancellationToken);
        if (!allowed)
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
        context.Response.Headers.CacheControl = "no-store";
        AuthenticatedPrincipal principal = context.GetProjectRpgPrincipal();
        if (!principal.IsGameServer || principal.DungeonSessionId is not Guid sessionId
            || string.IsNullOrWhiteSpace(principal.ServerId))
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
        try
        {
            await repository.SaveInventoryAsync(request.CharacterId, inventory, timeProvider.GetUtcNow(),
                cancellationToken, new GameServerCredential(principal.ServerId, sessionId));
        }
        catch (DungeonSessionMembershipException)
        {
            return Results.StatusCode(StatusCodes.Status403Forbidden);
        }
        return Results.NoContent();
    });

app.MapItemApi();
app.MapDungeonPickupIntentApi();
app.MapEconomyApi();
app.MapSecurityTelemetryApi();
app.MapDungeonRewardSnapshotApi();
app.MapCharacterProgressionApi();
app.MapNarrativeApi();
app.MapEquipmentProgressionApi();
app.MapDungeonAbandonmentApi();
app.MapDungeonMembershipSnapshotApi();
app.MapDungeonAdmissionApi();
app.MapDungeonDepartureApi();

await app.RunAsync();

static CharacterResponse ToCharacterResponse(GameCharacter character, CharacterProgressionSummary? progression = null) => new(
    character.CharacterId,
    character.RosterId,
    character.Name,
    character.CreatedAt,
    progression);

static RosterResponse ToRosterResponse(Roster roster) => new(
    roster.RosterId,
    roster.WorldId,
    roster.CreatedAt);

static DungeonSessionResponse ToDungeonSessionResponse(
    DungeonSession session) => DungeonSessionResponses.From(session);

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
        or DungeonRewardSettlementConflictException
        or DungeonDepartureConflictException;

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

static void ValidateApiRateLimitConfiguration(ApiRateLimitOptions options)
{
    if (!options.Enabled)
    {
        return;
    }
    if (options.WindowSeconds is < 1 or > 3600)
    {
        throw new InvalidOperationException(
            "RateLimits:WindowSeconds must be between 1 and 3600.");
    }
    ValidatePermitLimit(
        options.AuthenticationPermitLimit,
        "AuthenticationPermitLimit");
    ValidatePermitLimit(options.IngressPermitLimit, "IngressPermitLimit");
    ValidatePermitLimit(options.PlayerPermitLimit, "PlayerPermitLimit");
    ValidatePermitLimit(
        options.GameServerPermitLimit,
        "GameServerPermitLimit");
    ValidatePermitLimit(
        options.AdministratorPermitLimit,
        "AdministratorPermitLimit");

    static void ValidatePermitLimit(int value, string name)
    {
        if (value is < 1 or > 1_000_000)
        {
            throw new InvalidOperationException(
                $"RateLimits:{name} must be between 1 and 1000000.");
        }
    }
}
