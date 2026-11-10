using Microsoft.Data.Sqlite;
using ProjectRpg.Production.Contracts;

namespace ProjectRpg.Production.Coordinator;

public sealed partial class QueueStore
{
    private void InitializeLeases(SqliteTransaction transaction) => Execute(transaction, """
        ALTER TABLE tasks ADD COLUMN retry_after_utc INTEGER;
        ALTER TABLE tasks ADD COLUMN transient_failures INTEGER NOT NULL DEFAULT 0;
        ALTER TABLE tasks ADD COLUMN defect_failures INTEGER NOT NULL DEFAULT 0;
        CREATE TABLE attempts (
            attempt_id TEXT PRIMARY KEY, task_id TEXT NOT NULL COLLATE NOCASE REFERENCES tasks(task_id),
            generation INTEGER NOT NULL, worker_id TEXT NOT NULL, state TEXT NOT NULL,
            heartbeat_utc INTEGER NOT NULL, deadline_utc INTEGER NOT NULL,
            process_id INTEGER, process_start_ticks INTEGER, machine_name TEXT,
            process_settled INTEGER NOT NULL DEFAULT 0, result_json TEXT, result_hash TEXT,
            UNIQUE(task_id,generation));
        CREATE TABLE resource_leases (
            resource_key TEXT PRIMARY KEY COLLATE NOCASE,
            task_id TEXT NOT NULL COLLATE NOCASE REFERENCES tasks(task_id), generation INTEGER NOT NULL);
        CREATE TABLE path_leases (
            path TEXT NOT NULL COLLATE NOCASE, task_id TEXT NOT NULL COLLATE NOCASE REFERENCES tasks(task_id),
            generation INTEGER NOT NULL, write_access INTEGER NOT NULL,
            PRIMARY KEY(path,task_id));
        CREATE INDEX attempts_task ON attempts(task_id,generation);
        PRAGMA user_version=2;
        """);

    public ClaimedTask? TryClaim(string workerId)
    {
        ContractRules.Identifier(workerId);
        return Mutate<ClaimedTask?>(transaction =>
        {
            if (IsPaused(transaction)) return null;
            foreach (var retry in ReadTasks(transaction, $"SELECT task_id FROM tasks WHERE state='RetryWaiting' AND retry_after_utc<={Now};"))
                SetState(transaction, retry.Card.TaskId, TaskState.Ready, null, "RetryReady");
            foreach (var task in ReadTasks(transaction, ReadySql))
            {
                var card = task.Card;
                if (task.Attempts >= card.Limits.MaxAttempts)
                {
                    SetState(transaction, card.TaskId, TaskState.Blocked, "AttemptLimit", "AttemptLimit");
                    continue;
                }
                if (card.Limits.MaxAdditionalCostMicrosUsd != 0 || HasConflict(transaction, card)) continue;
                var token = new LeaseToken(card.TaskId, Guid.NewGuid().ToString("N"), checked(task.Generation + 1), workerId);
                var now = Now;
                Execute(transaction, """
                    UPDATE tasks SET generation=$generation,attempts=attempts+1,retry_after_utc=NULL WHERE task_id=$id;
                    INSERT INTO attempts(attempt_id,task_id,generation,worker_id,state,heartbeat_utc,deadline_utc)
                    VALUES($attempt,$id,$generation,$worker,'Leased',$now,$deadline);
                    """, ("$id", card.TaskId), ("$generation", token.Generation), ("$attempt", token.AttemptId), ("$worker", workerId),
                    ("$now", now), ("$deadline", checked(now + card.Limits.TimeoutSeconds * 1000L)));
                foreach (var resource in ResourceKeys(card))
                    Execute(transaction, "INSERT INTO resource_leases(resource_key,task_id,generation) VALUES($key,$id,$generation) ON CONFLICT(resource_key) DO UPDATE SET generation=excluded.generation;",
                        ("$key", resource), ("$id", card.TaskId), ("$generation", token.Generation));
                foreach (var (path, write) in PathScopes(card))
                    Execute(transaction, "INSERT INTO path_leases(path,task_id,generation,write_access) VALUES($path,$id,$generation,$write) ON CONFLICT(path,task_id) DO UPDATE SET generation=excluded.generation,write_access=excluded.write_access;",
                        ("$path", path), ("$id", card.TaskId), ("$generation", token.Generation), ("$write", write ? 1 : 0));
                SetState(transaction, card.TaskId, TaskState.Leased, null, "Claimed");
                return new(ReadTask(transaction, card.TaskId), token);
            }
            return null;
        });
    }

