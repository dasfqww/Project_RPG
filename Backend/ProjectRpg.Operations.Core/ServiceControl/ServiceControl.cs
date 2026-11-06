using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using ProjectRpg.Operations.Core.Access;
using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Operations.Core.ServiceControl;

public sealed record ServiceIntent(Guid RequestId, long ExpectedRevision, string Mode, string NoticeText,
    DateTimeOffset? NoticeStartsAtUtc, DateTimeOffset? NoticeEndsAtUtc, string Reason)
{
    public ServiceIntent Normalize() => this with { NoticeText = NoticeText?.Trim() ?? "", Reason = Reason?.Trim() ?? "",
        NoticeStartsAtUtc = Precision(NoticeStartsAtUtc), NoticeEndsAtUtc = Precision(NoticeEndsAtUtc) };
    private static DateTimeOffset? Precision(DateTimeOffset? value) => value is {} instant
        ? new DateTimeOffset(instant.UtcTicks - instant.UtcTicks % 10, TimeSpan.Zero) : null;
    public bool IsValid => RequestId != Guid.Empty && ExpectedRevision >= 0 && ExpectedRevision < long.MaxValue
        && Mode is "Open" or "Drain" && NoticeText is { Length: <= 1000 }
        && !NoticeText.Any(c => char.IsControl(c) && c is not '\n' and not '\r' and not '\t')
        && Reason is { Length: >= 10 and <= 512 } && Reason.EnumerateRunes().Take(10).Count() == 10
        && !Reason.Any(char.IsControl) && (NoticeStartsAtUtc is null || NoticeEndsAtUtc is null || NoticeStartsAtUtc < NoticeEndsAtUtc);
}
public sealed record ServiceSnapshot(long Revision, string Mode, string NoticeText, DateTimeOffset? NoticeStartsAtUtc,
    DateTimeOffset? NoticeEndsAtUtc, DateTimeOffset? UpdatedAtUtc, Guid? ActionRequestId)
{
    public static readonly ServiceSnapshot Initial = new(0, "Open", "", null, null, null, null);
    public bool NoticeVisible(DateTimeOffset now) => NoticeText.Length > 0
        && (NoticeStartsAtUtc is null || NoticeStartsAtUtc <= now) && (NoticeEndsAtUtc is null || now < NoticeEndsAtUtc);
}
public sealed record PublicServiceStatus(string Mode, string NoticeText, DateTimeOffset? NoticeStartsAtUtc,
    DateTimeOffset? NoticeEndsAtUtc, bool NoticeVisible, DateTimeOffset ObservedAtUtc)
{
    public static PublicServiceStatus From(ServiceSnapshot s, DateTimeOffset now) =>
        new(s.Mode, s.NoticeText, s.NoticeStartsAtUtc, s.NoticeEndsAtUtc, s.NoticeVisible(now), now);
}
public sealed record ServiceCommand(ServiceIntent Intent, string OperatorId, Guid OriginHttpRequestId, string? OriginTraceId)
{
    public const string Operation = "Ops.ServiceControl.Change.v1";
    public string CanonicalInput => JsonSerializer.Serialize(new { OperatorId, Intent.ExpectedRevision, Intent.Mode, Intent.NoticeText,
        Intent.NoticeStartsAtUtc, Intent.NoticeEndsAtUtc, Intent.Reason });
    public string InputHash => Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(CanonicalInput)));
    public ServiceReceipt Receipt(DateTimeOffset now) => new(Intent.RequestId, Operation, now, OperatorId, new("Service", "global"), Intent.Reason,
        new(Intent.ExpectedRevision + 1, Intent.Mode, Intent.NoticeText, Intent.NoticeStartsAtUtc, Intent.NoticeEndsAtUtc, now, Intent.RequestId), OriginHttpRequestId, OriginTraceId);
}
public sealed record ServiceTarget(string Scope, string Id);
public sealed record ServiceReceipt(Guid RequestId, string Operation, DateTimeOffset CommittedAtUtc, string OperatorId,
    ServiceTarget Target, string Reason, ServiceSnapshot Result, Guid OriginHttpRequestId, string? OriginTraceId);
public sealed record ServiceCommit(string Disposition, ServiceReceipt Receipt);
public interface IServiceControlReader
{
    Task<Result<ServiceSnapshot>> ReadAsync(CancellationToken ct);
    Task<Result<ServiceReceipt>> FindActionAsync(Guid id, CancellationToken ct);
}
public interface IServiceControlCommitter { Task<Result<ServiceCommit>> CommitAsync(ServiceCommand command, CancellationToken ct); }
public static class ServiceFailures
{
    public static readonly Failure Invalid = new(FailureKind.InvalidInput, "ops.invalid_service_control");
    public static readonly Failure Revision = new(FailureKind.Conflict, "ops.revision_conflict");
    public static readonly Failure Conflict = new(FailureKind.Conflict, "ops.request_conflict");
    public static readonly Failure Missing = new(FailureKind.NotFound, "ops.action_not_found");
    public static readonly Failure Unknown = new(FailureKind.Unavailable, "ops.outcome_unknown");
}
public sealed class ChangeServiceControl(IOperatorAccess access, IServiceControlCommitter store)
{
    public async Task<Result<ServiceCommit>> ExecuteAsync(ServiceIntent intent, Guid httpId, string? trace, CancellationToken ct)
    {
        var actor = await access.AuthorizeAsync(OpsCapability.ManageService, ct);
        if (!actor.IsSuccess) return Result<ServiceCommit>.Fail(actor.Error!);
        intent = intent.Normalize();
        return !intent.IsValid ? Result<ServiceCommit>.Fail(ServiceFailures.Invalid) : await store.CommitAsync(new(intent, actor.Value.Id, httpId, trace), ct);
    }
}
public sealed class ReadServiceControl(IOperatorAccess access, IServiceControlReader store)
{
    public async Task<Result<ServiceSnapshot>> ExecuteAsync(CancellationToken ct)
    {
        var actor = await access.AuthorizeAsync(OpsCapability.ManageService, ct);
        return actor.IsSuccess ? await store.ReadAsync(ct) : Result<ServiceSnapshot>.Fail(actor.Error!);
    }
    public async Task<Result<ServiceReceipt>> FindActionAsync(Guid id, CancellationToken ct)
    {
        var actor = await access.AuthorizeAsync(OpsCapability.ManageService, ct);
        return !actor.IsSuccess ? Result<ServiceReceipt>.Fail(actor.Error!) : id == Guid.Empty
            ? Result<ServiceReceipt>.Fail(ServiceFailures.Invalid) : await store.FindActionAsync(id, ct);
    }
}
