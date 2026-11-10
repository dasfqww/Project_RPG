using System.Collections.Concurrent;
using System.Diagnostics;
using ProjectRpg.Production.Contracts;
using ProjectRpg.Production.Coordinator;

internal sealed class ManualClock : TimeProvider
{
    private DateTimeOffset now = new(2026, 11, 10, 0, 0, 0, TimeSpan.Zero);
    public override DateTimeOffset GetUtcNow() => now;
    internal void Advance(int seconds) => now += TimeSpan.FromSeconds(seconds);
}

internal sealed class ProcessProbeFixture : IProcessProbe
{
    internal ProcessPresence Presence = ProcessPresence.Running;
    public ProcessPresence Inspect(ProcessIdentity identity) => Presence;
}

internal static class LeaseTests
{
    private static readonly ProcessIdentity Identity = new(1234, 123456, "fixture");
    private static readonly RecoveryClearance Clearance = new(true, true, true, "Saved/Production/Evidence/reconciled.json");

    internal static void Run(string root, string temporaryRoot)
    {
        var source = ContractTests.Example(root);
        TaskCard Card(string id, ResourceKind[]? resources = null) => source with
        {
            TaskId = id, Resources = resources ?? [ResourceKind.Codex], RequiresAdoption = false,
            WritePaths = ["Production/Work/" + id], OutputPaths = ["Production/Work/" + id + "/output.json"],
            Workspace = source.Workspace with { WorkspaceId = id, TemporaryRoot = "Saved/Production/Temp/" + id, EvidenceRoot = "Saved/Production/Evidence/" + id, CleanupPaths = ["Saved/Production/Temp/" + id] }
        };
        var policy = QueueTests.Policy(root);
        var clock = new ManualClock();
        var probe = new ProcessProbeFixture();
        using (var queue = new QueueStore(Path.Combine(temporaryRoot, "claims.db"), policy, clock, probe))
        {
            Add(queue, Card("one")); Add(queue, Card("two"));
            Check.That(queue.TryClaim("worker") is null, "Paused queue claimed work.");
            queue.SetPaused(false);
            var claims = new ConcurrentBag<ClaimedTask>();
            Parallel.For(0, 16, i => { var claim = queue.TryClaim("worker-" + i); if (claim is not null) claims.Add(claim); });
            Check.That(claims.Count == 1, "Concurrent claims exceeded Codex slot.");
            var lease = claims.Single().Lease;
            Check.That(queue.Get(lease.TaskId).Attempts == 1, "Concurrent claim inflated attempts.");
            queue.Start(lease, Identity);
            Check.Reject(() => queue.Heartbeat(lease with { WorkerId = "impostor" }), "Foreign worker renewed lease.");
            clock.Advance(100); queue.Heartbeat(lease); clock.Advance(100);
            Check.That(queue.FlagExpired().Length == 0, "Heartbeat did not extend lease.");
            clock.Advance(81);
            Check.That(queue.FlagExpired().Single().Token == lease, "Expired lease missing.");
            Check.That(queue.FlagExpired().Length == 0, "Repeated expiry duplicated recovery events.");
            Check.That(queue.TryClaim("next") is null, "Expired process released a slot before exit.");
            Check.Reject(() => queue.Heartbeat(lease), "Expired worker revived lease.");
            Check.Reject(() => queue.Cancel(lease.TaskId, "cancel"), "Cancel bypassed reserved partial workspace.");
            Check.Reject(() => queue.Recover(lease, RetryCause.Transient, Clearance), "Running worker was reclaimed.");
            probe.Presence = ProcessPresence.Unknown;
            Check.Reject(() => queue.Recover(lease, RetryCause.Transient, Clearance), "Unknown process was treated as stopped.");
            probe.Presence = ProcessPresence.Stopped;
            Check.Reject(() => queue.Recover(lease, RetryCause.Transient, Clearance with { OwnedExecutionTreeSettled = false }), "Live descendants ignored.");
            Check.Reject(() => queue.Recover(lease, RetryCause.Transient, Clearance with { ExternalWorkSettled = false }), "Unknown external job ignored.");
            Check.Reject(() => queue.Recover(lease, RetryCause.Transient, Clearance with { WorkspaceReconciled = false }), "Partial changes discarded.");
            Check.That(queue.Recover(lease, RetryCause.Transient, Clearance).State == TaskState.RetryWaiting, "Recovery did not wait for backoff.");
            Check.Reject(() => queue.Recover(lease, RetryCause.Transient, Clearance), "Recovery applied twice.");
            Check.Reject(() => queue.Report(lease, Result(queue.Get(lease.TaskId).Card, lease)), "Old generation accepted a result.");
            var independent = queue.TryClaim("next");
            Check.That(independent is not null && independent.Task.Card.TaskId != lease.TaskId, "Independent work did not proceed during backoff.");
        }
        ScopeConflicts(temporaryRoot, policy, clock, probe, Card);
        RetryAndRestart(temporaryRoot, policy, Card);
        ResultsAndProcessProbe(temporaryRoot, policy, Card);
        UnregisteredAndDeadline(temporaryRoot, policy, Card);
        DefectLimit(temporaryRoot, policy, Card);
        ActualProcessExit(root, temporaryRoot, policy, Card);
    }

