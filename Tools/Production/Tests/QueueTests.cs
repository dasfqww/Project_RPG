using System.Diagnostics;
using Microsoft.Data.Sqlite;
using ProjectRpg.Production.Contracts;
using ProjectRpg.Production.Coordinator;

internal static class QueueTests
{
    internal static RuntimePolicy Policy(string root) => ContractJson.Read<RuntimePolicy>(File.ReadAllText(Path.Combine(root, "Production/Policies/local.json")));

    internal static void Run(string root, string temporaryRoot)
    {
        var policy = Policy(root);
        var card = ContractTests.Example(root);
        var database = Path.Combine(temporaryRoot, "queue.db");
        long lastSequence;
        using (var queue = new QueueStore(database, policy))
        {
            Check.That(queue.Status().Paused && queue.Status().Tasks.Length == 0, "New database is paused and empty.");
            Check.Reject(() => { using var second = new QueueStore(database, policy); }, "Second writer acquired ownership.");
            Check.That(queue.Enqueue(card).State == TaskState.Proposed, "Enqueue starts proposed.");
            Parallel.For(0, 8, _ => queue.Enqueue(card));
            Check.That(queue.Events().Length == 1 && queue.Status().Tasks.Length == 1, "Concurrent duplicate enqueue produced extra state/event.");
            Check.Reject(() => queue.Enqueue(card with { SpecRevision = "v2" }), "Changed task ID accepted.");
            Check.Reject(() => queue.Enqueue(card with { TaskId = card.TaskId.ToUpperInvariant() }), "Case variant bypassed immutable key.");
            Check.Reject(() => queue.Enqueue(card with { TaskId = "missing-parent", DependsOn = ["not-present"] }), "Missing dependency accepted.");
            Check.That(queue.Status().Tasks.Length == 1 && queue.Events().Length == 1, "Rejected transaction leaked state/events.");
            queue.Approve(card.TaskId);
            queue.Approve(card.TaskId);
            Check.That(queue.Ready().Length == 0, "Paused queue dispatched.");
            queue.SetPaused(false);
            Check.That(queue.Ready().Single().Card.TaskId == card.TaskId, "Approved runnable task missing.");
            var child = card with { TaskId = "dependent", DependsOn = [card.TaskId], Priority = 99 };
            queue.Enqueue(child);
            queue.Approve(child.TaskId);
            Check.That(queue.Ready().Length == 1, "Dependent ran before parent finished.");
            queue.Enqueue(card with { TaskId = "paid", Limits = card.Limits with { MaxAdditionalCostMicrosUsd = 1 } });
            Check.Reject(() => queue.Approve("paid"), "Paid task approved.");
            queue.Cancel("paid", "Budget is not configured.");
            Check.Reject(() => queue.Approve("paid"), "Canceled task approved.");
            var independent = card with { TaskId = "independent", Priority = 90, WritePaths = ["Production/Briefs/Independent"], OutputPaths = ["Production/Briefs/Independent/spec.json"] };
            queue.Enqueue(independent);
            queue.Approve("independent");
            independent.WritePaths[0] = "corrupted-caller-array";
            Check.That(queue.Get("independent").Card.WritePaths[0] == "Production/Briefs/Independent", "Caller changed persisted card.");
            Check.That(queue.Ready()[0].Card.TaskId == "independent", "Priority ordering lost.");
            Check.That(queue.Events(limit: 2).Length == 2, "Event page size.");
            lastSequence = queue.Events().Last().Sequence;
        }
        using (var reopened = new QueueStore(database, policy))
        {
            Check.That(!reopened.Status().Paused && reopened.Status().Tasks.Length == 4, "Restart lost queue/pause state.");
            Check.That(reopened.Events().Last().Sequence == lastSequence && reopened.Events(lastSequence).Length == 0, "Restart/event cursor changed history.");
            reopened.SetPaused(true);
        }
        Check.Reject(() => { using var changed = new QueueStore(database, policy with { DailyReviewTime = "21:00" }); }, "Policy drift ignored.");
        using (var reopened = new QueueStore(database, policy)) Check.That(reopened.Status().Paused, "Pause not durable.");
        var foreign = Path.Combine(temporaryRoot, "foreign.db");
        Execute(foreign, "CREATE TABLE unrelated(value TEXT);");
        Check.Reject(() => { using var wrong = new QueueStore(foreign, policy); }, "Foreign database overwritten.");
        var future = Path.Combine(temporaryRoot, "future.db");
        Execute(future, "PRAGMA user_version=999;");
        Check.Reject(() => { using var wrong = new QueueStore(future, policy); }, "Future database overwritten.");
        Check.Reject(() => { using var wrong = new QueueStore("relative.db", policy); }, "Relative database accepted.");
        CrashAndRestart(root, Path.Combine(temporaryRoot, "crash.db"), policy);
        CliRoundtrip(root, Path.Combine(temporaryRoot, "cli.db"));
        var migration = Path.Combine(temporaryRoot, "v1.db");
        using (var queue = new QueueStore(migration, policy)) queue.Enqueue(card);
        // Reconstruct the previously shipped v1 schema while retaining its task and event.
        Execute(migration, "DROP TABLE path_leases; DROP TABLE resource_leases; DROP TABLE attempts; ALTER TABLE tasks DROP COLUMN retry_after_utc; ALTER TABLE tasks DROP COLUMN transient_failures; ALTER TABLE tasks DROP COLUMN defect_failures; PRAGMA user_version=1;");
        using (var migrated = new QueueStore(migration, policy))
        {
            Check.That(migrated.Get(card.TaskId).State == TaskState.Proposed && migrated.Events().Length == 1, "v1 migration lost existing task/event.");
            migrated.Approve(card.TaskId); migrated.SetPaused(false);
            Check.That(migrated.TryClaim("migrated") is not null, "v1 migrated database cannot create leases.");
        }
    }

