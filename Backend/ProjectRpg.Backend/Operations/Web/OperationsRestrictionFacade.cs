using System.Diagnostics;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Restrictions;

namespace ProjectRpg.Backend.Operations.Web;

public sealed record WebRestrictionPage(IReadOnlyList<RestrictionReceipt> Items,string? NextCursor,bool HasMore);
public sealed class OperationsRestrictionFacade(ChangeAccountRestriction change,ReadAccountRestriction read,OperationsCursor cursors,
    OperationsOptions options,OperationsEventRecorder events)
{
    public Task<Result<RestrictionSnapshot>> FindAsync(string account,CancellationToken ct)=>read.FindAsync(account,ct);
    public Task<Result<RestrictionReceipt>> FindActionAsync(Guid id,CancellationToken ct)=>read.FindActionAsync(id,ct);
    public async Task<Result<RestrictionCommit>> CommitAsync(HttpContext context,RestrictionIntent intent)
    {
        events.RecordRestriction(context,intent,"Started",null);
        var result=await change.ExecuteAsync(intent,OperationsHttp.RequestId(context),Activity.Current?.TraceId.ToString(),context.RequestAborted);
        events.RecordRestriction(context,intent,result.IsSuccess?result.Value.Disposition:result.Error==RestrictionFailures.Unknown?"Unknown":"Rejected",result.Error?.Code);
        return result;
    }
    public async Task<Result<WebRestrictionPage>> SearchAsync(HttpContext context,string account)
    {
        if(!OperationsInput.Limit(context,50,200,out var limit))return Result<WebRestrictionPage>.Fail(Failures.InvalidQuery);
        string key=$"actions\nAccount\n{account}\n{limit}";
        var cursor=cursors.Read<PagePosition>(context.Request.Query["cursor"],options.OperatorId,key);
        if(!cursor.IsSuccess)return Result<WebRestrictionPage>.Fail(cursor.Error!);
        var result=await read.SearchAsync(account,new(limit,cursor.Value.Position),context.RequestAborted);
        return result.IsSuccess?Result<WebRestrictionPage>.Success(new(result.Value.Items,cursors.Write(result.Value.Next,cursor.Value,options.OperatorId,key),result.Value.HasMore)):
            Result<WebRestrictionPage>.Fail(result.Error!);
    }
}
