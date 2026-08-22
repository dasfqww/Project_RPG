using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Api;

public static class EconomyApiEndpoints
{
    public static IEndpointRouteBuilder MapEconomyApi(
        this IEndpointRouteBuilder endpoints)
    {
        endpoints.MapGet(
            "/api/economy/currency-definitions",
            async (
                IEconomyRepository repository,
                CancellationToken cancellationToken) =>
            {
                IReadOnlyList<CurrencyDefinition> definitions =
                    await repository.GetDefinitionsAsync(cancellationToken);
                return Results.Ok(new CurrencyDefinitionsResponse(
                    definitions
                        .Select(EconomyApiContractMapper.ToContract)
                        .ToArray()));
            });

        endpoints.MapPut(
            "/api/economy/currency-definitions/{currencyCode}",
            async (
                string currencyCode,
                CurrencyDefinitionContract contract,
                HttpContext context,
                IEconomyRepository repository,
                TimeProvider timeProvider,
                CancellationToken cancellationToken) =>
            {
                if (!context.GetProjectRpgPrincipal().IsAdministrator)
                {
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                }

                if (!string.Equals(
                    currencyCode,
                    contract.CurrencyCode?.Trim(),
                    StringComparison.Ordinal))
                {
                    return Results.BadRequest(new
                    {
                        error = "Route currencyCode must match the request body."
                    });
                }

                if (!EconomyApiContractMapper.TryToDomain(
                    contract,
                    out CurrencyDefinition definition,
                    out string error))
                {
                    return Results.BadRequest(new { error });
                }

                try
                {
                    await repository.UpsertDefinitionAsync(
                        definition,
                        timeProvider.GetUtcNow(),
                        cancellationToken);
                    return Results.Ok(
                        EconomyApiContractMapper.ToContract(definition));
                }
                catch (InvalidOperationException exception)
                {
                    return Results.Conflict(new { error = exception.Message });
                }
            });

        endpoints.MapGet(
            "/api/economy/wallets/{characterId:guid}",
            async (
                Guid characterId,
                Guid? dungeonSessionId,
                HttpContext context,
                IGameRepository gameRepository,
                IEconomyRepository economyRepository,
                TimeProvider timeProvider,
                CancellationToken cancellationToken) =>
            {
                CharacterEconomyContext? economyContext =
                    await gameRepository.GetCharacterEconomyContextAsync(
                        characterId,
                        cancellationToken);
                if (economyContext is null)
                {
                    return Results.NotFound();
                }

                AuthenticatedPrincipal principal =
                    context.GetProjectRpgPrincipal();
                bool isOwner = principal.Kind == PrincipalKind.Player
                    && string.Equals(
                        principal.SteamId,
                        economyContext.SteamId,
                        StringComparison.Ordinal);
                bool isAuthorizedServer = principal.IsGameServer
                    && dungeonSessionId.HasValue
                    && principal.MatchesGameServer(
                        principal.ServerId!,
                        dungeonSessionId.Value)
                    && await gameRepository
                        .IsAuthorizedGameServerSessionMemberAsync(
                            dungeonSessionId.Value,
                            principal.ServerId!,
                            economyContext.SteamId,
                            characterId,
                            timeProvider.GetUtcNow(),
                            cancellationToken);
                if (!isOwner
                    && !isAuthorizedServer
                    && !principal.IsAdministrator)
                {
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                }

                CurrencyWallet wallet = await economyRepository.LoadWalletAsync(
                    economyContext,
                    cancellationToken);
                return Results.Ok(EconomyApiContractMapper.ToContract(wallet));
            });

        endpoints.MapGet(
            "/api/economy/transactions/{requestId:guid}",
            async (
                Guid requestId,
                Guid dungeonSessionId,
                HttpContext context,
                IGameRepository gameRepository,
                IEconomyRepository economyRepository,
                TimeProvider timeProvider,
                CancellationToken cancellationToken) =>
            {
                AuthenticatedPrincipal principal =
                    context.GetProjectRpgPrincipal();
                if (!principal.IsGameServer && !principal.IsAdministrator)
                {
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                }

                CurrencyTransactionResult? result =
                    await economyRepository.TryGetTransactionResultAsync(
                        requestId,
                        cancellationToken);
                if (result is null)
                {
                    return Results.NotFound();
                }

                if (principal.IsGameServer)
                {
                    CharacterEconomyContext? economyContext =
                        await gameRepository.GetCharacterEconomyContextAsync(
                            result.CharacterId,
                            cancellationToken);
                    bool isAuthorized = economyContext is not null
                        && principal.MatchesGameServer(
                            principal.ServerId!,
                            dungeonSessionId)
                        && await gameRepository
                            .IsAuthorizedGameServerSessionMemberAsync(
                                dungeonSessionId,
                                principal.ServerId!,
                                economyContext.SteamId,
                                result.CharacterId,
                                timeProvider.GetUtcNow(),
                                cancellationToken);
                    if (!isAuthorized)
                    {
                        return Results.StatusCode(
                            StatusCodes.Status403Forbidden);
                    }
                }

                return Results.Ok(EconomyApiContractMapper.ToContract(result));
            });

        endpoints.MapPost(
            "/api/economy/transactions/commit",
            async (
                CurrencyTransactionRequestContract contract,
                HttpContext context,
                CurrencyTransactionService transactionService,
                CancellationToken cancellationToken) =>
            {
                AuthenticatedPrincipal principal =
                    context.GetProjectRpgPrincipal();
                if (!principal.IsGameServer)
                {
                    return Results.StatusCode(StatusCodes.Status403Forbidden);
                }

                if (!EconomyApiContractMapper.TryToDomain(
                    contract,
                    out CurrencyTransactionRequest request,
                    out string error))
                {
                    return Results.BadRequest(new { error });
                }

                if (!principal.MatchesGameServer(
                    principal.ServerId!,
                    request.DungeonSessionId))
                {
                    return Results.StatusCode(
                        StatusCodes.Status403Forbidden);
                }

                CurrencyTransactionResult result =
                    await transactionService.CommitAsync(
                        request,
                        principal.ServerId!,
                        cancellationToken);
                return Results.Json(
                    EconomyApiContractMapper.ToContract(result),
                    statusCode: ToStatusCode(result.Status));
            });

        return endpoints;
    }

    private static int ToStatusCode(CurrencyTransactionStatus status)
    {
        return status switch
        {
            CurrencyTransactionStatus.Committed
                or CurrencyTransactionStatus.AlreadyCommitted =>
                StatusCodes.Status200OK,
            CurrencyTransactionStatus.InvalidRequest =>
                StatusCodes.Status400BadRequest,
            CurrencyTransactionStatus.CharacterNotFound
                or CurrencyTransactionStatus.DefinitionNotFound =>
                StatusCodes.Status404NotFound,
            CurrencyTransactionStatus.SessionNotAuthorized =>
                StatusCodes.Status403Forbidden,
            CurrencyTransactionStatus.IdempotencyConflict
                or CurrencyTransactionStatus.CurrencyDisabled
                or CurrencyTransactionStatus.InsufficientBalance
                or CurrencyTransactionStatus.BalanceLimitExceeded =>
                StatusCodes.Status409Conflict,
            _ => StatusCodes.Status500InternalServerError
        };
    }
}
