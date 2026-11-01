using System.Buffers;
using System.Text.Json;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Api;

public static class ItemApiEndpoints
{
    public const int MaximumCommitBodyBytes = 1024 * 1024;

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
                HttpContext context,
                ItemTransactionService transactionService,
                CancellationToken cancellationToken) =>
            {
                if (!context.GetProjectRpgPrincipal().IsGameServer)
                {
                    return Results.StatusCode(
                        StatusCodes.Status403Forbidden);
                }

                // Do not let automatic body binding parse a player's payload
                // before this game-server-only authorization check.
                var (contract, failure) = await ReadCommitAsync(context, cancellationToken);
                if (failure is not null) return failure;
                if (contract?.Mutations is null || contract.Mutations.Count > ItemRecordRules.MaximumMutationCount)
                {
                    return Results.BadRequest(new { error = "invalid_item_commit" });
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

    private static async Task<(ItemCommitRequestContract? Contract, IResult? Failure)> ReadCommitAsync(
        HttpContext context, CancellationToken cancellationToken)
    {
        if (!context.Request.HasJsonContentType()) return (null, Results.StatusCode(415));
        if (context.Request.ContentLength > MaximumCommitBodyBytes) return (null, Results.StatusCode(413));

        Stream originalBody = context.Request.Body;
        using var bounded = new MemoryStream();
        byte[] buffer = ArrayPool<byte>.Shared.Rent(16 * 1024);
        try
        {
            while (true)
            {
                int remaining = MaximumCommitBodyBytes - (int)bounded.Length;
                int read = await originalBody.ReadAsync(buffer.AsMemory(0, Math.Min(buffer.Length, remaining + 1)), cancellationToken);
                if (read == 0) break;
                if (read > remaining) return (null, Results.StatusCode(413));
                bounded.Write(buffer, 0, read);
            }
        }
        finally { ArrayPool<byte>.Shared.Return(buffer, clearArray: true); }

        try
        {
            // Retain ASP.NET's configured JSON options and charset support, but
            // deserialize only the bytes already checked, including chunked input.
            bounded.Position = 0;
            context.Request.Body = bounded;
            return (await context.Request.ReadFromJsonAsync<ItemCommitRequestContract>(cancellationToken), null);
        }
        catch (JsonException) { return (null, Results.BadRequest(new { error = "invalid_item_commit" })); }
        finally { context.Request.Body = originalBody; }
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