    private static void CrashAndRestart(string root, string database, RuntimePolicy policy)
    {
        var start = new ProcessStartInfo(Environment.ProcessPath!) { RedirectStandardOutput = true, RedirectStandardError = true, RedirectStandardInput = true, UseShellExecute = false, CreateNoWindow = true };
        foreach (var arg in new[] { typeof(QueueTests).Assembly.Location, "queue-child", root, database }) start.ArgumentList.Add(arg);
        using var child = Process.Start(start) ?? throw new Exception("Child failed to start.");
        try
        {
            var line = child.StandardOutput.ReadLineAsync().WaitAsync(TimeSpan.FromSeconds(15)).GetAwaiter().GetResult();
            Check.That(line == "transaction-open", "Child did not reach crash point: " + line);
            Check.Reject(() => { using var concurrent = new QueueStore(database, policy); }, "Cross-process writer exclusion failed.");
            child.Kill(entireProcessTree: true); // This test created and owns this child.
            Check.That(child.WaitForExit(10000), "Owned child failed to exit.");
        }
        finally
        {
            if (!child.HasExited) { child.Kill(entireProcessTree: true); child.WaitForExit(10000); }
        }
        using var restored = new QueueStore(database, policy);
        Check.That(restored.Get("crash-task").State == TaskState.Proposed, "Uncommitted state survived process kill.");
        Check.That(restored.Events().Single().Kind == "Enqueued", "Committed event lost or partial event leaked.");
        restored.Approve("crash-task");
        Check.That(restored.Get("crash-task").State == TaskState.Ready, "Writer lock did not recover after exit.");
    }

    internal static void Child(string root, string database)
    {
        using var queue = new QueueStore(database, Policy(root));
        queue.Enqueue(ContractTests.Example(root) with { TaskId = "crash-task" });
        using var connection = new SqliteConnection(new SqliteConnectionStringBuilder { DataSource = database, Pooling = false }.ToString());
        connection.Open();
        using var transaction = connection.BeginTransaction();
        using var command = connection.CreateCommand();
        command.Transaction = transaction;
        // Test-only interrupted transaction, never a production completion API.
        command.CommandText = "UPDATE tasks SET state='Done' WHERE task_id='crash-task'; INSERT INTO events(task_id,kind,detail,utc_ms) VALUES('crash-task','MustRollback','fixture',0);";
        command.ExecuteNonQuery();
        Console.WriteLine("transaction-open");
        Console.Out.Flush();
        Console.ReadLine();
    }

    internal static void Execute(string database, string sql)
    {
        using var connection = new SqliteConnection(new SqliteConnectionStringBuilder { DataSource = database, Pooling = false }.ToString());
        connection.Open();
        using var command = connection.CreateCommand();
        command.CommandText = sql;
        command.ExecuteNonQuery();
    }

    private static void CliRoundtrip(string root, string database)
    {
        var cli = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../ProjectRpg.Production.Coordinator.Cli/release/ProjectRpg.Production.Coordinator.Cli.dll"));
        string Run(params string[] command)
        {
            var start = new ProcessStartInfo(Environment.ProcessPath!) { RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false, CreateNoWindow = true };
            foreach (var arg in new[] { cli, database, Path.Combine(root, "Production/Policies/local.json") }.Concat(command)) start.ArgumentList.Add(arg);
            using var process = Process.Start(start) ?? throw new Exception("CLI did not start.");
            var output = process.StandardOutput.ReadToEndAsync();
            var error = process.StandardError.ReadToEndAsync();
            try
            {
                if (!process.WaitForExit(15000)) throw new Exception("CLI timeout.");
                Check.That(process.ExitCode == 0, "CLI failed: " + error.GetAwaiter().GetResult());
                return output.GetAwaiter().GetResult();
            }
            finally { if (!process.HasExited) { process.Kill(entireProcessTree: true); process.WaitForExit(10000); } }
        }
        Check.That(Run("init").Contains("\"paused\": true"), "CLI initial pause.");
        Check.That(Run("enqueue", Path.Combine(root, "Production/Contracts/Examples/seal-planner.json")).Contains("\"state\": \"proposed\""), "CLI enqueue.");
        Check.That(Run("approve", "seal-planner-v1").Contains("\"state\": \"ready\""), "CLI approve.");
        Check.That(Run("status").Contains("seal-planner-v1"), "CLI process restart lost task.");
        Check.That(Run("resume").Contains("\"paused\": false"), "CLI resume.");
        Check.That(Run("ready").Contains("seal-planner-v1"), "CLI candidate.");
        Check.That(Run("events").Contains("Approved"), "CLI event persistence.");
    }
}
