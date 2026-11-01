using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using static Tests;

static class DungeonDepartureTests
{
    static async Task Reject(Func<Task> action, string reason)
    {
        bool rejected = false;
        try { await action(); }
        catch (Exception exception) when (exception is DungeonDepartureConflictException
            or DungeonSessionMembershipException or DungeonSessionNotJoinableException or DungeonSessionStateConflictException)
        { rejected = true; }
        Require(rejected, reason);
    }

    public static async Task Run(string provider, IGameRepository repo)
    {
        Fixture f = await Fixture.Create(repo, DungeonSessionState.Waiting);
        DungeonSession initial = await f.Read();
        Require(initial.LeaderCharacterId == f.A.CharacterId && initial.LobbyRevision == 2, "Creator must be leader; join must revise lobby.");
        Guid request = Guid.NewGuid();
        Task<DungeonSession> Set(Fixture fixture, DungeonSession basis, Guid id, bool cancel = false, GameCharacter? actor = null, int min = 2) =>
            repo.SetDungeonDepartureAsync(fixture.Session, (actor ?? fixture.A).SteamId, (actor ?? fixture.A).CharacterId,
                id, basis.LobbyRevision, cancel, min, fixture.Now, default);
        Task<DungeonSession?> Claim(string server) => repo.ClaimNextDungeonSessionAsync(server, "127.0.0.1:7777", f.Now,
            f.Expires, default, [new("Dungeon.Test", "Hard")], requireDeparture: true, minimumPartySize: 2);

        Require(await Claim("before-request") is null, "Recruiting party was claimed.");
        await Reject(() => repo.AssignDungeonServerAsync(f.Session, f.Server, "127.0.0.1:7777", f.Now, f.Expires,
            default, requireDeparture: true, minimumPartySize: 2), "Manual activate bypassed departure.");
        await Reject(() => Set(f, initial, request, actor: f.B), "Nonleader requested departure.");
        await Reject(() => Set(f, initial, request, min: 3), "Minimum party size was not enforced.");
        await Reject(() => repo.SetDungeonDepartureAsync(f.Session, f.B.SteamId, f.A.CharacterId, request,
            initial.LobbyRevision, false, 2, f.Now, default), "A forged leader character was authorized.");
        Console.WriteLine($"PASS {provider} departure: recruiting cannot allocate; leader/ownership/minimum guards");

        DungeonSession queued = await Set(f, initial, request);
        DungeonSession replay = await Set(f, initial, request);
        Require(queued.DepartureRequestedAt == replay.DepartureRequestedAt && queued.LobbyRevision == replay.LobbyRevision
            && replay.ExpiresAt == initial.ExpiresAt, "Request replay changed revision or extended leases.");
        await Reject(() => repo.JoinDungeonSessionAsync(f.Session, f.C.SteamId, f.C.CharacterId, 4, f.Now, f.Expires, default),
            "Roster was expanded after departure request.");
        Require(await repo.GetActiveDungeonSessionForCharacterAsync(f.C.CharacterId, f.C.SteamId, f.Now, default) is null,
            "Rejected join leaked a character lease.");
        DungeonSession cancelled = await Set(f, initial, request, cancel: true);
        Require((await Set(f, initial, request, cancel: true)).LobbyRevision == cancelled.LobbyRevision, "Cancel replay mutated revision.");
        await Reject(() => Set(f, initial, request), "Cancelled request replay requeued the party.");
        // UUID reuse is still fenced by the immutable original revision pair.
        DungeonSession requeued = await Set(f, cancelled, request);
        await Reject(() => Set(f, initial, request, cancel: true), "Late cancel cancelled a newer command with the same UUID.");
        Require((await f.Read()).DepartureRequestRevision == cancelled.LobbyRevision, "Late command changed queue identity.");
        Console.WriteLine($"PASS {provider} departure: stable request/cancel retries, no renewal, roster lock, ABA fencing");

        DungeonSession left = await repo.LeaveDungeonSessionAsync(f.Session, f.B.SteamId, f.B.CharacterId, f.Now, default);
        Require(left.DepartureRequestedAt is null && left.LeaderCharacterId == f.A.CharacterId && left.Members.Count == 1,
            "Member departure must cancel queue without removing the remaining leader.");
        await Reject(() => Set(f, requeued, Guid.NewGuid()), "Stale roster revision requested departure.");
        await repo.JoinDungeonSessionAsync(f.Session, f.B.SteamId, f.B.CharacterId, 4, f.Now, f.Expires, default);
        DungeonSession promoted = await repo.LeaveDungeonSessionAsync(f.Session, f.A.SteamId, f.A.CharacterId, f.Now, default);
        Require(promoted.LeaderCharacterId == f.B.CharacterId && promoted.Members.Count == 1 && promoted.State == DungeonSessionState.Waiting,
            "Leader leave did not preserve/promote the remaining party.");
        await Reject(() => Set(f, promoted, Guid.NewGuid()), "Former leader retained departure authority.");
        Console.WriteLine($"PASS {provider} departure: leave auto-cancels, leader promotion and stale-owner/roster denial");

        Fixture race = await Fixture.Create(repo, DungeonSessionState.Waiting);
        DungeonSession basis = await race.Read(); Guid raceId = Guid.NewGuid();
        await Set(race, basis, raceId);
        bool cancelSucceeded = false;
        async Task CancelRace()
        {
            try { await Set(race, basis, raceId, cancel: true); cancelSucceeded = true; }
            catch (DungeonSessionStateConflictException) { }
        }
        Task<DungeonSession?> claimTask = Claim("race-server");
        await Task.WhenAll(claimTask, CancelRace());
        DungeonSession final = await race.Read();
        if (claimTask.Result is null)
            Require(cancelSucceeded && final.State == DungeonSessionState.Waiting && final.DepartureRequestedAt is null, "Cancel-winning race was not atomic.");
        else
        {
            Require(!cancelSucceeded && claimTask.Result.DungeonSessionId == race.Session && final.State == DungeonSessionState.Loading
                && final.DepartureRequestedAt.HasValue, "Claim-winning race was rolled back by cancellation.");
            Require((await Set(race, basis, raceId)).State == DungeonSessionState.Loading, "Committed request replay after claim was lost.");
        }
        Console.WriteLine($"PASS {provider} departure: atomic claim/cancel race, winner={(cancelSucceeded ? "cancel" : "claim")}");
    }

