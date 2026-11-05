using System.Text.Json;
using Npgsql;
using NpgsqlTypes;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Restrictions;

namespace ProjectRpg.Backend.Operations.Restrictions;

public sealed class PostgresRestrictionStore(NpgsqlDataSource source,TimeProvider clock):IAccountRestrictionReader,IAccountRestrictionCommitter
{
    private const string ActionColumns="request_id,operation,committed_at,operator_id,target_account_id,reason,result_json::text,origin_http_request_id,origin_trace_id,input_hash";
    private sealed record Stored(string Hash,RestrictionReceipt Receipt);
    private static Stored ReadAction(NpgsqlDataReader r)=>new(Convert.ToHexString(r.GetFieldValue<byte[]>(9)),new(r.GetGuid(0),r.GetString(1),
        r.GetFieldValue<DateTimeOffset>(2),r.GetString(3),new("Account",r.GetString(4)),r.GetString(5),
        JsonSerializer.Deserialize<RestrictionEffect>(r.GetString(6),OperationsHttp.Json)??throw new JsonException(),r.GetGuid(7),r.IsDBNull(8)?null:r.GetString(8)));
    private static DateTimeOffset Precision(DateTimeOffset instant)=>new(instant.UtcTicks-instant.UtcTicks%10,TimeSpan.Zero);
    private static async Task<NpgsqlTransaction> BeginReadAsync(NpgsqlConnection connection,CancellationToken ct)
    {
        var transaction=await connection.BeginTransactionAsync(System.Data.IsolationLevel.RepeatableRead,ct);
        try
        {
            await using var mode=new NpgsqlCommand("SET TRANSACTION READ ONLY; SET LOCAL statement_timeout='3000ms'",connection,transaction);
            await mode.ExecuteNonQueryAsync(ct);return transaction;
        }
        catch{await transaction.DisposeAsync();throw;}
    }
    public async Task<Result<RestrictionCommit>> CommitAsync(RestrictionCommand input,CancellationToken ct)
    {
        bool committing=false;
        try
        {
            var intent=input.Intent;
            await using var connection=await source.OpenConnectionAsync(ct);await using var tx=await connection.BeginTransactionAsync(ct);
            await using(var gate=new NpgsqlCommand("SET LOCAL statement_timeout='3000ms'; SELECT pg_advisory_xact_lock(hashtextextended(@key,0))",connection,tx))
            {gate.Parameters.AddWithValue("key","ops_action:"+intent.RequestId.ToString("D"));await gate.ExecuteNonQueryAsync(ct);}
            await using(var prior=new NpgsqlCommand($"SELECT {ActionColumns},target_scope FROM ops_actions WHERE request_id=@id",connection,tx))
            {
                prior.Parameters.AddWithValue("id",intent.RequestId);await using var rows=await prior.ExecuteReaderAsync(ct);
                if(await rows.ReadAsync(ct))
                {
                    if(rows.GetString(10)!="Account")return Result<RestrictionCommit>.Fail(RestrictionFailures.RequestConflict);
                    var existing=ReadAction(rows);
                    return existing.Hash==input.InputHash?Result<RestrictionCommit>.Success(new("Replayed",existing.Receipt)):
                        Result<RestrictionCommit>.Fail(RestrictionFailures.RequestConflict);
                }
            }
            await PostgresAccountAccess.LockAsync(connection,tx,intent.AccountId,ct);
            var now=Precision(clock.GetUtcNow());
            await using(var account=new NpgsqlCommand("SELECT 1 FROM accounts WHERE steam_id=@id FOR UPDATE",connection,tx))
            {account.Parameters.AddWithValue("id",intent.AccountId);if(await account.ExecuteScalarAsync(ct) is null)return Result<RestrictionCommit>.Fail(RestrictionFailures.AccountMissing);}
            RestrictionEffect? previous=null;
            await using(var query=new NpgsqlCommand("SELECT revision,state,starts_at,expires_at FROM ops_account_restrictions WHERE account_id=@id FOR UPDATE",connection,tx))
            {
                query.Parameters.AddWithValue("id",intent.AccountId);await using var r=await query.ExecuteReaderAsync(ct);
                if(await r.ReadAsync(ct))previous=new(intent.AccountId,r.GetString(1),r.GetInt64(0),r.GetFieldValue<DateTimeOffset>(2),r.GetFieldValue<DateTimeOffset>(3),0);
            }
            if(intent.Release&&previous is null)return Result<RestrictionCommit>.Fail(RestrictionFailures.RestrictionMissing);
            if((previous?.Revision??0)!=intent.ExpectedRevision)return Result<RestrictionCommit>.Fail(RestrictionFailures.Revision);
            if(RestrictionFailures.CheckNew(intent,now) is {} failure)return Result<RestrictionCommit>.Fail(failure);
            var expires=intent.Release?previous!.ExpiresAtUtc:Precision(intent.ExpiresAtUtc??now.AddHours(24));
            if(!intent.Release&&expires<=now)return Result<RestrictionCommit>.Fail(RestrictionFailures.Invalid);
            int revoked=0;
            if(!intent.Release)
            {
                await using var revoke=new NpgsqlCommand("DELETE FROM auth_sessions WHERE steam_id=@id",connection,tx);
                revoke.Parameters.AddWithValue("id",intent.AccountId);revoked=await revoke.ExecuteNonQueryAsync(ct);
            }
            var effect=new RestrictionEffect(intent.AccountId,intent.Release?"Released":"Active",intent.ExpectedRevision+1,intent.Release?previous!.StartsAtUtc:now,expires,revoked);
            var action=new RestrictionReceipt(intent.RequestId,intent.Operation,now,input.OperatorId,new("Account",intent.AccountId),intent.Reason,effect,input.OriginHttpRequestId,input.OriginTraceId);
            await using(var command=new NpgsqlCommand("""
                INSERT INTO ops_actions(request_id,operation,input_hash,canonical_input,operator_id,target_scope,target_account_id,reason,committed_at,result_json,origin_http_request_id,origin_trace_id)
                VALUES(@request,@operation,@hash,@input,@operator,'Account',@account,@reason,@now,@result,@http,@trace);
                INSERT INTO ops_account_restrictions(account_id,revision,state,starts_at,expires_at,reason,action_request_id)
                VALUES(@account,@revision,@state,@starts,@expires,@reason,@request)
                ON CONFLICT(account_id) DO UPDATE SET revision=EXCLUDED.revision,state=EXCLUDED.state,starts_at=EXCLUDED.starts_at,
                    expires_at=EXCLUDED.expires_at,reason=EXCLUDED.reason,action_request_id=EXCLUDED.action_request_id;
                """,connection,tx))
            {
                command.Parameters.AddWithValue("request",intent.RequestId);command.Parameters.AddWithValue("operation",intent.Operation);
                command.Parameters.AddWithValue("hash",Convert.FromHexString(input.InputHash));command.Parameters.AddWithValue("input",NpgsqlDbType.Jsonb,input.CanonicalInput);
                command.Parameters.AddWithValue("operator",input.OperatorId);command.Parameters.AddWithValue("account",intent.AccountId);
                command.Parameters.AddWithValue("reason",intent.Reason);command.Parameters.AddWithValue("now",now);
                command.Parameters.AddWithValue("result",NpgsqlDbType.Jsonb,JsonSerializer.Serialize(effect,OperationsHttp.Json));command.Parameters.AddWithValue("http",input.OriginHttpRequestId);
                command.Parameters.AddWithValue("trace",NpgsqlDbType.Varchar,(object?)input.OriginTraceId??DBNull.Value);
                command.Parameters.AddWithValue("revision",effect.Revision);command.Parameters.AddWithValue("state",effect.State);
                command.Parameters.AddWithValue("starts",effect.StartsAtUtc);command.Parameters.AddWithValue("expires",effect.ExpiresAtUtc);
                await command.ExecuteNonQueryAsync(ct);
            }
            committing=true;await tx.CommitAsync(ct);return Result<RestrictionCommit>.Success(new("Committed",action));
        }
        catch(Exception e) when(e is NpgsqlException or TimeoutException or OperationCanceledException)
        {return Result<RestrictionCommit>.Fail(committing?RestrictionFailures.Unknown:Failures.StorageUnavailable);}
    }
    public async Task<Result<RestrictionSnapshot>> FindAsync(string account,CancellationToken ct)
    {
        await using var connection=await source.OpenConnectionAsync(ct);
        await using var transaction=await BeginReadAsync(connection,ct);
        await using var query=new NpgsqlCommand("""
            SELECT a.steam_id,r.revision,r.state,r.starts_at,r.expires_at,r.reason,r.action_request_id FROM accounts a
            LEFT JOIN ops_account_restrictions r ON r.account_id=a.steam_id WHERE a.steam_id=@id
            """,connection,transaction){CommandTimeout=3};query.Parameters.AddWithValue("id",account);
        await using var r=await query.ExecuteReaderAsync(ct);
        if(!await r.ReadAsync(ct))return Result<RestrictionSnapshot>.Fail(RestrictionFailures.AccountMissing);
        var now=clock.GetUtcNow();
        return Result<RestrictionSnapshot>.Success(r.IsDBNull(1)?new(account,"None",0,null,null,null,null,now):
            RestrictionFailures.Snapshot(new(account,r.GetString(2),r.GetInt64(1),r.GetFieldValue<DateTimeOffset>(3),r.GetFieldValue<DateTimeOffset>(4),0),r.GetString(5),r.GetGuid(6),now));
    }
    public async Task<Result<RestrictionReceipt>> FindActionAsync(Guid id,CancellationToken ct)
    {
        await using var connection=await source.OpenConnectionAsync(ct);await using var transaction=await BeginReadAsync(connection,ct);
        await using var query=new NpgsqlCommand($"SELECT {ActionColumns} FROM ops_actions WHERE request_id=@id AND target_scope='Account'",connection,transaction){CommandTimeout=3};
        query.Parameters.AddWithValue("id",id);await using var r=await query.ExecuteReaderAsync(ct);
        return await r.ReadAsync(ct)?Result<RestrictionReceipt>.Success(ReadAction(r).Receipt):Result<RestrictionReceipt>.Fail(RestrictionFailures.ActionMissing);
    }
    public async Task<Result<Page<RestrictionReceipt>>> SearchActionsAsync(string account,PageRequest page,CancellationToken ct)
    {
        await using var connection=await source.OpenConnectionAsync(ct);await using var transaction=await BeginReadAsync(connection,ct);
        await using var query=new NpgsqlCommand($"""
            SELECT {ActionColumns} FROM ops_actions WHERE target_scope='Account' AND target_account_id=@account
            {(page.After is null?"":"AND (committed_at,request_id)<(@at,@after)")} ORDER BY committed_at DESC,request_id DESC LIMIT @limit
            """,connection,transaction){CommandTimeout=3};query.Parameters.AddWithValue("account",account);query.Parameters.AddWithValue("limit",page.Limit+1);
        if(page.After is {} after){query.Parameters.AddWithValue("at",after.Timestamp!.Value);query.Parameters.AddWithValue("after",after.Id);}
        List<RestrictionReceipt> rows=[];await using var r=await query.ExecuteReaderAsync(ct);while(await r.ReadAsync(ct))rows.Add(ReadAction(r).Receipt);
        return Result<Page<RestrictionReceipt>>.Success(Infrastructure.Reads.ReadMapping.Page(rows,page.Limit,a=>new(a.RequestId,a.CommittedAtUtc)));
    }
}