    private static IEnumerable<string> ResourceKeys(TaskCard card) => card.Resources.Select(x => "slot:" + x).Append("workspace:" + card.Workspace.WorkspaceId);
    private static (string Path, bool Write)[] PathScopes(TaskCard card) => card.Inputs.Select(x => (x.Path, Write: false))
        .Concat(card.WritePaths.Append(card.Workspace.TemporaryRoot).Append(card.Workspace.EvidenceRoot).Select(x => (Path: x, Write: true)))
        .GroupBy(x => x.Path, StringComparer.OrdinalIgnoreCase).Select(g => (g.Key, g.Any(x => x.Write))).ToArray();

    private bool HasConflict(SqliteTransaction transaction, TaskCard card)
    {
        foreach (var key in ResourceKeys(card))
            if (Convert.ToInt32(Scalar(transaction, "SELECT COUNT(*) FROM resource_leases WHERE resource_key=$key AND task_id<>$id;", ("$key", key), ("$id", card.TaskId))) != 0) return true;
        var requested = PathScopes(card);
        using var command = Command(transaction, "SELECT path,write_access FROM path_leases WHERE task_id<>$id;", ("$id", card.TaskId));
        using var reader = command.ExecuteReader();
        while (reader.Read())
            if (requested.Any(scope => (scope.Write || reader.GetInt32(1) != 0) && ContractRules.Overlaps(scope.Path, reader.GetString(0)))) return true;
        return false;
    }

    public LeaseSnapshot Lease(string attemptId) => Read(() => ReadLease(null, attemptId));

    public LeaseSnapshot[] Attempts(string taskId) => Read(() =>
    {
        _ = ReadTask(null, taskId);
        var ids = new List<string>();
        using (var command = Command(null, "SELECT attempt_id FROM attempts WHERE task_id=$id ORDER BY generation;", ("$id", taskId)))
        using (var reader = command.ExecuteReader())
            while (reader.Read()) ids.Add(reader.GetString(0));
        return ids.Select(id => ReadLease(null, id)).ToArray();
    });

    public TaskSnapshot HandOffRecovered(string taskId, long generation, RecoveryClearance clearance) => Mutate(transaction =>
    {
        var task = ReadTask(transaction, taskId);
        if (task.Generation != generation || task.State is not (TaskState.Blocked or TaskState.RetryWaiting))
            throw new QueueException("Handoff requires the current reconciled task generation.");
        var attempt = (string?)Scalar(transaction, "SELECT attempt_id FROM attempts WHERE task_id=$id ORDER BY generation DESC LIMIT 1;", ("$id", taskId));
        if (attempt is null) throw new QueueException("No attempt to hand off.");
        var lease = ReadLease(transaction, attempt);
        if (lease.State != "Reconciled" || !lease.ProcessSettled) throw new QueueException("Active work cannot be handed off.");
        RequireClearance(lease, clearance);
        Execute(transaction, "DELETE FROM path_leases WHERE task_id=$id; DELETE FROM resource_leases WHERE task_id=$id;", ("$id", taskId));
        SetState(transaction, taskId, TaskState.NeedsDirector, "HandedOff", "HandedOff");
        AppendEvent(transaction, taskId, "HandoffEvidence", clearance.EvidenceRef);
        return ReadTask(transaction, taskId);
    });

    public void Start(LeaseToken token, ProcessIdentity process) => Mutate(transaction =>
    {
        var lease = RequireLive(transaction, token);
        if (lease.State != "Leased" || processProbe.Inspect(process) != ProcessPresence.Running)
            throw new QueueException("Start requires a leased task and a matching live owned process.");
        Execute(transaction, "UPDATE attempts SET state='Running',process_id=$pid,process_start_ticks=$ticks,machine_name=$machine WHERE attempt_id=$attempt;",
            ("$pid", process.ProcessId), ("$ticks", process.StartUtcTicks), ("$machine", process.MachineName), ("$attempt", token.AttemptId));
        SetState(transaction, token.TaskId, TaskState.Running, null, "Started");
        return true;
    });

