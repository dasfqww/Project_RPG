await CoreTests.RunAsync();
await EvidenceCoreTests.RunAsync();
await HostTests.RunAsync();
Console.WriteLine("Operations contract tests passed.");

internal static class Check
{
    public static void That(bool condition, string description)
    {
        if (!condition) throw new InvalidOperationException(description);
    }
}
