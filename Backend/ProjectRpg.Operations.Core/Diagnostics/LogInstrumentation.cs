namespace ProjectRpg.Operations.Core.Diagnostics;

public sealed record LogProducerConfiguration(string Key, string DisplayName, bool ConfiguredEnabled,
    IReadOnlyList<string> Boundaries, IReadOnlyList<string> EventNames);

/// <summary>Configured producers in the current process, independent of historical search or delivery coverage.</summary>
public sealed record LogInstrumentationSnapshot(DateTimeOffset ObservedAtUtc, string Environment, string SourceId,
    string BootId, string BuildVersion, IReadOnlyList<LogProducerConfiguration> Producers)
{
    public string Scope => "CurrentProcessOnly";
    public string HistoricalCoverage => "Unknown";
}

public interface ILogInstrumentationReader
{
    LogInstrumentationSnapshot Read();
}
