using ProjectRpg.Backend.Domain;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Restrictions;

namespace ProjectRpg.Backend.Data;

public sealed partial class InMemoryGameRepository
{
    private sealed record StoredRestrictionAction(string Hash,RestrictionReceipt Receipt);
    private readonly Dictionary<Guid,StoredRestrictionAction> _restrictionActions=[];
    private readonly Dictionary<string,RestrictionReceipt> _accountRestrictions=new(StringComparer.Ordinal);
    private bool AccountAllowed(string account,DateTimeOffset now)=>!_accountRestrictions.TryGetValue(account,out var action)
        ||action.Result.State!="Active"||action.Result.ExpiresAtUtc<=now;
    private void EnsureAccountAllowed(string account,DateTimeOffset now)
    { if(!AccountAllowed(account,now))throw new AccountRestrictedException(); }
    internal Result<RestrictionCommit> CommitOperationsRestriction(RestrictionCommand command,DateTimeOffset now,CancellationToken ct)
    {
        lock(_dungeonGate)
        {
            ct.ThrowIfCancellationRequested();var intent=command.Intent;
            if(_operationsActions.ContainsKey(intent.RequestId)||_serviceActions.ContainsKey(intent.RequestId))return Result<RestrictionCommit>.Fail(RestrictionFailures.RequestConflict);
            if(_restrictionActions.TryGetValue(intent.RequestId,out var prior))return prior.Hash==command.InputHash
                ?Result<RestrictionCommit>.Success(new("Replayed",prior.Receipt)):Result<RestrictionCommit>.Fail(RestrictionFailures.RequestConflict);
            if(!_accounts.ContainsKey(intent.AccountId))return Result<RestrictionCommit>.Fail(RestrictionFailures.AccountMissing);
            _accountRestrictions.TryGetValue(intent.AccountId,out var previous);
            if(intent.Release&&previous is null)return Result<RestrictionCommit>.Fail(RestrictionFailures.RestrictionMissing);
            if((previous?.Result.Revision??0)!=intent.ExpectedRevision)return Result<RestrictionCommit>.Fail(RestrictionFailures.Revision);
            if(RestrictionFailures.CheckNew(intent,now) is {} failure)return Result<RestrictionCommit>.Fail(failure);
            var revoked=intent.Release?[]:_sessions.Where(p=>p.Value.SteamId==intent.AccountId).ToArray();
            var effect=new RestrictionEffect(intent.AccountId,intent.Release?"Released":"Active",intent.ExpectedRevision+1,
                intent.Release?previous!.Result.StartsAtUtc:now,intent.Release?previous!.Result.ExpiresAtUtc:intent.ExpiresAtUtc??now.AddHours(24),revoked.Length);
            var receipt=new RestrictionReceipt(intent.RequestId,intent.Operation,now,command.OperatorId,new("Account",intent.AccountId),intent.Reason,effect,
                command.OriginHttpRequestId,command.OriginTraceId);
            try
            {
                _restrictionActions.Add(intent.RequestId,new(command.InputHash,receipt));
                _accountRestrictions[intent.AccountId]=receipt;
                foreach(var pair in revoked)_sessions.TryRemove(pair.Key,out _);
                return Result<RestrictionCommit>.Success(new("Committed",receipt));
            }
            catch
            {
                _restrictionActions.Remove(intent.RequestId);
                if(previous is null)_accountRestrictions.Remove(intent.AccountId);else _accountRestrictions[intent.AccountId]=previous;
                foreach(var pair in revoked)_sessions[pair.Key]=pair.Value;
                throw;
            }
        }
    }
    internal Result<RestrictionSnapshot> ReadOperationsRestriction(string account,DateTimeOffset now)
    {
        lock(_dungeonGate)
        {
            if(!_accounts.ContainsKey(account))return Result<RestrictionSnapshot>.Fail(RestrictionFailures.AccountMissing);
            return Result<RestrictionSnapshot>.Success(_accountRestrictions.TryGetValue(account,out var r)
                ?RestrictionFailures.Snapshot(r.Result,r.Reason,r.RequestId,now):new(account,"None",0,null,null,null,null,now));
        }
    }
    internal Result<RestrictionReceipt> ReadOperationsRestrictionAction(Guid id)
    {
        lock(_dungeonGate)return _restrictionActions.TryGetValue(id,out var r)?Result<RestrictionReceipt>.Success(r.Receipt):Result<RestrictionReceipt>.Fail(RestrictionFailures.ActionMissing);
    }
    internal Result<Page<RestrictionReceipt>> ReadOperationsRestrictionActions(string account,PageRequest page)
    {
        lock(_dungeonGate)
        {
            var rows=_restrictionActions.Values.Select(r=>r.Receipt).Where(r=>r.Target.Id==account)
                .Where(r=>page.After is null||r.CommittedAtUtc<page.After.Timestamp||r.CommittedAtUtc==page.After.Timestamp&&r.RequestId.CompareTo(page.After.Id)<0)
                .OrderByDescending(r=>r.CommittedAtUtc).ThenByDescending(r=>r.RequestId).Take(page.Limit+1).ToArray();
            return Result<Page<RestrictionReceipt>>.Success(Operations.Infrastructure.Reads.ReadMapping.Page(rows,page.Limit,r=>new(r.RequestId,r.CommittedAtUtc)));
        }
    }
}
