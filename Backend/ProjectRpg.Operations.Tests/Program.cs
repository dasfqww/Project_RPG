if (args.SequenceEqual(["--service-status-ue-host"])) { await ServiceStatusUEHost.RunAsync(); return; }
await CoreTests.RunAsync();
await EvidenceCoreTests.RunAsync();
await HostTests.RunAsync();
await CompensationTests.RunAsync();
await MonitoringPolicyTests.RunAsync();
await MonitoringHostTests.RunAsync();
await RestrictionTests.RunAsync();
await ServiceControlTests.RunAsync();
Console.WriteLine("Operations contract tests passed.");

internal static class Check
{
    public static void That(bool condition, string description)
    {
        if (!condition) throw new InvalidOperationException(description);
    }
}
