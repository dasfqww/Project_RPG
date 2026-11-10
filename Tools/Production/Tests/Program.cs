using ProjectRpg.Production.Contracts;

if (args.Length < 2)
{
    Console.Error.WriteLine("Usage: <contracts> <repository-root>");
    return 2;
}
try
{
    switch (args[0])
    {
        case "contracts": ContractTests.Run(Path.GetFullPath(args[1])); break;
        default: throw new InvalidOperationException("Unknown test group.");
    }
    Console.WriteLine($"PASS {Check.Count} checks ({args[0]})");
    return 0;
}
catch (Exception ex)
{
    Console.Error.WriteLine(ex);
    return 1;
}

internal static class Check
{
    public static int Count { get; private set; }
    public static void That(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
        Count++;
    }
    public static void Reject(Action action, string message)
    {
        try { action(); }
        catch (Exception ex) when (ex is ContractException or System.Text.Json.JsonException) { Count++; return; }
        throw new Exception(message);
    }
}
