using System.Diagnostics;
using ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;
using ProjectRpg.Operations.Core.ServiceControl;
using ProjectRpg.Operations.Core.Common;
namespace ProjectRpg.Backend.Operations.Web;

public sealed class OperationsServiceFacade(ChangeServiceControl change, ReadServiceControl read, OperationsEventRecorder events)
{
    public Task<Result<ServiceSnapshot>> ReadAsync(CancellationToken ct) => read.ExecuteAsync(ct);
    public Task<Result<ServiceReceipt>> FindActionAsync(Guid id, CancellationToken ct) => read.FindActionAsync(id, ct);
    public async Task<Result<ServiceCommit>> CommitAsync(HttpContext c, ServiceIntent intent)
    {
        events.RecordService(c, intent.RequestId, "Started", null);
        var result = await change.ExecuteAsync(intent, OperationsHttp.RequestId(c), Activity.Current?.TraceId.ToString(), c.RequestAborted);
        events.RecordService(c, intent.RequestId, result.IsSuccess ? result.Value.Disposition : result.Error == ServiceFailures.Unknown ? "Unknown" : "Rejected", result.Error?.Code);
        return result;
    }
}
