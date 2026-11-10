using Microsoft.Data.Sqlite;
using ProjectRpg.Production.Contracts;

namespace ProjectRpg.Production.Coordinator;

// Only the coordinator owns this connection. Workers will use the coordinator transport.
public sealed partial class QueueStore : IDisposable
{
    private const int ApplicationId = 0x50524750;
    private readonly SqliteConnection connection;
    private readonly FileStream writerLock;
    private readonly object gate = new();
    private readonly RuntimePolicy policy;
    private readonly TimeProvider clock;
    private bool disposed;

    public QueueStore(string databasePath, RuntimePolicy policy, TimeProvider? clock = null)
    {
        ContractRules.Validate(policy);
        this.policy = ContractJson.Read<RuntimePolicy>(ContractJson.Write(policy));
        this.clock = clock ?? TimeProvider.System;
        var path = Path.GetFullPath(databasePath);
        if (path.StartsWith("\\\\", StringComparison.Ordinal) || !Path.IsPathFullyQualified(databasePath) || !path.EndsWith(".db", StringComparison.OrdinalIgnoreCase))
            throw new QueueException("Use an absolute local .db path.");
        CheckNoReparsePoint(path);
        foreach (var suffix in new[] { ".writer.lock", "-wal", "-shm", "-journal" }) CheckNoReparsePoint(path + suffix);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        try { writerLock = new FileStream(path + ".writer.lock", FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None); }
        catch (IOException) { throw new QueueException("Another coordinator owns this database, or its lock is inaccessible."); }
        connection = new SqliteConnection(new SqliteConnectionStringBuilder
        {
            DataSource = path, Mode = SqliteOpenMode.ReadWriteCreate, Cache = SqliteCacheMode.Private,
            Pooling = false, ForeignKeys = true, DefaultTimeout = 5
        }.ToString());
        try
        {
            connection.Open();
            Initialize();
        }
        catch { connection.Dispose(); writerLock.Dispose(); throw; }
    }

    private static void CheckNoReparsePoint(string path)
    {
        for (var item = path; !string.IsNullOrEmpty(item); item = Path.GetDirectoryName(item))
        {
            if ((File.Exists(item) || Directory.Exists(item)) && (File.GetAttributes(item) & FileAttributes.ReparsePoint) != 0)
                throw new QueueException("Database paths must not traverse reparse points.");
        }
    }

    private void Initialize()
    {
        var version = Convert.ToInt32(Scalar(null, "PRAGMA user_version;"));
        var application = Convert.ToInt32(Scalar(null, "PRAGMA application_id;"));
        if (version != 0 && (version != 1 || application != ApplicationId)) throw new QueueException("Unsupported database version or owner.");
        if (version == 0 && (application != 0 || Convert.ToInt32(Scalar(null, "SELECT COUNT(*) FROM sqlite_master WHERE name NOT LIKE 'sqlite_%';")) != 0))
            throw new QueueException("Refusing to initialize a foreign database.");
        Execute(null, "PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;");
        using var transaction = connection.BeginTransaction(deferred: false);
        if (version == 0)
        {
            Execute(transaction, """
                CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
                CREATE TABLE tasks (
                    task_id TEXT PRIMARY KEY COLLATE NOCASE, card_json TEXT NOT NULL, card_hash TEXT NOT NULL,
                    state TEXT NOT NULL, adoption TEXT NOT NULL, generation INTEGER NOT NULL DEFAULT 0,
                    attempts INTEGER NOT NULL DEFAULT 0, block_reason TEXT, priority INTEGER NOT NULL,
                    created_utc INTEGER NOT NULL, updated_utc INTEGER NOT NULL);
                CREATE TABLE dependencies (
                    task_id TEXT NOT NULL COLLATE NOCASE REFERENCES tasks(task_id),
                    dependency_id TEXT NOT NULL COLLATE NOCASE REFERENCES tasks(task_id),
                    PRIMARY KEY(task_id, dependency_id));
                CREATE TABLE events (
                    sequence INTEGER PRIMARY KEY AUTOINCREMENT, task_id TEXT COLLATE NOCASE REFERENCES tasks(task_id),
                    kind TEXT NOT NULL, detail TEXT NOT NULL, utc_ms INTEGER NOT NULL);
                CREATE INDEX events_task ON events(task_id, sequence);
                CREATE INDEX tasks_dispatch ON tasks(state, priority DESC, created_utc, task_id);
                INSERT INTO settings(key,value) VALUES('paused','true');
                PRAGMA application_id=1347569488;
                PRAGMA user_version=1;
                """);
            Execute(transaction, "INSERT INTO settings(key,value) VALUES('policy_hash',$hash);", ("$hash", ContractJson.Hash(policy)));
        }
        if ((string?)Scalar(transaction, "SELECT value FROM settings WHERE key='policy_hash';") != ContractJson.Hash(policy))
            throw new QueueException("Policy differs from persisted configuration; explicit migration is required.");
        transaction.Commit();
    }

