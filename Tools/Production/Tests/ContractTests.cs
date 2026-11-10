using ProjectRpg.Production.Contracts;

internal static class ContractTests
{
    internal static TaskCard Example(string root) => ContractJson.Read<TaskCard>(File.ReadAllText(Path.Combine(root, "Production/Contracts/Examples/seal-planner.json")));

    internal static void Run(string root)
    {
        var card = Example(root);
        Check.That(card.TaskId == "seal-planner-v1", "Read example.");
        var canonical = ContractJson.Write(card);
        Check.That(ContractJson.Write(ContractJson.Read<TaskCard>(canonical)) == canonical, "Canonical roundtrip.");
        var policy = ContractJson.Read<RuntimePolicy>(File.ReadAllText(Path.Combine(root, "Production/Policies/local.json")));
        Check.That(policy.DailyReviewTime is null && !policy.AllowExtraSpending, "Unresolved time and no paid dispatch.");
        foreach (var path in new[] { "../other", "C:/other", "/absolute", "foo\\bar", "foo/../bar", "foo//bar", ".git/config", "x/CON.txt", "x/NUL", "a.", "a ", "a/*", "a/\n" })
            Check.Reject(() => ContractRules.RelativePath(path), $"Unsafe path accepted: {path}");
        Check.That(ContractRules.Overlaps("Content/Pilot", "content/pilot/Mesh.uasset"), "Case insensitive subtree collision.");
        Check.That(!ContractRules.Overlaps("Content/Pilot", "Content/Pilot2"), "Segment boundary.");
        Check.Reject(() => ContractRules.Validate(card with { DependsOn = [card.TaskId] }), "Self dependency accepted.");
        Check.Reject(() => ContractRules.Validate(card with { WritePaths = ["Docs/x", "docs/X"] }), "Case collision accepted.");
        Check.Reject(() => ContractRules.Validate(card with { OutputPaths = ["Source/other.cpp"] }), "Output outside scope accepted.");
        Check.Reject(() => ContractRules.Validate(card with { Workspace = card.Workspace with { CleanupPaths = ["Content"] } }), "Cleanup escape accepted.");
        Check.Reject(() => ContractRules.Validate(card with { Workspace = card.Workspace with { EvidenceRoot = card.Workspace.TemporaryRoot } }), "Evidence deletion accepted.");
        Check.Reject(() => ContractRules.Validate(card with { Limits = card.Limits with { MaxAttempts = 0 } }), "Zero attempts accepted.");
        Check.Reject(() => ContractRules.Validate(card with { Inputs = [null!] }), "Null array element accepted.");
        Check.Reject(() => ContractRules.Validate(card with { SchemaVersion = 2 }), "Future version accepted.");
        Check.Reject(() => ContractJson.Read<TaskCard>(canonical.Replace("\"priority\": 50", "\"priority\": 50, \"priority\": 51")), "Duplicate JSON accepted.");
        Check.Reject(() => ContractJson.Read<TaskCard>(canonical.Replace("\"owner\": \"production\",", "")), "Missing required field accepted.");
        Check.Reject(() => ContractJson.Read<TaskCard>(canonical.Replace("\"owner\": \"production\"", "\"owner\": null")), "Null field accepted.");
        Check.Reject(() => ContractJson.Read<TaskCard>(canonical.Replace("\"priority\": 50", "\"priority\": 50, \"unexpected\": true")), "Unknown field accepted.");
        Check.Reject(() => ContractJson.Read<TaskCard>(canonical.Replace("\"role\": \"planner\"", "\"role\": 0")), "Integer enum accepted.");
        Check.Reject(() => ContractRules.Validate(policy with { AllowApiFallback = true }), "API fallback accepted.");
        Check.Reject(() => ContractRules.Validate(policy with { AllowExtraSpending = true }), "Paid dispatch accepted.");
        Check.Reject(() => ContractRules.Validate(policy with { DailyReviewTime = "25:00" }), "Invalid review time accepted.");
        var schemas = new Dictionary<string, string>
        {
            ["task.schema.json"] = ContractJson.Schema<TaskCard>(),
            ["worker-result.schema.json"] = ContractJson.Schema<WorkerResult>(),
            ["policy.schema.json"] = ContractJson.Schema<RuntimePolicy>()
        };
        foreach (var (name, schema) in schemas)
            Check.That(File.ReadAllText(Path.Combine(root, "Production/Contracts", name)).Replace("\r\n", "\n") == schema, "Schema drift: " + name);
    }
}
