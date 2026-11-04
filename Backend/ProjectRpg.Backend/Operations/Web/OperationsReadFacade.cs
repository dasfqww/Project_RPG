using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Diagnostics;
using ProjectRpg.Operations.Core.Inventory;
using ProjectRpg.Operations.Core.Sessions;
using ProjectRpg.Operations.Core.Wallets;

namespace ProjectRpg.Backend.Operations.Web;

public sealed record WebPage<T>(IReadOnlyList<T> Items, bool HasMore, string? NextCursor);
public sealed record WebLogPage(IReadOnlyList<LogEvent> Events, string? NextCursor, LogCoverage Coverage,
    LogTruncation? TruncationReason, IReadOnlyList<string> SearchedSources, long ScannedBytes,
    DateTimeOffset SearchedFromUtc, DateTimeOffset SearchedToUtc);

// Input/cursor/wire mapping shared by Razor and JSON adapters. All business access remains in Core.
public sealed class OperationsReadFacade(SearchCharacters search, GetCharacterSummary character, GetInventoryPage inventory,
    GetWallet wallet, GetSessionEvidence sessions, SearchOperationLogs logs,
    OperationsCursor cursors, OperationsOptions options, TimeProvider clock, IHttpContextAccessor contextAccessor)
{
    private void Mark(string kind, Guid? id = null)
    {
        if (contextAccessor.HttpContext is not { } request) return;
        request.Items["Ops.QueryKind"] = kind;
        if (id.HasValue) request.Items["Ops.CharacterId"] = id.Value;
    }
    public async Task<Result<WebPage<CharacterHeader>>> SearchAsync(HttpContext context)
    {
        Mark("SearchCharacters");
        if (!OperationsInput.KnownQuery(context, "kind", "value", "limit", "cursor") || !OperationsInput.Kind(context, out var kind)
            || !OperationsInput.Limit(context, 50, 200, out int limit))
            return Result<WebPage<CharacterHeader>>.Fail(Failures.InvalidQuery);
        string value = context.Request.Query["value"].ToString();
        string key = $"characters\n{kind}\n{value}\n{limit}";
        var state = cursors.Read<PagePosition>(context.Request.Query["cursor"], options.OperatorId, key);
        if (!state.IsSuccess) return Result<WebPage<CharacterHeader>>.Fail(state.Error!);
        return Map(await search.ExecuteAsync(new(kind, value, new(limit, state.Value.Position)), context.RequestAborted), state.Value, key);
    }

    public Task<Result<CharacterHeader>> CharacterAsync(Guid id, CancellationToken ct)
    { Mark("GetCharacterSummary", id); return character.ExecuteAsync(id, ct); }
    public Task<Result<WalletSnapshot>> WalletAsync(Guid id, CancellationToken ct)
    { Mark("GetWallet", id); return wallet.ExecuteAsync(id, ct); }

    public async Task<Result<WebPage<ItemSummary>>> InventoryAsync(HttpContext context, Guid id)
    {
        Mark("GetInventoryPage", id);
        if (!OperationsInput.KnownQuery(context, "panel", "container", "limit", "cursor") || !OperationsInput.Container(context, out var container)
            || !OperationsInput.Limit(context, 50, 200, out int limit))
            return Result<WebPage<ItemSummary>>.Fail(Failures.InvalidQuery);
        string key = $"inventory\n{id:D}\n{container}\n{limit}";
        var state = cursors.Read<PagePosition>(context.Request.Query["cursor"], options.OperatorId, key);
        if (!state.IsSuccess) return Result<WebPage<ItemSummary>>.Fail(state.Error!);
        return Map(await inventory.ExecuteAsync(id, container, new(limit, state.Value.Position), context.RequestAborted), state.Value, key);
    }

    public async Task<Result<WebPage<SessionEvidence>>> SessionsAsync(HttpContext context, Guid id)
    {
        Mark("GetSessionEvidence", id);
        if (!OperationsInput.KnownQuery(context, "panel", "limit", "cursor") || !OperationsInput.Limit(context, 10, 50, out int limit))
            return Result<WebPage<SessionEvidence>>.Fail(Failures.InvalidQuery);
        string key = $"sessions\n{id:D}\n{limit}";
        var state = cursors.Read<PagePosition>(context.Request.Query["cursor"], options.OperatorId, key);
        if (!state.IsSuccess) return Result<WebPage<SessionEvidence>>.Fail(state.Error!);
        return Map(await sessions.ExecuteAsync(id, new(limit, state.Value.Position), context.RequestAborted), state.Value, key);
    }
    public async Task<Result<WebLogPage>> LogsAsync(HttpContext context)
    {
        Mark("Logs");
        if (!OperationsInput.Logs(context, clock, options.SourceId, out OperationLogQuery query))
            return Result<WebLogPage>.Fail(Failures.InvalidQuery);
        string key = OperationsInput.LogKey(query);
        var state = cursors.Read<LogPosition>(context.Request.Query["cursor"], options.OperatorId, key);
        if (!state.IsSuccess) return Result<WebLogPage>.Fail(state.Error!);
        var result = await logs.ExecuteAsync(query with { After = state.Value.Position }, context.RequestAborted);
        if (!result.IsSuccess) return Result<WebLogPage>.Fail(result.Error!);
        var p = result.Value;
        return Result<WebLogPage>.Success(new(p.Events, cursors.Write(p.Next, state.Value, options.OperatorId, key), p.Coverage,
            p.TruncationReason, p.SearchedSources, p.ScannedBytes, p.SearchedFromUtc, p.SearchedToUtc));
    }
    private Result<WebPage<T>> Map<T>(Result<Page<T>> result, CursorState<PagePosition> state, string key) =>
        result.IsSuccess ? Result<WebPage<T>>.Success(new(result.Value.Items, result.Value.HasMore,
            cursors.Write(result.Value.Next, state, options.OperatorId, key))) : Result<WebPage<T>>.Fail(result.Error!);
}
