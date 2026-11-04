using ProjectRpg.Operations.Core.Access;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Diagnostics;
using ProjectRpg.Operations.Core.Sessions;

internal static class CoreTests
{
    public static async Task RunAsync()
    {
        ProbeStore store = new();
        var denied = await new SearchCharacters(new Access(false), store).ExecuteAsync(
            new(CharacterLookupKind.SteamId, "not-an-id", new()), CancellationToken.None);
        Check.That(denied.Error == Failures.Forbidden && store.Reads == 0, "Authorization precedes validation and storage.");
        var search = new SearchCharacters(new Access(true), store);
        foreach (string id in new[] { "0", "01", "-1", " 12", "12 ", "18446744073709551616", "١٢" })
            Check.That(!(await search.ExecuteAsync(new(CharacterLookupKind.SteamId, id, new()), default)).IsSuccess,
                "Reject noncanonical/out-of-range account ID.");
        Check.That(store.Reads == 0, "Invalid searches do not reach storage.");
        Check.That((await search.ExecuteAsync(new(CharacterLookupKind.SteamId, "18446744073709551615", new()), default)).Value.Items.Count == 0,
            "Valid empty search is success; uint64 precision is preserved.");
        Check.That((await new GetCharacterSummary(new Access(true), store).ExecuteAsync(Guid.NewGuid(), default)).Error == Failures.CharacterMissing,
            "A missing single character differs from an empty search.");
        Check.That(!new CharacterLookup(CharacterLookupKind.SteamId, "1", new(201)).IsValid
            && !new CharacterLookup(CharacterLookupKind.SteamId, "1", new(1, new(Guid.NewGuid()))).IsValid,
            "Bound page sizes and reject incomplete keyset positions.");
        DateTimeOffset now = DateTimeOffset.UtcNow;
        Check.That(SessionEvidence.Observe(now, now, now.AddMinutes(1), now.AddMinutes(1)) == LeaseTiming.ObservedExpired
            && SessionEvidence.Observe(now, now.AddMinutes(1), null, now.AddMinutes(1)) == LeaseTiming.LeaseMissing,
            "Lease evidence does not invent online status.");
        OperationLogQuery query = new(now.AddHours(-1), now, "backend", new Dictionary<string,string> { ["eventName"] = "ops.http.completed" });
        Check.That(query.IsValid && !(query with { FromUtc = now.AddDays(-2) }).IsValid
            && !(query with { Filters = new Dictionary<string,string> { ["path"] = "../../secret" } }).IsValid,
            "Log queries require bounded, known filters.");
        Check.That(typeof(ICharacterReadStore).Assembly.GetReferencedAssemblies().All(a => a.Name is not null
            && !a.Name.Contains("Backend") && !a.Name.Contains("AspNet") && !a.Name.Contains("Npgsql") && !a.Name.Contains("Serilog")),
            "Core has no host or provider dependency.");
        Console.WriteLine("PASS Core: authorization, canonical IDs, empty/missing, pagination, lease evidence, bounded logs, dependency direction.");
    }
    private sealed class Access(bool allowed) : IOperatorAccess
    {
        public Task<Result<OperatorActor>> AuthorizeAsync(OpsCapability capability, CancellationToken cancellationToken) =>
            Task.FromResult(allowed ? Result<OperatorActor>.Success(new("github:1")) : Result<OperatorActor>.Fail(Failures.Forbidden));
    }
    private sealed class ProbeStore : ICharacterReadStore
    {
        public int Reads { get; private set; }
        public Task<Result<Page<CharacterHeader>>> SearchAsync(CharacterLookup query, CancellationToken cancellationToken)
        { Reads++; return Task.FromResult(Result<Page<CharacterHeader>>.Success(new([], false, null))); }
        public Task<Result<CharacterHeader>> FindAsync(Guid characterId, CancellationToken cancellationToken) =>
            Task.FromResult(Result<CharacterHeader>.Fail(Failures.CharacterMissing));
    }
}
