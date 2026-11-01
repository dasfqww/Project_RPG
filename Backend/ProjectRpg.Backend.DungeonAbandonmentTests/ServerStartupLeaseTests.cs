using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using static Tests;

static class ServerStartupLeaseTests
{
    public static void CheckPolicy()
    {
        DateTimeOffset now = DateTimeOffset.UtcNow;
        DungeonSessionOptions defaults = new();
        Require(defaults.GetServerStartupLeaseExpiry(now) == now.AddMinutes(5)
            && defaults.ActiveLeaseSeconds == 90, "Startup and active defaults must remain separate.");
        foreach ((int value, int expected) in new[] { (int.MinValue, 30), (0, 30), (30, 30),
            (300, 300), (900, 900), (int.MaxValue, 900) })
        {
            DungeonSessionOptions options = new() { ServerStartupLeaseSeconds = value };
            Require(options.GetServerStartupLeaseExpiry(now) == now.AddSeconds(expected),
                "Startup configuration must have finite backend-owned bounds.");
        }
        Console.WriteLine("PASS startup lease: defaults and finite configuration bounds");
    }

    public static async Task RunSuite(string provider, Func<IGameRepository> create)
    {
        DungeonSessionOptions options = new();
        async Task<Fixture> Assign(bool claim)
        {
            Fixture f = await Fixture.Create(create(), DungeonSessionState.Waiting);
            DateTimeOffset expires = options.GetServerStartupLeaseExpiry(f.Now);
            DungeonSession? assigned = claim
                ? await f.Repo.ClaimNextDungeonSessionAsync(f.Server, "127.0.0.1:7777", f.Now, expires, default)
                : await f.Repo.AssignDungeonServerAsync(f.Session, f.Server, "127.0.0.1:7777", f.Now, expires, default);
            Require(assigned is not null && assigned.DungeonSessionId == f.Session && assigned.State == DungeonSessionState.Loading
                && assigned.ExpiresAt == expires, "Assignment did not retain the startup lease.");
            return f;
        }
        async Task<string> Credential(Fixture f)
        {
            string hash = Guid.NewGuid().ToString("N") + Guid.NewGuid().ToString("N");
            await f.Repo.StoreGameServerCredentialAsync(hash, f.Server, f.Session, f.Now.AddHours(12), default);
            return hash;
        }

        foreach (bool claim in new[] { false, true })
        {
            Fixture f = await Assign(claim);
            string hash = await Credential(f);
            DateTimeOffset coldBootCompleted = f.Now.AddSeconds(120);
            Require(await f.Repo.ResolveGameServerCredentialAsync(hash, coldBootCompleted, TimeSpan.Zero, default) is not null,
                "Valid startup credential was rejected after the active lease but before startup expiry.");
            DungeonSession started = await f.Repo.StartDungeonSessionAsync(f.Session, f.Server,
                coldBootCompleted, coldBootCompleted.AddSeconds(options.ActiveLeaseSeconds), default);
            Require(started.State == DungeonSessionState.InProgress
                && started.ExpiresAt == coldBootCompleted.AddSeconds(90), "Start did not switch to the shorter active lease.");
            Require(await f.Repo.ResolveGameServerCredentialAsync(hash, started.ExpiresAt, TimeSpan.Zero, default) is null,
                "The startup window leaked into active credential authority at exact expiry.");
            Console.WriteLine($"PASS {provider} startup lease: {(claim ? "claim" : "activate")} cold boot then active expiry");
        }

        {
            Fixture f = await Assign(false);
            string hash = await Credential(f);
            DateTimeOffset expires = options.GetServerStartupLeaseExpiry(f.Now);
            Require(await f.Repo.ResolveGameServerCredentialAsync(hash, expires.AddTicks(-10), TimeSpan.Zero, default) is not null,
                "Startup was rejected before its deadline.");
            Require(await f.Repo.ResolveGameServerCredentialAsync(hash, expires, TimeSpan.Zero, default) is null,
                "Startup authority survived its exact deadline.");
            bool rejected = false;
            try { await f.Repo.StartDungeonSessionAsync(f.Session, f.Server, expires, expires.AddSeconds(90), default); }
            catch (DungeonSessionStateConflictException) { rejected = true; }
            Require(rejected, "Expired startup was revived by a late start request.");
            Console.WriteLine($"PASS {provider} startup lease: exact deadline rejects credential and late start");
        }
    }
}
