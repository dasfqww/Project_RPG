using System.Security.Claims;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Operations.Core.Access;
using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Backend.Operations.Web;

public sealed class OperationsAccess(IHttpContextAccessor context, OperationsOptions options) : IOperatorAccess
{
    public Task<Result<OperatorActor>> AuthorizeAsync(OpsCapability capability, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        ClaimsPrincipal? user = context.HttpContext?.User;
        if (user?.Identity?.IsAuthenticated != true)
            return Task.FromResult(Result<OperatorActor>.Fail(Failures.Unauthenticated));
        bool permitted = user.Identities.Any(i => i.AuthenticationType == OperationsAuthentication.CookieScheme
            && i.FindFirst(ClaimTypes.NameIdentifier)?.Value == options.OperatorId);
        return Task.FromResult(permitted && Enum.IsDefined(capability)
            ? Result<OperatorActor>.Success(new(options.OperatorId)) : Result<OperatorActor>.Fail(Failures.Forbidden));
    }
}