    public TaskSnapshot Enqueue(TaskCard card)
    {
        // Re-read through the wire contract to detach mutable caller arrays.
        card = ContractJson.Read<TaskCard>(ContractJson.Write(card));
        return Mutate(transaction =>
        {
            var existingHash = (string?)Scalar(transaction, "SELECT card_hash FROM tasks WHERE task_id=$id;", ("$id", card.TaskId));
            if (existingHash is not null)
            {
                if (existingHash != ContractJson.Hash(card)) throw new QueueException("Task ID already has different immutable input.");
                return ReadTask(transaction, card.TaskId);
            }
            // Dependencies must already exist. Together with immutable cards this prevents cycles.
            foreach (var dependency in card.DependsOn) _ = ReadTask(transaction, dependency);
            var now = Now;
            Execute(transaction, """
                INSERT INTO tasks(task_id,card_json,card_hash,state,adoption,priority,created_utc,updated_utc)
                VALUES($id,$json,$hash,'Proposed',$adoption,$priority,$now,$now);
                """, ("$id", card.TaskId), ("$json", ContractJson.Write(card)), ("$hash", ContractJson.Hash(card)),
                ("$adoption", (card.RequiresAdoption ? AdoptionState.Pending : AdoptionState.NotRequired).ToString()), ("$priority", card.Priority), ("$now", now));
            foreach (var dependency in card.DependsOn)
                Execute(transaction, "INSERT INTO dependencies(task_id,dependency_id) VALUES($id,$dependency);", ("$id", card.TaskId), ("$dependency", dependency));
            AppendEvent(transaction, card.TaskId, "Enqueued", ContractJson.Hash(card));
            return ReadTask(transaction, card.TaskId);
        });
    }

    public TaskSnapshot Approve(string taskId) => Mutate(transaction =>
    {
        var task = ReadTask(transaction, taskId);
        if (task.State == TaskState.Ready) return task;
        if (task.State != TaskState.Proposed) throw new QueueException("Only a proposed task may become ready.");
        if (task.Card.Limits.MaxAdditionalCostMicrosUsd != 0) throw new QueueException("Paid task dispatch is disabled.");
        SetState(transaction, taskId, TaskState.Ready, null, "Approved");
        return ReadTask(transaction, taskId);
    });

    public TaskSnapshot Cancel(string taskId, string reason) => Mutate(transaction =>
    {
        ValidateReason(reason);
        var task = ReadTask(transaction, taskId);
        if (task.State == TaskState.Canceled) return task;
        if (task.State is not (TaskState.Proposed or TaskState.Ready or TaskState.Blocked or TaskState.NeedsDirector or TaskState.RetryWaiting))
            throw new QueueException("Active or completed tasks cannot be canceled without reconciliation.");
        SetState(transaction, taskId, TaskState.Canceled, reason, "Canceled");
        return ReadTask(transaction, taskId);
    });

    public void SetPaused(bool paused) => Mutate(transaction =>
    {
        if (IsPaused(transaction) != paused)
        {
            Execute(transaction, "UPDATE settings SET value=$value WHERE key='paused';", ("$value", paused ? "true" : "false"));
            AppendEvent(transaction, null, paused ? "Paused" : "Resumed", "Dispatch policy only; active owners are preserved.");
        }
        return true;
    });

    public QueueStatus Status() => Read(() => new QueueStatus(IsPaused(null), ReadTasks(null, "SELECT task_id FROM tasks ORDER BY created_utc,task_id;")));
    public TaskSnapshot Get(string taskId) => Read(() => ReadTask(null, taskId));
    public TaskSnapshot[] Ready() => Read(() => IsPaused(null) ? [] : ReadTasks(null, ReadySql));

    private const string ReadySql = """
        SELECT t.task_id FROM tasks t WHERE t.state='Ready' AND NOT EXISTS (
            SELECT 1 FROM dependencies d JOIN tasks parent ON parent.task_id=d.dependency_id
            WHERE d.task_id=t.task_id AND (parent.state<>'Done' OR parent.adoption NOT IN ('Accepted','NotRequired')))
        ORDER BY t.priority DESC,t.created_utc,t.task_id;
        """;

