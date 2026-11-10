using ProjectRpg.Production.Contracts;

try
{
    if (args.Length == 2 && args[0] == "schemas")
    {
        Directory.CreateDirectory(args[1]);
        File.WriteAllText(Path.Combine(args[1], "task.schema.json"), ContractJson.Schema<TaskCard>());
        File.WriteAllText(Path.Combine(args[1], "worker-result.schema.json"), ContractJson.Schema<WorkerResult>());
        File.WriteAllText(Path.Combine(args[1], "policy.schema.json"), ContractJson.Schema<RuntimePolicy>());
        Console.WriteLine("Generated 3 structural schemas.");
        return 0;
    }
    if (args.Length == 3 && args[0] == "validate")
    {
        var json = File.ReadAllText(args[2]);
        object value = args[1] switch
        {
            "task" => ContractJson.Read<TaskCard>(json),
            "result" => ContractJson.Read<WorkerResult>(json),
            "policy" => ContractJson.Read<RuntimePolicy>(json),
            _ => throw new ContractException("Use task, result, or policy.")
        };
        Console.WriteLine(ContractJson.Write(new { valid = true, hash = ContractJson.Hash(value) }));
        return 0;
    }
    Console.Error.WriteLine("Usage: schemas <output-directory> | validate <task|result|policy> <file>");
    return 2;
}
catch (Exception ex) when (ex is ContractException or System.Text.Json.JsonException or IOException)
{
    Console.Error.WriteLine(ex.Message);
    return 1;
}