    private static void ScopeConflicts(string temp, RuntimePolicy policy, ManualClock clock, ProcessProbeFixture probe, Func<string, ResourceKind[]?, TaskCard> card)
    {
        using var queue = new QueueStore(Path.Combine(temp, "scopes.db"), policy, clock, probe);
        var writer = card("writer", []) with { WritePaths = ["Content/Pilot"], OutputPaths = ["Content/Pilot/output.json"], Priority = 100 };
        var reader = card("reader", []) with { Inputs = [new() { Path = "content/pilot/Mesh.uasset", Sha256 = new string('a', 64) }], Priority = 90 };
        Add(queue, writer); Add(queue, reader); Add(queue, card("other", []));
        queue.SetPaused(false);
        Check.That(queue.TryClaim("w")!.Task.Card.TaskId == "writer", "Priority claim failed.");
        Check.That(queue.TryClaim("w2")!.Task.Card.TaskId == "other", "Read/write conflict blocked unrelated candidate or leaked reader.");
        Check.That(queue.TryClaim("w3") is null, "Nested case-insensitive read/write conflict ignored.");
        var overlap = card("overlap", []) with { WritePaths = ["Content"], OutputPaths = ["Content/other.json"] };
        Add(queue, overlap);
        Check.That(queue.TryClaim("w4") is null, "Ancestor write scope conflict ignored.");
        using var heavy = new QueueStore(Path.Combine(temp, "heavy.db"), policy, clock, probe);
        Add(heavy, card("heavy-one", [ResourceKind.Heavy])); Add(heavy, card("heavy-two", [ResourceKind.Heavy]));
        heavy.SetPaused(false);
        Check.That(heavy.TryClaim("gpu") is not null && heavy.TryClaim("gpu2") is null, "Heavy slot exceeded.");
    }

    private static void RetryAndRestart(string temp, RuntimePolicy policy, Func<string, ResourceKind[]?, TaskCard> card)
    {
        var clock = new ManualClock();
        var probe = new ProcessProbeFixture();
        var database = Path.Combine(temp, "retries.db");
        LeaseToken original;
        using (var queue = new QueueStore(database, policy, clock, probe))
        {
            Add(queue, card("retry", null)); queue.SetPaused(false);
            original = queue.TryClaim("first")!.Lease; queue.Start(original, Identity);
        }
        using (var queue = new QueueStore(database, policy, clock, probe))
        {
            Check.That(queue.Lease(original.AttemptId).Process == Identity && queue.Get("retry").Attempts == 1, "Restart lost process identity/attempts.");
            Check.That(queue.TryClaim("duplicate") is null, "Restart released active reservation.");
            clock.Advance(181); queue.FlagExpired(); probe.Presence = ProcessPresence.Stopped;
            queue.Recover(original, RetryCause.Transient, Clearance);
            Check.That(queue.TryClaim("early") is null, "Retry ignored persisted delay.");
            clock.Advance(60);
            var second = queue.TryClaim("second")!.Lease;
            Check.That(second.Generation > original.Generation && queue.Get("retry").Attempts == 2, "New generation/attempt count missing.");
            Check.Reject(() => queue.Heartbeat(original), "Old token renewed new lease.");
            probe.Presence = ProcessPresence.Running; queue.Start(second, Identity);
            clock.Advance(181); queue.FlagExpired(); probe.Presence = ProcessPresence.Stopped;
            queue.Recover(second, RetryCause.Transient, Clearance);
            clock.Advance(299); Check.That(queue.TryClaim("early") is null, "Second transient retry reset backoff.");
            clock.Advance(1);
            var third = queue.TryClaim("third")!.Lease;
            probe.Presence = ProcessPresence.Running; queue.Start(third, Identity);
            clock.Advance(181); queue.FlagExpired(); probe.Presence = ProcessPresence.Stopped;
            Check.That(queue.Recover(third, RetryCause.Interrupted, Clearance).BlockReason == "RetryLimit", "Attempt cap not enforced.");
            Check.That(queue.TryClaim("fourth") is null && queue.Get("retry").Attempts == 3, "Session restart bypassed attempts cap.");
            var conflict = card("conflict", []) with { WritePaths = ["Production/Work/retry"], OutputPaths = ["Production/Work/retry/new.json"] };
            Add(queue, conflict);
            Check.That(queue.TryClaim("conflict") is null, "Exhausted partial workspace reservation discarded.");
            var current = queue.Get("retry");
            Check.That(queue.Attempts("retry").Length == 3, "Attempt history is not recoverable through coordinator API.");
            Check.Reject(() => queue.HandOffRecovered("retry", current.Generation - 1, Clearance), "Stale handoff released paths.");
            Check.That(queue.HandOffRecovered("retry", current.Generation, Clearance).State == TaskState.NeedsDirector, "Reconciled handoff did not await director.");
            Check.That(queue.TryClaim("replacement")!.Task.Card.TaskId == "conflict", "Handoff retained completed reservation.");
        }
    }

