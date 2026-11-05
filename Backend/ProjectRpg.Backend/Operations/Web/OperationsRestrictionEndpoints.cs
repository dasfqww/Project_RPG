using Microsoft.AspNetCore.Antiforgery;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Restrictions;

namespace ProjectRpg.Backend.Operations.Web;

public sealed record RestrictAccountRequest(Guid RequestId,string AccountId,long ExpectedRevision,DateTimeOffset? ExpiresAtUtc,string Reason);
public sealed record ReleaseAccountRequest(Guid RequestId,long ExpectedRevision,string Reason);
internal static class OperationsRestrictionEndpoints
{
    internal static void Map(WebApplication app)
    {
        if(!app.Services.GetRequiredService<OperationsOptions>().RestrictionsEnabled)return;
        var api=app.MapGroup("/ops/api/account-restrictions").RequireAuthorization(OperationsAuthentication.Policy);
        api.MapGet("/{account}",async(string account,HttpContext c,OperationsRestrictionFacade facade,OperationsOptions o,TimeProvider clock)=>
        {
            if(!OperationsInput.KnownQuery(c))return OperationsHttp.Error(c,Failures.InvalidQuery);
            var result=await facade.FindAsync(account,c.RequestAborted);
            return result.IsSuccess?OperationsHttp.Success(c,result.Value,o,clock):OperationsHttp.Error(c,result.Error!);
        });
        api.MapPost("",async(HttpContext c,IAntiforgery csrf,OperationsRestrictionFacade facade,OperationsOptions o,TimeProvider clock)=>
        {
            var input=await OperationsCommandInput.ReadAsync<RestrictAccountRequest>(c,csrf);
            if(!input.IsSuccess)return OperationsHttp.Error(c,input.Error!);
            var v=input.Value;
            return Reply(c,await facade.CommitAsync(c,new(v.RequestId,v.AccountId,v.ExpectedRevision,v.ExpiresAtUtc,v.Reason)),o,clock);
        });
        api.MapPost("/{account}/release",async(string account,HttpContext c,IAntiforgery csrf,OperationsRestrictionFacade facade,OperationsOptions o,TimeProvider clock)=>
        {
            var input=await OperationsCommandInput.ReadAsync<ReleaseAccountRequest>(c,csrf);
            if(!input.IsSuccess)return OperationsHttp.Error(c,input.Error!);
            var v=input.Value;
            return Reply(c,await facade.CommitAsync(c,new(v.RequestId,account,v.ExpectedRevision,null,v.Reason,true)),o,clock);
        });
    }
    private static IResult Reply(HttpContext context,Result<RestrictionCommit> result,OperationsOptions options,TimeProvider clock)=>
        result.IsSuccess?Results.Json(new {data=result.Value,meta=OperationsHttp.Meta(context,options,clock)},OperationsHttp.Json,
            statusCode:result.Value.Disposition=="Committed"?201:200):OperationsHttp.Error(context,result.Error!);
}
