if (args.SequenceEqual(["--service-status-ue-host"])) { await ServiceStatusUEHost.RunAsync(); return; }
if (args.SequenceEqual(["--game-http"])) { await CoreTests.RunAsync(); await GameHttpDiagnosticsTests.RunAsync(); return; }
if (args.SequenceEqual(["--game-transaction"])) { await CoreTests.RunAsync(); await GameHttpDiagnosticsTests.RunAsync(); await GameTransactionDiagnosticsTests.RunAsync(); return; }
if (args.SequenceEqual(["--log-navigation"])) { await CoreTests.RunAsync(); await LogNavigationTests.RunAsync(); return; }
if (args.SequenceEqual(["--pickup-tracing"])) { await CoreTests.RunAsync(); await GameTransactionDiagnosticsTests.RunAsync(); await PickupDiagnosticsTests.RunAsync(); return; }
if (args.SequenceEqual(["--equipment-tracing"])) { await CoreTests.RunAsync(); await GameTransactionDiagnosticsTests.RunAsync(); await EquipmentDiagnosticsTests.RunAsync(); return; }
if (args.SequenceEqual(["--instrumentation"])) { await CoreTests.RunAsync(); await LogInstrumentationTests.RunAsync(); return; }
if (args.SequenceEqual(["--trade-tracing"])) { await CoreTests.RunAsync(); await LogNavigationTests.RunAsync(); await LogInstrumentationTests.RunAsync(); await TradeDiagnosticsTests.RunAsync(); return; }
if (args.SequenceEqual(["--storage-tracing"])) { await CoreTests.RunAsync(); await GameTransactionDiagnosticsTests.RunAsync(); await StorageDiagnosticsTests.RunAsync(); await LogInstrumentationTests.RunAsync(); return; }
if (args.SequenceEqual(["--span-navigation"])) { await CoreTests.RunAsync(); await LogNavigationTests.RunAsync(); await SpanNavigationTests.RunAsync(); return; }
if (args.SequenceEqual(["--pickup-storage"])) { await CoreTests.RunAsync(); await PickupDiagnosticsTests.RunAsync(); await StorageDiagnosticsTests.RunAsync(); await LogInstrumentationTests.RunAsync(); return; }
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
await GameHttpDiagnosticsTests.RunAsync();
await GameTransactionDiagnosticsTests.RunAsync();
await LogNavigationTests.RunAsync();
await PickupDiagnosticsTests.RunAsync();
await EquipmentDiagnosticsTests.RunAsync();
await LogInstrumentationTests.RunAsync();
await TradeDiagnosticsTests.RunAsync();
await StorageDiagnosticsTests.RunAsync();
await SpanNavigationTests.RunAsync();
Console.WriteLine("Operations contract tests passed.");

internal static class Check
{
    public static void That(bool condition, string description)
    {
        if (!condition) throw new InvalidOperationException(description);
    }
}
