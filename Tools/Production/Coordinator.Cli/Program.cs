using ProjectRpg.Production.Contracts;
using ProjectRpg.Production.Coordinator;

try
{
    if (args.Length < 3) return Usage();
    var policy = ContractJson.Read<RuntimePolicy>(File.ReadAllText(args[1]));
    using var queue = new QueueStore(Path.GetFullPath(args[0]), policy);
    object result;
    switch (args[2])
    {
        case "init" or "status" when args.Length == 3: result = queue.Status(); break;
        case "events" when args.Length == 3: result = queue.Events(); break;
        case "ready" when args.Length == 3: result = queue.Ready(); break;
        case "enqueue" when args.Length == 4: result = queue.Enqueue(ContractJson.Read<TaskCard>(File.ReadAllText(args[3]))); break;
        case "approve" when args.Length == 4: result = queue.Approve(args[3]); break;
        case "get" when args.Length == 4: result = queue.Get(args[3]); break;
        case "cancel" when args.Length == 5: result = queue.Cancel(args[3], args[4]); break;
        case "pause" when args.Length == 3: queue.SetPaused(true); result = queue.Status(); break;
        case "resume" when args.Length == 3: queue.SetPaused(false); result = queue.Status(); break;
        default: return Usage();
    }
    Console.WriteLine(ContractJson.Write(result));
    return 0;
}
catch (Exception ex) when (ex is ContractException or QueueException or System.Text.Json.JsonException or IOException or Microsoft.Data.Sqlite.SqliteException)
{
    Console.Error.WriteLine(ContractJson.Write(new { error = ex.GetType().Name, message = ex.Message }));
    return 1;
}

static int Usage()
{
    Console.Error.WriteLine("Usage: <database.db> <policy.json> init|status|events|ready|pause|resume|enqueue <task.json>|approve|get <task-id>|cancel <task-id> <reason>");
    return 2;
}
