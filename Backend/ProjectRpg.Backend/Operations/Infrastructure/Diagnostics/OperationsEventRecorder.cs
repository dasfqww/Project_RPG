using System.Diagnostics;
using System.Security.Claims;
using System.Text.Json;
using Microsoft.Extensions.Logging;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Diagnostics;

namespace ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;

public sealed class OperationsEventRecorder(OperationsLogStore store, OperationsOptions options, TimeProvider clock)
{
    public static string BuildVersion { get; } = typeof(OperationsEventRecorder).Assembly.GetName().Version
        + "+" + typeof(OperationsEventRecorder).Module.ModuleVersionId.ToString("N");
    private readonly Microsoft.Extensions.Logging.ILogger logger = store.CreateLogger();
    public void Record(HttpContext context, string eventName, string operation, string outcome, double? durationMs = null,
        string? errorCode = null, Guid? characterId = null)
    {
        if (!OperationLogQuery.EventNames.Contains(eventName)) throw new ArgumentException("Unregistered operations event.", nameof(eventName));
        string? actor = context.User.FindFirstValue(ClaimTypes.NameIdentifier);
        if (actor != options.OperatorId) actor = null;
        bool truncated = operation.Length > 128 || outcome.Length > 64 || errorCode?.Length > 128;
        LogEvent value = new(1, Guid.NewGuid(), eventName, clock.GetUtcNow(), errorCode is null ? "Information" : "Warning",
            options.EnvironmentId, "ProjectRpg.Backend", store.BootId,
            BuildVersion,
            OperationsHttp.RequestId(context), Activity.Current?.TraceId.ToString(), Activity.Current?.SpanId.ToString(),
            Activity.Current is { } activity && activity.ParentSpanId != default ? activity.ParentSpanId.ToString() : null,
            operation[..Math.Min(operation.Length, 128)], durationMs, actor, characterId, null, null, null, null, null, null,
            outcome[..Math.Min(outcome.Length, 64)], errorCode is null ? null : errorCode[..Math.Min(errorCode.Length, 128)],
            context.Response.StatusCode, "Operations", truncated);
        // Only these typed, server-owned fields cross the logger boundary. Never attach request/body/exception objects.
        byte[] json = JsonSerializer.SerializeToUtf8Bytes(value, OperationsHttp.Json);
        if (json.Length > 16 * 1024)
            json = JsonSerializer.SerializeToUtf8Bytes(value with { OperationName = "DetailsTruncated", ErrorCode = "ops.details_truncated", DetailsTruncated = true }, OperationsHttp.Json);
        using JsonDocument document = JsonDocument.Parse(json);
        Dictionary<string, object?> properties = document.RootElement.EnumerateObject().ToDictionary(p => p.Name, p => p.Value.ValueKind switch
        {
            JsonValueKind.String => (object?)p.Value.GetString(),
            JsonValueKind.Number => p.Value.GetDouble(),
            JsonValueKind.True => true,
            JsonValueKind.False => false,
            _ => null
        }, StringComparer.Ordinal);
        logger.LogInformation("{@OpsEvent}", properties);
    }
}