    private static void ResultsAndProcessProbe(string temp, RuntimePolicy policy, Func<string, ResourceKind[]?, TaskCard> card)
    {
        var clock = new ManualClock(); var probe = new ProcessProbeFixture();
        using var queue = new QueueStore(Path.Combine(temp, "results.db"), policy, clock, probe);
        var task = card("result", null); Add(queue, task); Add(queue, card("after", null)); queue.SetPaused(false);
        var token = queue.TryClaim("producer")!.Lease;
        task = queue.Get(token.TaskId).Card;
        queue.Start(token, Identity);
        var result = Result(task, token);
        Check.Reject(() => queue.Report(token, result with { InputManifestHash = new string('f', 64) }), "Wrong input result accepted.");
        Check.Reject(() => queue.Report(token, result with { ArtifactManifest = [new() { Path = "Source/other.cpp", Sha256 = new string('a', 64) }] }), "Out of scope result accepted.");
        Check.That(queue.Report(token, result).State == TaskState.Checking, "Worker success did not enter Checking.");
        var events = queue.Events().Length;
        queue.Report(token, result);
        Check.That(queue.Events().Length == events, "Duplicate report repeated state/event.");
        Check.Reject(() => queue.Report(token, result with { Handoff = "different" }), "Changed duplicate result accepted.");
        Check.Reject(() => queue.SettleExecution(token, Clearance), "Running process resources released.");
        probe.Presence = ProcessPresence.Stopped; queue.SettleExecution(token, Clearance);
        Check.That(queue.Get(token.TaskId).State == TaskState.Checking, "Worker result bypassed actual QA/publishing.");
        clock.Advance(3600);
        Check.That(queue.FlagExpired().Length == 0, "Settled process expired while waiting for QA.");
        Check.That(queue.TryClaim("independent") is not null, "Settled execution did not release Codex slot.");
        using var current = Process.GetCurrentProcess();
        var real = new ProcessIdentity(current.Id, current.StartTime.ToUniversalTime().Ticks, Environment.MachineName);
        var local = new LocalProcessProbe();
        Check.That(local.Inspect(real) == ProcessPresence.Running, "Actual process identity not observed.");
        Check.That(local.Inspect(real with { StartUtcTicks = real.StartUtcTicks - 1 }) == ProcessPresence.Stopped, "Reused PID confused with original process.");
        Check.That(local.Inspect(real with { MachineName = "another-machine" }) == ProcessPresence.Unknown, "Remote process treated as local.");
    }

    private static void UnregisteredAndDeadline(string temp, RuntimePolicy policy, Func<string, ResourceKind[]?, TaskCard> card)
    {
        var clock = new ManualClock(); var probe = new ProcessProbeFixture();
        using var queue = new QueueStore(Path.Combine(temp, "unknown.db"), policy, clock, probe);
        Add(queue, card("unknown", null)); queue.SetPaused(false);
        var token = queue.TryClaim("worker")!.Lease;
        clock.Advance(181); queue.FlagExpired(); probe.Presence = ProcessPresence.Stopped;
        Check.Reject(() => queue.Recover(token, RetryCause.Interrupted, Clearance), "Lost launch acknowledgment assumed no process.");
        using var timed = new QueueStore(Path.Combine(temp, "deadline.db"), policy, clock, probe);
        var shortTask = card("short", null); shortTask = shortTask with { Limits = shortTask.Limits with { TimeoutSeconds = 10 } };
        Add(timed, shortTask); timed.SetPaused(false); var shortLease = timed.TryClaim("w")!.Lease;
        probe.Presence = ProcessPresence.Running; timed.Start(shortLease, Identity);
        clock.Advance(9); timed.Heartbeat(shortLease); clock.Advance(1);
        Check.That(timed.FlagExpired().Length == 1, "Heartbeats extended hard timeout.");
    }

