using System.Text.RegularExpressions;

namespace ProjectRpg.Production.Contracts;

public static partial class ContractRules
{
    [GeneratedRegex("^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$", RegexOptions.CultureInvariant)]
    private static partial Regex IdentifierPattern();
    [GeneratedRegex("^[0-9a-f]{64}$", RegexOptions.CultureInvariant)]
    private static partial Regex HashPattern();
    [GeneratedRegex("^[0-9a-f]{40}$", RegexOptions.CultureInvariant)]
    private static partial Regex CommitPattern();
    [GeneratedRegex("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])($|\\.)", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant)]
    private static partial Regex DevicePattern();

    public static void Require([System.Diagnostics.CodeAnalysis.DoesNotReturnIf(false)] bool condition, string message)
    {
        if (!condition) throw new ContractException(message);
    }

    public static void Identifier(string? value) => Require(value is not null && IdentifierPattern().IsMatch(value), "Invalid identifier.");
    public static void Hash(string? value) => Require(value is not null && HashPattern().IsMatch(value), "Expected lowercase SHA-256.");
    public static void Commit(string? value) => Require(value is not null && CommitPattern().IsMatch(value), "Expected full lowercase Git SHA-1.");

    public static void RelativePath(string? path)
    {
        Require(!string.IsNullOrWhiteSpace(path) && path.Length <= 240, "Invalid relative path length.");
        Require(!path!.Any(c => char.IsControl(c) || "\\:*?\"<>|".Contains(c)), "Paths use literal forward-slash segments without wildcards.");
        foreach (var segment in path.Split('/'))
        {
            Require(segment.Length > 0 && segment is not "." and not ".." && !segment.EndsWith('.') && !segment.EndsWith(' '), "Unsafe path segment.");
            Require(!DevicePattern().IsMatch(segment) && !segment.Equals(".git", StringComparison.OrdinalIgnoreCase), "Reserved path segment.");
        }
    }

    public static bool Overlaps(string left, string right) => Within(left, right) || Within(right, left);
    public static bool Within(string path, string root) => path.Equals(root, StringComparison.OrdinalIgnoreCase) || path.StartsWith(root + "/", StringComparison.OrdinalIgnoreCase);

    private static void Strings(string[]? values, Action<string?> validate, bool nonEmpty = false)
    {
        Require(values is not null && values.Length <= 256 && (!nonEmpty || values.Length > 0), "Invalid list length.");
        foreach (var value in values!) validate(value);
        Require(values.Distinct(StringComparer.OrdinalIgnoreCase).Count() == values.Length, "Duplicate list entry.");
    }

    private static void Text(string? value) => Require(!string.IsNullOrWhiteSpace(value) && value.Length <= 8192, "Required text is empty or too long.");
    private static void Digests(FileDigest[]? values)
    {
        Require(values is not null && values.Length <= 1024, "Invalid manifest.");
        foreach (var item in values!)
        {
            Require(item is not null, "Null manifest entry.");
            RelativePath(item!.Path);
            Hash(item.Sha256);
        }
        Require(values.Select(x => x.Path).Distinct(StringComparer.OrdinalIgnoreCase).Count() == values.Length, "Duplicate manifest path.");
    }