    public QueueEvent[] Events(long afterSequence = 0, int limit = 200) => Read(() =>
    {
        if (afterSequence < 0 || limit is < 1 or > 1000) throw new QueueException("Invalid event cursor or limit.");
        using var command = Command(null, "SELECT sequence,task_id,kind,detail,utc_ms FROM events WHERE sequence>$after ORDER BY sequence LIMIT $limit;", ("$after", afterSequence), ("$limit", limit));
        using var reader = command.ExecuteReader();
        var events = new List<QueueEvent>();
        while (reader.Read()) events.Add(new(reader.GetInt64(0), reader.IsDBNull(1) ? null : reader.GetString(1), reader.GetString(2), reader.GetString(3), reader.GetInt64(4)));
        return events.ToArray();
    });

    private long Now => clock.GetUtcNow().ToUnixTimeMilliseconds();
    private bool IsPaused(SqliteTransaction? transaction) => (string?)Scalar(transaction, "SELECT value FROM settings WHERE key='paused';") != "false";
    private static void ValidateReason(string reason)
    {
        if (string.IsNullOrWhiteSpace(reason) || reason.Length > 1024) throw new QueueException("A bounded non-secret reason is required.");
    }

    private TaskSnapshot ReadTask(SqliteTransaction? transaction, string id)
    {
        ContractRules.Identifier(id);
        using var command = Command(transaction, "SELECT card_json,state,adoption,generation,attempts,block_reason FROM tasks WHERE task_id=$id;", ("$id", id));
        using var reader = command.ExecuteReader();
        if (!reader.Read()) throw new QueueException("Task does not exist.");
        return new(ContractJson.Read<TaskCard>(reader.GetString(0)), Enum.Parse<TaskState>(reader.GetString(1)), Enum.Parse<AdoptionState>(reader.GetString(2)), reader.GetInt64(3), reader.GetInt32(4), reader.IsDBNull(5) ? null : reader.GetString(5));
    }

    private TaskSnapshot[] ReadTasks(SqliteTransaction? transaction, string sql)
    {
        var ids = new List<string>();
        using (var command = Command(transaction, sql))
        using (var reader = command.ExecuteReader())
            while (reader.Read()) ids.Add(reader.GetString(0));
        return ids.Select(id => ReadTask(transaction, id)).ToArray();
    }

    private void SetState(SqliteTransaction transaction, string id, TaskState state, string? reason, string eventKind)
    {
        Execute(transaction, "UPDATE tasks SET state=$state,block_reason=$reason,updated_utc=$now WHERE task_id=$id;", ("$id", id), ("$state", state.ToString()), ("$reason", reason), ("$now", Now));
        AppendEvent(transaction, id, eventKind, reason ?? state.ToString());
    }

    private void AppendEvent(SqliteTransaction transaction, string? id, string kind, string detail) =>
        Execute(transaction, "INSERT INTO events(task_id,kind,detail,utc_ms) VALUES($id,$kind,$detail,$now);", ("$id", id), ("$kind", kind), ("$detail", detail), ("$now", Now));

    private T Read<T>(Func<T> operation)
    {
        lock (gate) { ObjectDisposedException.ThrowIf(disposed, this); return operation(); }
    }

    private T Mutate<T>(Func<SqliteTransaction, T> operation) => Read(() =>
    {
        using var transaction = connection.BeginTransaction(deferred: false);
        var result = operation(transaction);
        transaction.Commit();
        return result;
    });

    private SqliteCommand Command(SqliteTransaction? transaction, string sql, params (string Name, object? Value)[] parameters)
    {
        var command = connection.CreateCommand();
        command.Transaction = transaction;
        command.CommandText = sql;
        foreach (var (name, value) in parameters) command.Parameters.AddWithValue(name, value ?? DBNull.Value);
        return command;
    }

    private object? Scalar(SqliteTransaction? transaction, string sql, params (string Name, object? Value)[] parameters)
    {
        using var command = Command(transaction, sql, parameters);
        return command.ExecuteScalar();
    }

    private void Execute(SqliteTransaction? transaction, string sql, params (string Name, object? Value)[] parameters)
    {
        using var command = Command(transaction, sql, parameters);
        command.ExecuteNonQuery();
    }

    public void Dispose()
    {
        lock (gate)
        {
            if (disposed) return;
            disposed = true;
            try { connection.Dispose(); } finally { writerLock.Dispose(); }
        }
    }
}
