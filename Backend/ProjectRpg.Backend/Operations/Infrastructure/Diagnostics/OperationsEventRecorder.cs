using System.Diagnostics;
using System.Security.Claims;
using System.Text.Json;
using Microsoft.Extensions.Logging;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Diagnostics;
using ProjectRpg.Backend.Application;

namespace ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;

public sealed class OperationsEventRecorder(OperationsLogStore store, OperationsOptions options, TimeProvider clock) : IGameHttpObserver
{
    public static string BuildVersion { get; } = typeof(OperationsEventRecorder).Assembly.GetName().Version
        + "+" + typeof(OperationsEventRecorder).Module.ModuleVersionId.ToString("N");
    private readonly Microsoft.Extensions.Logging.ILogger logger = store.CreateLogger();
    public void RecordGameTransaction(Guid httpRequestId, GameTransactionObservation observation)
    {
        Activity? activity = Activity.Current;
        bool hasTrace = activity is not null && activity.TraceId != default;
        string operation = observation.Kind switch
        {
            GameTransactionKind.Item => "ItemTransaction.Commit",
            GameTransactionKind.Currency => "CurrencyTransaction.Commit",
            GameTransactionKind.DungeonPickup => "DungeonPickup.Commit",
            GameTransactionKind.EquipmentCraft => "Equipment.Craft",
            GameTransactionKind.EquipmentDismantle => "Equipment.Dismantle",
            GameTransactionKind.EquipmentUpgrade => "Equipment.Upgrade",
            GameTransactionKind.EquipmentTransfer => "Equipment.Transfer",
            _ => throw new ArgumentOutOfRangeException(nameof(observation))
        };
        Write(new(1,Guid.NewGuid(),observation.Disposition == GameTransactionDisposition.Committed
                ? "game.transaction.receipt.committed" : "game.transaction.receipt.replayed",
            clock.GetUtcNow(),"Information",options.EnvironmentId,"ProjectRpg.Backend",store.BootId,BuildVersion,httpRequestId,
            hasTrace ? activity!.TraceId.ToString() : null,hasTrace && activity!.SpanId != default ? activity.SpanId.ToString() : null,
            hasTrace && activity!.ParentSpanId != default ? activity.ParentSpanId.ToString() : null,
            operation,null,null,observation.CharacterId,null,
            observation.Kind == GameTransactionKind.DungeonPickup ? observation.DungeonSessionId : null,
            null,observation.TransactionRequestId,
            observation.Kind == GameTransactionKind.DungeonPickup ? observation.ItemId : null,null,
            observation.Disposition.ToString(),null,null,"GameTransaction"));
    }
    public void Observe(GameHttpObservation observation)
    {
        string? error = observation.Outcome switch
        {
            GameHttpOutcome.Rejected => "game.http_rejected",
            GameHttpOutcome.Faulted => "game.http_faulted",
            GameHttpOutcome.Canceled => "game.http_canceled",
            _ => null
        };
        Write(new(1,Guid.NewGuid(),observation.Phase == GameHttpPhase.Started ? "game.http.started" : "game.http.completed",
            clock.GetUtcNow(),observation.Outcome == GameHttpOutcome.Faulted ? "Error" : error is null ? "Information" : "Warning",
            options.EnvironmentId,"ProjectRpg.Backend",store.BootId,BuildVersion,observation.HttpRequestId,
            observation.TraceId,observation.SpanId,observation.ParentSpanId,observation.OperationName,observation.DurationMs,
            null,null,null,null,null,null,null,null,observation.Outcome.ToString(),error,observation.HttpStatus,"GameHttp"));
    }
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
        Write(value);
    }
    public void RecordSettlement(DungeonSettlementObservation observation)
    {
        string eventName = observation.Phase switch
        {
            DungeonSettlementPhase.ReceiptCommitted => "game.reward.receipt.committed",
            DungeonSettlementPhase.Started => "game.settlement.attempt.started",
            DungeonSettlementPhase.Completed => "game.settlement.attempt.completed",
            DungeonSettlementPhase.Requeued => "game.settlement.attempt.requeued",
            _ => "game.settlement.attempt.failed"
        };
        Write(new(1, Guid.NewGuid(), eventName, clock.GetUtcNow(), observation.Phase == DungeonSettlementPhase.Failed ? "Warning" : "Information",
            options.EnvironmentId, "ProjectRpg.Backend", store.BootId, BuildVersion, null,
            Activity.Current?.TraceId.ToString(), Activity.Current?.SpanId.ToString(), null,
            "DungeonRewardSettlement.Attempt" + observation.Attempt, null, null, observation.CharacterId, null,
            observation.SessionId, null, observation.TransactionRequestId, null, null, observation.Phase.ToString(),
            observation.Phase == DungeonSettlementPhase.Failed ? "settlement_failed" : null, null, "DungeonSettlement"));
    }
    public void RecordCompensation(HttpContext context, ProjectRpg.Operations.Core.Compensation.CompensationCommand command,
        string outcome, string? error, Guid? itemId = null)
    {
        try
        {
            string suffix = error == "ops.outcome_unknown" ? "unknown" : outcome.ToLowerInvariant();
            Write(new(1, Guid.NewGuid(), "ops.command." + suffix, clock.GetUtcNow(), error is null ? "Information" : "Warning",
                options.EnvironmentId, "ProjectRpg.Backend", store.BootId, BuildVersion, OperationsHttp.RequestId(context),
                Activity.Current?.TraceId.ToString(), Activity.Current?.SpanId.ToString(), null,
                ProjectRpg.Operations.Core.Compensation.CompensationIntent.Template, null, command.Preview.OperatorId,
                command.Preview.Intent.CharacterId, null, null, command.RequestId, command.RequestId, itemId, command.Preview.Intent.CaseId,
                outcome, error, null, "OperationsCompensation"));
        }
        catch (Exception) { /* Optional technical log failure must not alter the atomic audit/receipt result. */ }
    }
    public void RecordRestriction(HttpContext context,ProjectRpg.Operations.Core.Restrictions.RestrictionIntent intent,string outcome,string? error)
    {
        try
        {
            Write(new(1,Guid.NewGuid(),"ops.command."+outcome.ToLowerInvariant(),clock.GetUtcNow(),error is null?"Information":"Warning",
                options.EnvironmentId,"ProjectRpg.Backend",store.BootId,BuildVersion,OperationsHttp.RequestId(context),
                Activity.Current?.TraceId.ToString(),Activity.Current?.SpanId.ToString(),null,intent.Operation,null,options.OperatorId,
                null,null,null,intent.RequestId,null,null,null,outcome,error,null,"OperationsRestriction"));
        }
        catch(Exception) { /* The immutable database receipt is authoritative even when technical logging is unavailable. */ }
    }
    public void RecordService(HttpContext context, Guid requestId, string outcome, string? error)
    {
        try
        {
            Write(new(1,Guid.NewGuid(),"ops.command."+outcome.ToLowerInvariant(),clock.GetUtcNow(),error is null?"Information":"Warning",
                options.EnvironmentId,"ProjectRpg.Backend",store.BootId,BuildVersion,OperationsHttp.RequestId(context),
                Activity.Current?.TraceId.ToString(),Activity.Current?.SpanId.ToString(),null,
                ProjectRpg.Operations.Core.ServiceControl.ServiceCommand.Operation,null,options.OperatorId,
                null,null,null,requestId,null,null,null,outcome,error,null,"OperationsServiceControl"));
        }
        catch(Exception) { /* Committed database audit remains authoritative. */ }
    }
    private void Write(LogEvent value)
    {
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
