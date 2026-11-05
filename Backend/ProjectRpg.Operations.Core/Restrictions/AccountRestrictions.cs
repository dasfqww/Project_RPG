using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using ProjectRpg.Operations.Core.Access;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Operations.Core.Restrictions;

public sealed record RestrictionIntent(Guid RequestId,string AccountId,long ExpectedRevision,DateTimeOffset? ExpiresAtUtc,string Reason,bool Release=false)
{
    public RestrictionIntent Normalize()=>this with { Reason=Reason?.Trim()??"",ExpiresAtUtc=ExpiresAtUtc?.ToUniversalTime() };
    public bool IsValid=>RequestId!=Guid.Empty&&CharacterLookup.ValidSteamId(AccountId)&&ExpectedRevision>=0&&ExpectedRevision<long.MaxValue
        &&Reason is {Length:>=10 and <=512}&&Reason.EnumerateRunes().Take(10).Count()==10
        &&Reason==Reason.Trim()&&!Reason.Any(char.IsControl)&&(!Release||ExpiresAtUtc is null);
    public string Operation=>Release?"Ops.AccountRestriction.Release.v1":"Ops.AccountRestriction.Set.v1";
}
public sealed record RestrictionCommand(RestrictionIntent Intent,string OperatorId,Guid OriginHttpRequestId,string? OriginTraceId)
{
    public string CanonicalInput=>JsonSerializer.Serialize(new {OperatorId,Intent.AccountId,Intent.ExpectedRevision,Intent.Reason,Intent.Release,
        Expiration=Intent.Release?"Released":Intent.ExpiresAtUtc?.ToString("O")??"Default24Hours"});
    public string InputHash=>Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(CanonicalInput)));
}
public sealed record RestrictionSnapshot(string AccountId,string State,long Revision,DateTimeOffset? StartsAtUtc,
    DateTimeOffset? ExpiresAtUtc,string? Reason,Guid? ActionRequestId,DateTimeOffset ObservedAtUtc);
public sealed record RestrictionEffect(string AccountId,string State,long Revision,DateTimeOffset StartsAtUtc,
    DateTimeOffset ExpiresAtUtc,int RevokedSessionCount);
public sealed record RestrictionTarget(string Scope,string Id);
public sealed record RestrictionReceipt(Guid RequestId,string Operation,DateTimeOffset CommittedAtUtc,string OperatorId,
    RestrictionTarget Target,string Reason,RestrictionEffect Result,Guid OriginHttpRequestId,string? OriginTraceId)
{
    public object EffectReferences=>new {accountId=Result.AccountId,revision=Result.Revision};
}
public sealed record RestrictionCommit(string Disposition,RestrictionReceipt Receipt);
public interface IAccountRestrictionReader
{
    Task<Result<RestrictionSnapshot>> FindAsync(string accountId,CancellationToken ct);
    Task<Result<RestrictionReceipt>> FindActionAsync(Guid requestId,CancellationToken ct);
    Task<Result<Page<RestrictionReceipt>>> SearchActionsAsync(string accountId,PageRequest page,CancellationToken ct);
}
public interface IAccountRestrictionCommitter { Task<Result<RestrictionCommit>> CommitAsync(RestrictionCommand command,CancellationToken ct); }
public static class RestrictionFailures
{
    public static readonly Failure Invalid=new(FailureKind.InvalidInput,"ops.invalid_restriction");
    public static readonly Failure Revision=new(FailureKind.Conflict,"ops.revision_conflict");
    public static readonly Failure AccountMissing=new(FailureKind.NotFound,"ops.account_not_found");
    public static readonly Failure RestrictionMissing=new(FailureKind.NotFound,"ops.restriction_not_found");
    public static readonly Failure ActionMissing=new(FailureKind.NotFound,"ops.action_not_found");
    public static readonly Failure RequestConflict=new(FailureKind.Conflict,"ops.request_conflict");
    public static readonly Failure Unknown=new(FailureKind.Unavailable,"ops.outcome_unknown");
    public static Failure? CheckNew(RestrictionIntent intent,DateTimeOffset now)=>!intent.Release&&intent.ExpiresAtUtc is {} expiry
        &&(expiry<=now||expiry>now.AddDays(30))?Invalid:null;
    public static RestrictionSnapshot Snapshot(RestrictionEffect value,string reason,Guid requestId,DateTimeOffset now)=>
        new(value.AccountId,value.State=="Active"&&value.ExpiresAtUtc<=now?"Expired":value.State,value.Revision,
            value.StartsAtUtc,value.ExpiresAtUtc,reason,requestId,now);
}
public sealed class ChangeAccountRestriction(IOperatorAccess access,IAccountRestrictionCommitter store)
{
    public async Task<Result<RestrictionCommit>> ExecuteAsync(RestrictionIntent intent,Guid httpId,string? traceId,CancellationToken ct)
    {
        var actor=await access.AuthorizeAsync(OpsCapability.RestrictAccount,ct);
        if(!actor.IsSuccess)return Result<RestrictionCommit>.Fail(actor.Error!);
        intent=intent.Normalize();
        return !intent.IsValid?Result<RestrictionCommit>.Fail(RestrictionFailures.Invalid):
            await store.CommitAsync(new(intent,actor.Value.Id,httpId,traceId),ct);
    }
}
public sealed class ReadAccountRestriction(IOperatorAccess access,IAccountRestrictionReader reader)
{
    public async Task<Result<RestrictionSnapshot>> FindAsync(string accountId,CancellationToken ct)
    {
        var actor=await access.AuthorizeAsync(OpsCapability.RestrictAccount,ct);
        if(!actor.IsSuccess)return Result<RestrictionSnapshot>.Fail(actor.Error!);
        return !CharacterLookup.ValidSteamId(accountId)?Result<RestrictionSnapshot>.Fail(RestrictionFailures.Invalid):await reader.FindAsync(accountId,ct);
    }
    public async Task<Result<RestrictionReceipt>> FindActionAsync(Guid requestId,CancellationToken ct)
    {
        var actor=await access.AuthorizeAsync(OpsCapability.RestrictAccount,ct);
        if(!actor.IsSuccess)return Result<RestrictionReceipt>.Fail(actor.Error!);
        return requestId==Guid.Empty?Result<RestrictionReceipt>.Fail(RestrictionFailures.Invalid):await reader.FindActionAsync(requestId,ct);
    }
    public async Task<Result<Page<RestrictionReceipt>>> SearchAsync(string accountId,PageRequest page,CancellationToken ct)
    {
        var actor=await access.AuthorizeAsync(OpsCapability.RestrictAccount,ct);
        if(!actor.IsSuccess)return Result<Page<RestrictionReceipt>>.Fail(actor.Error!);
        return !CharacterLookup.ValidSteamId(accountId)||!page.IsValid()||page.After is {Timestamp:null}
            ?Result<Page<RestrictionReceipt>>.Fail(RestrictionFailures.Invalid):await reader.SearchActionsAsync(accountId,page,ct);
    }
}
