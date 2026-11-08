namespace ProjectRpg.Operations.Core.Diagnostics;

public enum GameHttpPhase { Started, Completed }
public enum GameHttpOutcome { Started, Succeeded, Rejected, Faulted, Canceled }

/// <summary>Server-owned transport facts only. No request, principal, URI, body or exception crosses this port.</summary>
public sealed record GameHttpObservation(GameHttpPhase Phase, Guid HttpRequestId, string? TraceId, string? SpanId,
    string? ParentSpanId, string OperationName, GameHttpOutcome Outcome, int? HttpStatus, double? DurationMs);

public interface IGameHttpObserver
{
    void Observe(GameHttpObservation observation);
}
