using System.Diagnostics;

namespace ProjectRpg.Production.Coordinator;

public enum ProcessPresence { Running, Stopped, Unknown }
public enum RetryCause { Interrupted, Transient, Defect }
public sealed record ProcessIdentity(int ProcessId, long StartUtcTicks, string MachineName);
public sealed record LeaseToken(string TaskId, string AttemptId, long Generation, string WorkerId);
public sealed record LeaseSnapshot(LeaseToken Token, string State, long HeartbeatUtcMilliseconds, long DeadlineUtcMilliseconds, ProcessIdentity? Process, bool ProcessSettled);
public sealed record ClaimedTask(TaskSnapshot Task, LeaseToken Lease);

// Trusted coordinator evidence from the process/external-job/workspace adapters, not worker self-report.
// EvidenceRef is a retained record; these flags do not themselves inspect files or external providers.
public sealed record RecoveryClearance(bool OwnedExecutionTreeSettled, bool ExternalWorkSettled, bool WorkspaceReconciled, string EvidenceRef);

public interface IProcessProbe
{
    ProcessPresence Inspect(ProcessIdentity identity);
}

public sealed class LocalProcessProbe : IProcessProbe
{
    public ProcessPresence Inspect(ProcessIdentity identity)
    {
        if (identity.ProcessId <= 0 || identity.StartUtcTicks <= 0 || !identity.MachineName.Equals(Environment.MachineName, StringComparison.OrdinalIgnoreCase))
            return ProcessPresence.Unknown;
        try
        {
            using var process = Process.GetProcessById(identity.ProcessId);
            // A reused PID is not the original worker. Never terminate it.
            return process.HasExited || process.StartTime.ToUniversalTime().Ticks != identity.StartUtcTicks ? ProcessPresence.Stopped : ProcessPresence.Running;
        }
        catch (ArgumentException) { return ProcessPresence.Stopped; }
        catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException or NotSupportedException)
        { return ProcessPresence.Unknown; }
    }
}