    private static void Add(QueueStore queue, TaskCard card) { queue.Enqueue(card); queue.Approve(card.TaskId); }

    private static void ActualProcessExit(string root, string temp, RuntimePolicy policy, Func<string, ResourceKind[]?, TaskCard> card)
    {
        var start = new ProcessStartInfo(Environment.ProcessPath!) { RedirectStandardOutput = true, RedirectStandardInput = true, RedirectStandardError = true, UseShellExecute = false, CreateNoWindow = true };
        foreach (var arg in new[] { typeof(LeaseTests).Assembly.Location, "process-child", root }) start.ArgumentList.Add(arg);
        using var child = Process.Start(start) ?? throw new Exception("Owned process fixture failed to start.");
        try
        {
            Check.That(child.StandardOutput.ReadLineAsync().WaitAsync(TimeSpan.FromSeconds(15)).GetAwaiter().GetResult() == "ready", "Process fixture not ready.");
            var clock = new ManualClock();
            using var queue = new QueueStore(Path.Combine(temp, "actual-process.db"), policy, clock);
            Add(queue, card("actual", null)); queue.SetPaused(false);
            var token = queue.TryClaim("owner")!.Lease;
            queue.Start(token, new(child.Id, child.StartTime.ToUniversalTime().Ticks, Environment.MachineName));
            clock.Advance(181); queue.FlagExpired();
            Check.Reject(() => queue.Recover(token, RetryCause.Interrupted, Clearance), "Actual living process was reclaimed.");
            child.StandardInput.WriteLine(); child.StandardInput.Flush();
            Check.That(child.WaitForExit(10000) && child.ExitCode == 0, "Owned process did not exit normally.");
            Check.That(queue.Recover(token, RetryCause.Interrupted, Clearance).State == TaskState.RetryWaiting, "Actual exit did not permit reconciliation.");
            Check.That(queue.TryClaim("replacement")!.Lease.Generation > token.Generation, "Actual exit retry generation did not advance.");
        }
        finally { if (!child.HasExited) { child.Kill(entireProcessTree: true); child.WaitForExit(10000); } }
    }

    private static void DefectLimit(string temp, RuntimePolicy policy, Func<string, ResourceKind[]?, TaskCard> card)
    {
        var probe = new ProcessProbeFixture();
        using var queue = new QueueStore(Path.Combine(temp, "defects.db"), policy, new ManualClock(), probe);
        var task = card("defect", null); task = task with { Limits = task.Limits with { MaxAttempts = 10 } };
        Add(queue, task); queue.SetPaused(false);
        for (var attempt = 1; attempt <= 3; attempt++)
        {
            var token = queue.TryClaim("worker-" + attempt)!.Lease;
            probe.Presence = ProcessPresence.Running; queue.Start(token, Identity);
            var failure = Result(task, token) with { Outcome = WorkerOutcome.Failed, BlockReason = "Product defect fixture." };
            Check.That(queue.Report(token, failure).State == TaskState.Blocked, "Failure not blocked for reconciliation.");
            var count = queue.Events().Length;
            queue.Report(token, failure);
            Check.That(queue.Events().Length == count, "Failure report replay changed history.");
            probe.Presence = ProcessPresence.Stopped;
            var recovered = queue.Recover(token, RetryCause.Defect, Clearance);
            Check.That(recovered.State == (attempt < 3 ? TaskState.RetryWaiting : TaskState.Blocked), "Defect retry count is wrong.");
        }
        Check.That(queue.TryClaim("fourth") is null, "New worker reset defect retry cap.");
    }
    private static WorkerResult Result(TaskCard card, LeaseToken token) => new()
    {
        SchemaVersion = 1, TaskId = card.TaskId, AttemptId = token.AttemptId, Role = card.Role, SpecRevision = card.SpecRevision,
        InputManifestHash = ContractJson.InputHash(card), Outcome = WorkerOutcome.Succeeded,
        ArtifactManifest = [new() { Path = card.OutputPaths[0], Sha256 = new string('a', 64) }], ValidationRefs = [],
        Handoff = "Fixture worker report; actual QA and publishing still required.", BlockReason = null, ExternalTaskIds = []
    };
}