    public static async Task CheckPersistence(PrivateSchema schema)
    {
        IGameRepository repo = schema.Repository();
        Fixture f = await Fixture.Create(repo, DungeonSessionState.Waiting);
        DungeonSession original = await f.Read(); Guid id = Guid.NewGuid();
        DungeonSession queued = await repo.SetDungeonDepartureAsync(f.Session, f.A.SteamId, f.A.CharacterId, id,
            original.LobbyRevision, false, 2, f.Now, default);
        DungeonSession restored = (await schema.Repository().GetDungeonSessionAsync(f.Session, f.Now, default))!;
        Require(restored.LeaderCharacterId == queued.LeaderCharacterId && restored.LobbyRevision == queued.LobbyRevision
            && restored.DepartureRequestId == id && restored.DepartureRequestRevision == original.LobbyRevision
            && restored.DepartureRequestedAt == queued.DepartureRequestedAt, "Departure/leadership did not survive repository recreation.");
        await schema.Execute($"UPDATE dungeon_sessions SET leader_character_id = NULL, lobby_revision = 0, departure_request_id = NULL, departure_request_revision = NULL, departure_requested_at = NULL WHERE dungeon_session_id = '{f.Session:D}';");
        Require(await schema.Repository().ClaimNextDungeonSessionAsync("legacy", "127.0.0.1:7777", f.Now, f.Expires,
            default, [new("Dungeon.Test", "Hard")], requireDeparture: true) is null, "Unowned legacy lobby was automatically allocated.");
        DungeonSession legacy = (await schema.Repository().GetDungeonSessionAsync(f.Session, f.Now, default))!;
        Require(legacy.Members.Count == 2 && legacy.State == DungeonSessionState.Waiting, "Legacy preservation failed.");
        Console.WriteLine("PASS Postgres departure: persisted metadata and legacy Waiting preservation without inferred leader/queue");
    }
}
