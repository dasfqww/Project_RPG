using System.Diagnostics;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Compensation;

namespace ProjectRpg.Backend.Operations.Web;

public sealed record WebCompensationPreview(CompensationIntent NormalizedIntent, string PreviewToken, DateTimeOffset ExpiresAtUtc);
public sealed record WebActionPage(IReadOnlyList<OperationAction> Items, string? NextCursor, bool HasMore);
public sealed record CompensationSubmission(Guid RequestId, string PreviewToken);
public sealed class OperationsCompensationFacade(PreviewCompensation preview, CommitCompensation commit,
    ReadOperationActions actions, CompensationPreviewTokens tokens, OperationsCursor cursors, OperationsOptions options, OperationsEventRecorder events)
{
    public async Task<Result<WebCompensationPreview>> PreviewAsync(CompensationIntent input, CancellationToken ct)
    {
        var result = await preview.ExecuteAsync(input, ct);
        return result.IsSuccess ? Result<WebCompensationPreview>.Success(new(result.Value.Intent, tokens.Protect(result.Value), result.Value.ExpiresAtUtc))
            : Result<WebCompensationPreview>.Fail(result.Error!);
    }
    public async Task<Result<ActionCommit>> CommitAsync(HttpContext context, CompensationSubmission input)
    {
        var token = tokens.Read(input.PreviewToken);
        if (!token.IsSuccess) return Result<ActionCommit>.Fail(token.Error!);
        CompensationCommand command = new(input.RequestId, token.Value, OperationsHttp.RequestId(context), Activity.Current?.TraceId.ToString());
        events.RecordCompensation(context, command, "Started", null);
        var result = await commit.ExecuteAsync(command, context.RequestAborted);
        // Log transport failure cannot reverse an already committed business result.
        events.RecordCompensation(context, command, result.IsSuccess ? result.Value.Disposition
            : result.Error == CompensationFailures.Unknown ? "Unknown" : "Rejected", result.Error?.Code,
            result.IsSuccess ? result.Value.Receipt.Result.ItemId : null);
        return result;
    }
    public Task<Result<OperationAction>> FindAsync(Guid id, CancellationToken ct) => actions.FindAsync(id, ct);
    public async Task<Result<WebActionPage>> SearchAsync(HttpContext context, Guid characterId)
    {
        if (!OperationsInput.Limit(context, 50, 200, out int limit)) return Result<WebActionPage>.Fail(Failures.InvalidQuery);
        string key = $"actions\nCharacter\n{characterId:D}\n{limit}";
        var cursor = cursors.Read<PagePosition>(context.Request.Query["cursor"], options.OperatorId, key);
        if (!cursor.IsSuccess) return Result<WebActionPage>.Fail(cursor.Error!);
        var result = await actions.SearchAsync(characterId, new(limit, cursor.Value.Position), context.RequestAborted);
        return result.IsSuccess ? Result<WebActionPage>.Success(new(result.Value.Items,
            cursors.Write(result.Value.Next, cursor.Value, options.OperatorId, key), result.Value.HasMore)) : Result<WebActionPage>.Fail(result.Error!);
    }
}
