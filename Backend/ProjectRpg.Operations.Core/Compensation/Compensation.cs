using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using ProjectRpg.Operations.Core.Access;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Operations.Core.Compensation;

public sealed record CompensationIntent(Guid CharacterId, Guid CaseId, string TemplateId, string DefinitionKey, string Reason)
{
    public const string Template = "Ops.Compensation.Item.v1";
    public CompensationIntent Normalize() => this with { Reason = Reason?.Trim() ?? "" };
    public bool IsValid => CharacterId != Guid.Empty && CaseId != Guid.Empty && TemplateId == Template
        && DefinitionKey is { Length: > 0 and <= 256 } && Reason is { Length: >= 10 and <= 512 }
        && Reason == Reason.Trim() && !Reason.Any(char.IsControl);
}
public sealed record CompensationPreview(CompensationIntent Intent, string OperatorId, string PolicyVersion,
    DateTimeOffset IssuedAtUtc, DateTimeOffset ExpiresAtUtc);
public sealed record CompensationCommand(Guid RequestId, CompensationPreview Preview, Guid OriginHttpRequestId, string? OriginTraceId)
{
    public string CanonicalInput => JsonSerializer.Serialize(new { Preview.OperatorId, Preview.Intent });
    public string InputHash => Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(CanonicalInput)));
}
public sealed record ActionTarget(string Scope, Guid Id);
public sealed record CompensationEffect(Guid ItemId, string DefinitionKey, int Quantity, string Location, Guid ItemReceiptRequestId);
public sealed record OperationAction(Guid RequestId, string Operation, DateTimeOffset CommittedAtUtc, string OperatorId,
    ActionTarget Target, Guid CaseId, string Reason, CompensationEffect Result, Guid OriginHttpRequestId, string? OriginTraceId)
{
    public object EffectReferences => new { itemReceiptRequestId = Result.ItemReceiptRequestId, itemId = Result.ItemId };
}
public sealed record ActionCommit(string Disposition, OperationAction Receipt);

public interface ICompensationPolicy
{
    string Version { get; }
    IReadOnlyList<string> AllowedDefinitions { get; }
    bool Allows(string definitionKey);
}
/// <summary>Owns the complete atomic busy-check, item, receipt, audit and case-uniqueness boundary.</summary>
public interface ICompensationCommitter
{
    Task<Result<ActionCommit>> CommitAsync(CompensationCommand command, CancellationToken ct);
}
public interface IOperationActionReader
{
    Task<Result<OperationAction>> FindAsync(Guid requestId, CancellationToken ct);
    Task<Result<Page<OperationAction>>> SearchAsync(Guid characterId, PageRequest page, CancellationToken ct);
}
public static class CompensationFailures
{
    public static readonly Failure Invalid = new(FailureKind.InvalidInput, "ops.invalid_compensation");
    public static readonly Failure PreviewInvalid = new(FailureKind.InvalidInput, "ops.invalid_preview");
    public static readonly Failure Expired = new(FailureKind.Conflict, "ops.preview_expired");
    public static readonly Failure PolicyChanged = new(FailureKind.Conflict, "ops.compensation_policy_changed");
    public static readonly Failure NotAllowed = new(FailureKind.Forbidden, "ops.compensation_not_allowed");
    public static readonly Failure RequestConflict = new(FailureKind.Conflict, "ops.request_conflict");
    public static readonly Failure CaseConflict = new(FailureKind.Conflict, "ops.case_already_compensated");
    public static readonly Failure Busy = new(FailureKind.Conflict, "ops.target_busy");
    public static readonly Failure Missing = new(FailureKind.NotFound, "ops.action_not_found");
    public static readonly Failure Inconsistent = new(FailureKind.Internal, "ops.internal_error");
    public static readonly Failure Unknown = new(FailureKind.Unavailable, "ops.outcome_unknown");
    // Called INSIDE the transaction, only after replay and receipt consistency checks.
    public static Failure? CheckNew(CompensationCommand command, ICompensationPolicy policy, DateTimeOffset now)
    {
        if (command.Preview.ExpiresAtUtc <= now) return Expired;
        if (command.Preview.PolicyVersion != policy.Version) return PolicyChanged;
        return policy.Allows(command.Preview.Intent.DefinitionKey) ? null : NotAllowed;
    }
}
public sealed class PreviewCompensation(IOperatorAccess access, ICharacterReadStore characters, ICompensationPolicy policy, TimeProvider clock)
{
    public async Task<Result<CompensationPreview>> ExecuteAsync(CompensationIntent intent, CancellationToken ct)
    {
        var actor = await access.AuthorizeAsync(OpsCapability.Compensate, ct);
        if (!actor.IsSuccess) return Result<CompensationPreview>.Fail(actor.Error!);
        intent = intent.Normalize();
        if (!intent.IsValid) return Result<CompensationPreview>.Fail(CompensationFailures.Invalid);
        if (!policy.Allows(intent.DefinitionKey)) return Result<CompensationPreview>.Fail(CompensationFailures.NotAllowed);
        var target = await characters.FindAsync(intent.CharacterId, ct);
        if (!target.IsSuccess) return Result<CompensationPreview>.Fail(target.Error!);
        var now = clock.GetUtcNow();
        return Result<CompensationPreview>.Success(new(intent, actor.Value.Id, policy.Version, now, now.AddMinutes(5)));
    }
}
public sealed class CommitCompensation(IOperatorAccess access, ICompensationCommitter committer, TimeProvider clock)
{
    public async Task<Result<ActionCommit>> ExecuteAsync(CompensationCommand command, CancellationToken ct)
    {
        var actor = await access.AuthorizeAsync(OpsCapability.Compensate, ct);
        if (!actor.IsSuccess) return Result<ActionCommit>.Fail(actor.Error!);
        if (command.RequestId == Guid.Empty || !command.Preview.Intent.IsValid
            || command.Preview.OperatorId != actor.Value.Id || command.Preview.IssuedAtUtc > clock.GetUtcNow()
            || command.Preview.ExpiresAtUtc - command.Preview.IssuedAtUtc != TimeSpan.FromMinutes(5))
            return Result<ActionCommit>.Fail(CompensationFailures.PreviewInvalid);
        return await committer.CommitAsync(command, ct);
    }
}
public sealed class ReadOperationActions(IOperatorAccess access, IOperationActionReader reader)
{
    public async Task<Result<OperationAction>> FindAsync(Guid requestId, CancellationToken ct)
    {
        var actor = await access.AuthorizeAsync(OpsCapability.Compensate, ct);
        if (!actor.IsSuccess) return Result<OperationAction>.Fail(actor.Error!);
        return requestId == Guid.Empty ? Result<OperationAction>.Fail(Failures.InvalidQuery) : await reader.FindAsync(requestId, ct);
    }
    public async Task<Result<Page<OperationAction>>> SearchAsync(Guid characterId, PageRequest page, CancellationToken ct)
    {
        var actor = await access.AuthorizeAsync(OpsCapability.Compensate, ct);
        if (!actor.IsSuccess) return Result<Page<OperationAction>>.Fail(actor.Error!);
        if (characterId == Guid.Empty || !page.IsValid() || page.After is { Timestamp: null })
            return Result<Page<OperationAction>>.Fail(Failures.InvalidQuery);
        return await reader.SearchAsync(characterId, page, ct);
    }
}