    public void Heartbeat(LeaseToken token) => Mutate(transaction =>
    {
        var lease = RequireLive(transaction, token);
        if (lease.ProcessSettled) throw new QueueException("Settled attempt does not need worker heartbeats.");
        Execute(transaction, "UPDATE attempts SET heartbeat_utc=$now WHERE attempt_id=$attempt;", ("$now", Math.Max(Now, lease.HeartbeatUtcMilliseconds)), ("$attempt", token.AttemptId));
        return true;
    });

    public TaskSnapshot Report(LeaseToken token, WorkerResult result)
    {
        result = ContractJson.Read<WorkerResult>(ContractJson.Write(result));
        return Mutate(transaction =>
        {
            var lease = RequireCurrent(transaction, token);
            var task = ReadTask(transaction, token.TaskId);
            if (result.TaskId != task.Card.TaskId || result.AttemptId != token.AttemptId || result.Role != task.Card.Role || result.SpecRevision != task.Card.SpecRevision || result.InputManifestHash != ContractJson.InputHash(task.Card))
                throw new QueueException("Result does not match the leased input.");
            if (!result.ArtifactManifest.All(a => task.Card.WritePaths.Any(path => ContractRules.Within(a.Path, path))))
                throw new QueueException("Result artifact is outside reserved scope.");
            var previous = (string?)Scalar(transaction, "SELECT result_hash FROM attempts WHERE attempt_id=$id;", ("$id", token.AttemptId));
            if (previous is not null)
            {
                if (previous != ContractJson.Hash(result)) throw new QueueException("Attempt already has a different result.");
                return task;
            }
            _ = RequireLive(transaction, token);
            if (lease.State != "Running") throw new QueueException("Only a running attempt can report.");
            Execute(transaction, "UPDATE attempts SET result_json=$json,result_hash=$hash,state=$state WHERE attempt_id=$id;",
                ("$json", ContractJson.Write(result)), ("$hash", ContractJson.Hash(result)), ("$state", result.Outcome == WorkerOutcome.Succeeded ? "Checking" : "RecoveryRequired"), ("$id", token.AttemptId));
            SetState(transaction, token.TaskId, result.Outcome == WorkerOutcome.Succeeded ? TaskState.Checking : TaskState.Blocked,
                result.Outcome == WorkerOutcome.Succeeded ? null : result.Outcome.ToString(), "ResultReported");
            return ReadTask(transaction, token.TaskId);
        });
    }

    public LeaseSnapshot[] FlagExpired() => Mutate(transaction =>
    {
        var ids = new List<string>();
        using (var command = Command(transaction, "SELECT attempt_id FROM attempts WHERE state IN ('Leased','Running','Checking') AND process_settled=0 AND (heartbeat_utc<=$cutoff OR deadline_utc<=$now);",
                   ("$cutoff", Now - policy.StaleAfterSeconds * 1000L), ("$now", Now)))
        using (var reader = command.ExecuteReader())
            while (reader.Read()) ids.Add(reader.GetString(0));
        foreach (var id in ids)
        {
            var lease = ReadLease(transaction, id);
            Execute(transaction, "UPDATE attempts SET state='RecoveryRequired' WHERE attempt_id=$id;", ("$id", id));
            SetState(transaction, lease.Token.TaskId, TaskState.Blocked, "LeaseExpired", "RecoveryRequired");
        }
        // Deliberately keep all reservations until actual exit and reconciliation.
        return ids.Select(id => ReadLease(transaction, id)).ToArray();
    });

