using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Backend.Operations.Web;

internal static class OperationsActionEndpoints
{
    internal static void Map(WebApplication app)
    {
        var options=app.Services.GetRequiredService<OperationsOptions>();
        if(!options.Compensation.Enabled&&!options.RestrictionsEnabled&&!options.ServiceControlEnabled)return;
        var api=app.MapGroup("/ops/api/actions").RequireAuthorization(OperationsAuthentication.Policy);
        api.MapGet("/{requestId}",async(string requestId,HttpContext c,OperationsCompensationFacade compensation,OperationsRestrictionFacade restrictions,OperationsServiceFacade service,OperationsOptions o,TimeProvider clock)=>
        {
            if(!OperationsInput.Id(requestId,out var id)||!OperationsInput.KnownQuery(c))return OperationsHttp.Error(c,Failures.InvalidQuery);
            if(o.ServiceControlEnabled)
            {
                var result=await service.FindActionAsync(id,c.RequestAborted);
                if(result.IsSuccess)return OperationsHttp.Success(c,result.Value,o,clock);
                if(result.Error!.Kind!=FailureKind.NotFound)return OperationsHttp.Error(c,result.Error!);
            }
            if(o.RestrictionsEnabled)
            {
                var result=await restrictions.FindActionAsync(id,c.RequestAborted);
                if(result.IsSuccess)return OperationsHttp.Success(c,result.Value,o,clock);
                if(result.Error!.Kind!=FailureKind.NotFound)return OperationsHttp.Error(c,result.Error!);
            }
            if(o.Compensation.Enabled)
            {
                var result=await compensation.FindAsync(id,c.RequestAborted);
                return result.IsSuccess?OperationsHttp.Success(c,result.Value,o,clock):OperationsHttp.Error(c,result.Error!);
            }
            return OperationsHttp.Error(c,ProjectRpg.Operations.Core.Restrictions.RestrictionFailures.ActionMissing);
        });
        api.MapGet("",async(HttpContext c,OperationsCompensationFacade compensation,OperationsRestrictionFacade restrictions,OperationsServiceFacade service,OperationsOptions o,TimeProvider clock)=>
        {
            if(!OperationsInput.KnownQuery(c,"targetScope","targetId","limit","cursor"))return OperationsHttp.Error(c,Failures.InvalidQuery);
            if(c.Request.Query["targetScope"]=="Account"&&o.RestrictionsEnabled)
            {
                var result=await restrictions.SearchAsync(c,c.Request.Query["targetId"].ToString());
                return result.IsSuccess?OperationsHttp.Success(c,result.Value,o,clock):OperationsHttp.Error(c,result.Error!);
            }
            if(c.Request.Query["targetScope"]=="Character"&&o.Compensation.Enabled&&OperationsInput.Id(c.Request.Query["targetId"].ToString(),out var id))
            {
                var result=await compensation.SearchAsync(c,id);
                return result.IsSuccess?OperationsHttp.Success(c,result.Value,o,clock):OperationsHttp.Error(c,result.Error!);
            }
            return OperationsHttp.Error(c,Failures.InvalidQuery);
        });
    }
}
