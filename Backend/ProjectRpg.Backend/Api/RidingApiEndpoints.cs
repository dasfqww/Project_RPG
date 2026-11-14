using System.Text.Json;
using Npgsql;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Riding;

namespace ProjectRpg.Backend.Api;

public static partial class RidingApiEndpoints
{
    public const int MaximumBodyBytes = 16 * 1024;
    private const string Root = "/api/characters/{characterId:guid}/riding";
    public static void MapRidingApi(this WebApplication app)
    {
        MapRidingPreferencesApi(app);
        MapRidingServerSnapshotApi(app);
        MapRidingRegistrationOptionsApi(app);
        app.MapGet(Root, (Guid characterId, HttpContext c, IRidingPreferencesStore store, RidingRuntime runtime, RidingRequestLimiter limiter, CancellationToken ct) =>
            Run(c, limiter, false, async account =>
            {
                var view = await store.GetRidingOwnerViewAsync(account, characterId, ct); var collection = view.Collection;
                return new { characterId, collection.RosterId, rosterRevision = RidingApiContracts.Number(collection.Revision),
                    catalogReady = runtime.Catalog is not null, catalogVersion = runtime.Catalog?.CatalogVersion, gameplayHash = runtime.Catalog?.GameplayHash,
                    registrationEnabled = runtime.Catalog is not null, preferencesReady = true, preferencesEnabled = runtime.Catalog is not null,
                    preferencesRevision = RidingApiContracts.Number(view.Preferences.Revision), preferences = PreferencesContract(view.Preferences), mounts = collection.Mounts };
            }));
        app.MapGet(Root + "/requests/{requestId:guid}", (Guid characterId, Guid requestId, HttpContext c, IRidingPreferencesStore store, RidingRequestLimiter limiter, CancellationToken ct) =>
            Run(c, limiter, false, async account =>
            {
                try { return await store.GetRidingCommandReceiptAsync(account, characterId, requestId, ct) is { } receipt
                    ? CommandReceiptContract(receipt) : Results.NotFound(new { error = "NotFound" }); }
                catch (RidingStoreException e) when (e.Code == RidingStoreFailure.IdempotencyConflict)
                { return Results.NotFound(new { error = "NotFound" }); }
            }));
        app.MapPost(Root + "/register", (Guid characterId, HttpContext c, IRidingRegistrationStore store, RidingRuntime runtime, RidingRequestLimiter limiter, CancellationToken ct) =>
            Run(c, limiter, true, async account =>
            {
                if (!c.Request.HasJsonContentType()) return Results.StatusCode(415);
                if (c.Request.ContentLength > MaximumBodyBytes) return Results.StatusCode(413);
                using var bytes = new MemoryStream(); byte[] buffer = new byte[2048];
                while (true)
                {
                    int remaining = MaximumBodyBytes - (int)bytes.Length;
                    int read = await c.Request.Body.ReadAsync(buffer.AsMemory(0, Math.Min(buffer.Length, remaining + 1)), ct);
                    if (read == 0) break; if (read > remaining) return Results.StatusCode(413); bytes.Write(buffer, 0, read);
                }
                RidingRegistrationCommand command;
                try { using var json = JsonDocument.Parse(bytes.ToArray(), new JsonDocumentOptions { MaxDepth = 8 }); command = RidingApiContracts.Command(json.RootElement); }
                catch (JsonException) { throw RidingApiContracts.Invalid(); }
                // Receipt recovery remains possible with a disabled/replaced catalog. Current authorization is still checked in the store.
                var receipt = await store.GetRidingRegistrationReceiptAsync(account, characterId, command.RequestId, ct);
                if (receipt is not null)
                {
                    if (receipt.Fingerprint != RidingRegistrationRules.Fingerprint(new(characterId, receipt.RosterId), command))
                        throw new RidingStoreException(RidingStoreFailure.IdempotencyConflict);
                    return RidingApiContracts.Result(new(true, receipt));
                }
                if (runtime.Catalog is null) return Results.NotFound(new { error = "RidingDisabled" });
                return RidingApiContracts.Result(await store.RegisterMountAsync(account, characterId, command, runtime.Catalog, ct));
            }));
    }

    private static async Task<IResult> Run(HttpContext c, RidingRequestLimiter limiter, bool write, Func<string, Task<object>> action)
    {
        c.Response.Headers.CacheControl = "no-store";
        var principal = c.GetProjectRpgPrincipal();
        if (principal.Kind != ProjectRpg.Backend.Domain.PrincipalKind.Player || principal.SteamId is null) return Results.StatusCode(403);
        if (!limiter.Acquire(principal.SteamId, write))
        { c.Response.Headers.RetryAfter = "1"; return Results.Json(new { error = "RateLimited" }, statusCode: 429); }
        if (c.Request.Query.Count != 0) return Results.BadRequest(new { error = "InvalidRequest" });
        try { var result = await action(principal.SteamId); return result is IResult response ? response : Results.Ok(result); }
        catch (RidingRuleException e)
        {
            int status = e.Code switch
            {
                RidingFailure.InvalidRequest or RidingFailure.DefinitionMismatch => 400,
                RidingFailure.ItemUnavailable or RidingFailure.WrongInventory => 404,
                RidingFailure.InvalidCatalog => 503,
                _ => 409
            };
            return Results.Json(new { error = status == 404 ? "NotFound" : e.Code.ToString() }, statusCode: status);
        }
        catch (RidingStoreException e)
        {
            int status = e.Code switch { RidingStoreFailure.CharacterNotOwned => 403, RidingStoreFailure.ItemCommitFailed or RidingStoreFailure.InconsistentReceipt => 503, _ => 409 };
            if (status == 503) c.Response.Headers.RetryAfter = "1";
            return Results.Json(new { error = e.Code.ToString() }, statusCode: status);
        }
        catch (Exception e) when (e is NpgsqlException or TimeoutException)
        { c.Response.Headers.RetryAfter = "1"; return Results.Json(new { error = "StorageUnavailable" }, statusCode: 503); }
    }
}