    public TaskSnapshot Recover(LeaseToken token, RetryCause cause, RecoveryClearance clearance) => Mutate(transaction =>
    {
        if (!Enum.IsDefined(cause)) throw new QueueException("Invalid retry cause.");
        var lease = RequireCurrent(transaction, token);
        if (lease.State != "RecoveryRequired") throw new QueueException("Attempt is not awaiting recovery.");
        RequireClearance(lease, clearance);
        var task = ReadTask(transaction, token.TaskId);
        var transient = Convert.ToInt32(Scalar(transaction, "SELECT transient_failures FROM tasks WHERE task_id=$id;", ("$id", token.TaskId))) + (cause == RetryCause.Transient ? 1 : 0);
        var defects = Convert.ToInt32(Scalar(transaction, "SELECT defect_failures FROM tasks WHERE task_id=$id;", ("$id", token.TaskId))) + (cause == RetryCause.Defect ? 1 : 0);
        var allowed = task.Attempts < task.Card.Limits.MaxAttempts && transient <= policy.TransientBackoffSeconds.Length && defects <= policy.DefectRetries;
        var delay = cause == RetryCause.Transient && transient <= policy.TransientBackoffSeconds.Length ? policy.TransientBackoffSeconds[transient - 1] : 0;
        Execute(transaction, """
            UPDATE attempts SET state='Reconciled',process_settled=1 WHERE attempt_id=$attempt;
            UPDATE tasks SET generation=generation+1,transient_failures=$transient,defect_failures=$defects,retry_after_utc=$retry WHERE task_id=$id;
            DELETE FROM resource_leases WHERE task_id=$id AND resource_key LIKE 'slot:%';
            """, ("$attempt", token.AttemptId), ("$id", token.TaskId), ("$transient", transient), ("$defects", defects), ("$retry", allowed ? Now + delay * 1000L : null));
        // Keep workspace and path reservations, including partial changes, across retries.
        SetState(transaction, token.TaskId, allowed ? TaskState.RetryWaiting : TaskState.Blocked, allowed ? cause.ToString() : "RetryLimit", "Reconciled");
        AppendEvent(transaction, token.TaskId, "RecoveryEvidence", clearance.EvidenceRef);
        return ReadTask(transaction, token.TaskId);
    });

    public void SettleExecution(LeaseToken token, RecoveryClearance clearance) => Mutate(transaction =>
    {
        var lease = RequireCurrent(transaction, token);
        if (lease.State != "Checking") throw new QueueException("Only a checking result may settle execution.");
        if (lease.ProcessSettled) return true;
        RequireClearance(lease, clearance);
        Execute(transaction, "UPDATE attempts SET process_settled=1 WHERE attempt_id=$attempt; DELETE FROM resource_leases WHERE task_id=$id AND resource_key LIKE 'slot:%';",
            ("$attempt", token.AttemptId), ("$id", token.TaskId));
        AppendEvent(transaction, token.TaskId, "ExecutionSettled", clearance.EvidenceRef);
        // Actual QA/publishing still own the product paths. This never marks Done.
        return true;
    });

    private void RequireClearance(LeaseSnapshot lease, RecoveryClearance clearance)
    {
        ContractRules.RelativePath(clearance.EvidenceRef);
        if (!clearance.OwnedExecutionTreeSettled || !clearance.ExternalWorkSettled || !clearance.WorkspaceReconciled || lease.Process is null || processProbe.Inspect(lease.Process) != ProcessPresence.Stopped)
            throw new QueueException("Process exit, external work, and workspace reconciliation must all be confirmed. Unknown is not stopped.");
    }

    private LeaseSnapshot RequireCurrent(SqliteTransaction transaction, LeaseToken token)
    {
        var lease = ReadLease(transaction, token.AttemptId);
        if (lease.Token != token || ReadTask(transaction, token.TaskId).Generation != token.Generation)
            throw new QueueException("Stale or foreign lease token.");
        return lease;
    }

    private LeaseSnapshot RequireLive(SqliteTransaction transaction, LeaseToken token)
    {
        var lease = RequireCurrent(transaction, token);
        if (lease.State is not ("Leased" or "Running" or "Checking") || (!lease.ProcessSettled && (lease.DeadlineUtcMilliseconds <= Now || lease.HeartbeatUtcMilliseconds <= Now - policy.StaleAfterSeconds * 1000L)))
            throw new QueueException("Attempt expired or is awaiting reconciliation.");
        return lease;
    }

    private LeaseSnapshot ReadLease(SqliteTransaction? transaction, string id)
    {
        ContractRules.Identifier(id);
        using var command = Command(transaction, "SELECT task_id,generation,worker_id,state,heartbeat_utc,deadline_utc,process_id,process_start_ticks,machine_name,process_settled FROM attempts WHERE attempt_id=$id;", ("$id", id));
        using var reader = command.ExecuteReader();
        if (!reader.Read()) throw new QueueException("Attempt does not exist.");
        return new(new(reader.GetString(0), id, reader.GetInt64(1), reader.GetString(2)), reader.GetString(3), reader.GetInt64(4), reader.GetInt64(5),
            reader.IsDBNull(6) ? null : new(reader.GetInt32(6), reader.GetInt64(7), reader.GetString(8)), reader.GetInt32(9) == 1);
    }
}
