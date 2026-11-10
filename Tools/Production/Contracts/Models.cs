namespace ProjectRpg.Production.Contracts;

public enum ProductionRole { Planner, ArtDirector, Developer, Integrator, QaReviewer, Reporter }
public enum ResourceKind { Codex, Heavy, Git, External3D }
public enum TaskState { Proposed, Ready, Leased, Running, Checking, Publishing, Done, RetryWaiting, Blocked, NeedsDirector, Canceled }
public enum WorkerOutcome { Succeeded, Failed, Interrupted, Blocked }
public enum AdoptionState { NotRequired, Pending, Accepted, Rejected }

public sealed record FileDigest
{
    public required string Path { get; init; }
    public required string Sha256 { get; init; }
}

public sealed record ExecutionLimits
{
    public required int TimeoutSeconds { get; init; }
    public required int MaxAttempts { get; init; }
    // Integer millionths of USD; this is an upper bound, never evidence of actual cost.
    public required long MaxAdditionalCostMicrosUsd { get; init; }
}

public sealed record WorkspacePlan
{
    public required string WorkspaceId { get; init; }
    public required string TemporaryRoot { get; init; }
    public required string EvidenceRoot { get; init; }
    public required string[] CleanupPaths { get; init; }
}

public sealed record TaskCard
{
    public required int SchemaVersion { get; init; }
    public required string TaskId { get; init; }
    public required string ParentGoal { get; init; }
    public required ProductionRole Role { get; init; }
    public required int Priority { get; init; }
    public required string[] DependsOn { get; init; }
    public required string SpecRevision { get; init; }
    public required string BaseCommit { get; init; }
    public required FileDigest[] Inputs { get; init; }
    public required string[] WritePaths { get; init; }
    public required ResourceKind[] Resources { get; init; }
    public required string[] OutputPaths { get; init; }
    public required string[] AcceptanceCriteria { get; init; }
    public required string[] CheckIds { get; init; }
    public required string Owner { get; init; }
    public required ProductionRole? HandoffRole { get; init; }
    public required ExecutionLimits Limits { get; init; }
    public required WorkspacePlan Workspace { get; init; }
    public required bool RequiresAdoption { get; init; }
}

public sealed record WorkerResult
{
    public required int SchemaVersion { get; init; }
    public required string TaskId { get; init; }
    public required string AttemptId { get; init; }
    public required ProductionRole Role { get; init; }
    public required string SpecRevision { get; init; }
    public required string InputManifestHash { get; init; }
    public required WorkerOutcome Outcome { get; init; }
    public required FileDigest[] ArtifactManifest { get; init; }
    public required string[] ValidationRefs { get; init; }
    public required string Handoff { get; init; }
    public required string? BlockReason { get; init; }
    public required string[] ExternalTaskIds { get; init; }
}

public sealed record RuntimePolicy
{
    public required int SchemaVersion { get; init; }
    public required string AuthenticationMode { get; init; }
    public required bool AllowApiFallback { get; init; }
    public required bool AllowExtraSpending { get; init; }
    public required long DailyAdditionalBudgetMicrosUsd { get; init; }
    public required int CodexConcurrency { get; init; }
    public required int HeavyConcurrency { get; init; }
    public required int GitConcurrency { get; init; }
    public required int HeartbeatSeconds { get; init; }
    public required int StaleAfterSeconds { get; init; }
    public required int DefectRetries { get; init; }
    public required int[] TransientBackoffSeconds { get; init; }
    public required string ReviewTimeZone { get; init; }
    public required string? DailyReviewTime { get; init; }
}