    public static void Validate(TaskCard card)
    {
        Require(card.SchemaVersion == 1, "Unsupported task schema version.");
        Identifier(card.TaskId); Text(card.ParentGoal); Identifier(card.SpecRevision); Commit(card.BaseCommit); Identifier(card.Owner);
        Require(Enum.IsDefined(card.Role) && (card.HandoffRole is null || Enum.IsDefined(card.HandoffRole.Value)), "Invalid role.");
        Require(card.Priority is >= 0 and <= 100, "Priority must be 0..100.");
        Strings(card.DependsOn, Identifier);
        Require(!card.DependsOn.Contains(card.TaskId, StringComparer.OrdinalIgnoreCase), "Task depends on itself.");
        Digests(card.Inputs);
        Require(card.Inputs.Length > 0, "At least one fixed input is required.");
        Strings(card.WritePaths, RelativePath); Strings(card.OutputPaths, RelativePath, true);
        Require(card.OutputPaths.All(p => card.WritePaths.Any(root => Within(p, root))), "Output is outside write scope.");
        Require(card.Resources is not null && card.Resources.All(Enum.IsDefined) && card.Resources.Distinct().Count() == card.Resources.Length, "Invalid resource list.");
        Strings(card.AcceptanceCriteria, Text, true); Strings(card.CheckIds, Identifier, true);
        Require(card.Limits is not null, "Missing limits.");
        Require(card.Limits!.TimeoutSeconds is >= 1 and <= 86400 && card.Limits.MaxAttempts is >= 1 and <= 16 && card.Limits.MaxAdditionalCostMicrosUsd >= 0, "Invalid execution limits.");
        Require(card.Workspace is not null, "Missing workspace plan.");
        Identifier(card.Workspace!.WorkspaceId); RelativePath(card.Workspace.TemporaryRoot); RelativePath(card.Workspace.EvidenceRoot);
        Strings(card.Workspace.CleanupPaths, RelativePath);
        Require(card.Workspace.CleanupPaths.All(p => Within(p, card.Workspace.TemporaryRoot)), "Cleanup outside owned temporary root.");
        Require(!Overlaps(card.Workspace.TemporaryRoot, card.Workspace.EvidenceRoot), "Evidence overlaps temporary root.");
        Require(!card.WritePaths.Any(p => Overlaps(p, card.Workspace.TemporaryRoot)), "Product writes overlap temporary root.");
    }

    public static void Validate(WorkerResult result)
    {
        Require(result.SchemaVersion == 1, "Unsupported result schema version.");
        Identifier(result.TaskId); Identifier(result.AttemptId); Identifier(result.SpecRevision); Hash(result.InputManifestHash);
        Require(Enum.IsDefined(result.Role) && Enum.IsDefined(result.Outcome), "Invalid result enum.");
        Digests(result.ArtifactManifest); Strings(result.ValidationRefs, RelativePath); Text(result.Handoff);
        Strings(result.ExternalTaskIds, Text);
        if (result.Outcome is WorkerOutcome.Blocked or WorkerOutcome.Failed or WorkerOutcome.Interrupted) Text(result.BlockReason);
        else Require(result.BlockReason is null, "Successful result has a block reason.");
    }

    public static void Validate(RuntimePolicy policy)
    {
        Require(policy.SchemaVersion == 1 && policy.AuthenticationMode == "SubscriptionOnly", "Unsupported policy/auth mode.");
        Require(!policy.AllowApiFallback, "API fallback is not implemented.");
        Require(!policy.AllowExtraSpending && policy.DailyAdditionalBudgetMicrosUsd == 0, "Paid dispatch requires a future budget adapter.");
        Require(policy.CodexConcurrency == 1 && policy.HeavyConcurrency == 1 && policy.GitConcurrency == 1, "Initial resource limits are one.");
        Require(policy.HeartbeatSeconds is >= 1 and <= 60 && policy.StaleAfterSeconds >= policy.HeartbeatSeconds * 3 && policy.StaleAfterSeconds <= 3600, "Invalid heartbeat policy.");
        Require(policy.DefectRetries is >= 0 and <= 2, "Invalid defect retry limit.");
        Require(policy.TransientBackoffSeconds is { Length: > 0 and <= 3 } && policy.TransientBackoffSeconds.All(x => x is >= 1 and <= 7200), "Invalid backoff.");
        Require(policy.ReviewTimeZone == "Asia/Seoul", "Initial review timezone is Asia/Seoul.");
        Require(policy.DailyReviewTime is null || TimeOnly.TryParseExact(policy.DailyReviewTime, "HH:mm", out _), "Review time must be HH:mm or null.");
    }
}
