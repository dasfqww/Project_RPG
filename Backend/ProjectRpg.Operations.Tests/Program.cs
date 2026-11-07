if (args.SequenceEqual(["--service-status-ue-host"])) { await ServiceStatusUEHost.RunAsync(); return; }
if (args.SequenceEqual(["--monitoring"]))
{
    await MonitoringPolicyTests.RunAsync();
    await MonitoringHostTests.RunAsync();
    await DrainMonitoringTests.RunAsync();
    await BackupMonitoringTests.RunAsync();
    return;
}
if (args.Length != 0) throw new ArgumentException("Unknown operations test mode.");
await CoreTests.RunAsync();
await EvidenceCoreTests.RunAsync();
await HostTests.RunAsync();
await CompensationTests.RunAsync();
await MonitoringPolicyTests.RunAsync();
await MonitoringHostTests.RunAsync();
await RestrictionTests.RunAsync();
await ServiceControlTests.RunAsync();
await DrainMonitoringTests.RunAsync();
await BackupMonitoringTests.RunAsync();
Console.WriteLine("Operations contract tests passed.");

internal static class Check
{
    public static void That(bool condition, string description)
    {
        if (!condition) throw new InvalidOperationException(description);
    }
}
