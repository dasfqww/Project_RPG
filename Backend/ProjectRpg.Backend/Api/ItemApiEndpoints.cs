using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Api;

public static class ItemApiEndpoints
{
    public static IEndpointRouteBuilder MapItemApi(
        this IEndpointRouteBuilder endpoints)
    {
        endpoints.MapGet(
            "/api/items",
            async (
                string ownerType,
                string ownerId,
                bool includeTerminal,
                int? limit,
                HttpContext context,
                ItemOwnerAccessService accessService,
                IItemRepository itemRepository,
                CancellationToken cancellationToken) =>
            {
                if (!ItemApiContractMapper.TryParseOwner(
                    ownerType,
                    ownerId,
                    out ItemOwnerRef owner,
                    out string error))
                {
                    return Results.BadRequest(new { error });
                }

                AuthenticatedPrincipal principal =
                    context.GetProjectRpgPrincipal();
                if (includeTerminal && !principal.IsGameServer)
                {
                    return Results.StatusCode(
                        StatusCodes.Status403Forbidden);
                }

                if (!await accessService.CanReadAsync(
                    principal,
                    owner,
                    cancellationToken))
                {
                    return Results.StatusCode(
                        StatusCodes.Status403Forbidden);
                }

                int pageSize = Math.Clamp(limit ?? 200, 1, 500);
                IReadOnlyList<ItemRecord> records =
                    await itemRepository.FindByOwnerAsync(
                        owner,
                        includeTerminal,
                        pageSize,
                        cancellationToken);
                return Results.Ok(new LoadItemsResponse(
                    records
                        .Select(ItemApiContractMapper.ToContract)
                        .ToArray()));
            });

        endpoints.MapGet(
            "/api/items/{itemId:guid}",
            async (
                Guid itemId,
                HttpContext context,
                ItemOwnerAccessService accessService,
                IItemRepository itemRepository,
                CancellationToken cancellationToken) =>
            {
                ItemRecord? record = await itemRepository.FindAsync(
                    itemId,
                    cancellationToken);
                if (record is null)
                {
                    return Results.NotFound();
                }

                if (!await accessService.CanReadAsync(
                    context.GetProjectRpgPrincipal(),
                    record.Owner,
                    cancellationToken))
                {
                    return Results.StatusCode(
                        StatusCodes.Status403Forbidden);
                }

                return Results.Ok(
                    ItemApiContractMapper.ToContract(record));
            });

        endpoints.MapGet(
            "/api/item-transactions/{requestId:guid}",
            async (
                Guid requestId,
                HttpContext context,
                IItemRepository itemRepository,
                CancellationToken cancellationToken) =>
            {
                if (!context.GetProjectRpgPrincipal().IsGameServer)
                {
                    return Results.StatusCode(
                        StatusCodes.Status403Forbidden);
                }

                ItemRepositoryCommitResult? result =
                    await itemRepository.TryGetCommitResultAsync(
                        requestId,
                        cancellationToken);
                return result is null
                    ? Results.NotFound()
                    : Results.Ok(
                        ItemApiContractMapper.ToContract(result));
            });

        endpoints.MapPost(
            "/api/item-transactions/commit",
            async (
                ItemCommitRequestContract contract,
                HttpContext context,
                ItemTransactionService transactionService,
                CancellationToken cancellationToken) =>
            {
                if (!context.GetProjectRpgPrincipal().IsGameServer)
                {
                    return Results.StatusCode(
                        StatusCodes.Status403Forbidden);
                }

                if (!ItemApiContractMapper.TryToDomain(
                    contract,
                    out ItemRepositoryCommitRequest request,
                    out string error)
                    || !ItemRecordRules.TryValidateCommit(
                        request,
                        out error))
                {
                    return Results.BadRequest(new { error });
                }

                ItemRepositoryCommitResult result =
                    await transactionService.CommitAsync(
                        request,
                        cancellationToken);
                return Results.Json(
                    ItemApiContractMapper.ToContract(result),
                    statusCode: ToStatusCode(result.Status));
            });

        return endpoints;
    }

    private static int ToStatusCode(ItemRepositoryCommitStatus status)
    {
        return status switch
        {
            ItemRepositoryCommitStatus.Committed
                or ItemRepositoryCommitStatus.AlreadyCommitted =>
                StatusCodes.Status200OK,
            ItemRepositoryCommitStatus.InvalidRequest
                or ItemRepositoryCommitStatus.ValidationFailed =>
                StatusCodes.Status400BadRequest,
            ItemRepositoryCommitStatus.NotFound =>
                StatusCodes.Status404NotFound,
            ItemRepositoryCommitStatus.IdempotencyConflict
                or ItemRepositoryCommitStatus.RevisionConflict
                or ItemRepositoryCommitStatus.LocationConflict =>
                StatusCodes.Status409Conflict,
            _ => StatusCodes.Status500InternalServerError
        };
    }
}
