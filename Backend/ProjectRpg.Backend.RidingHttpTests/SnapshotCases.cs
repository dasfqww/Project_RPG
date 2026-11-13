using System.Net.Http.Headers;
using System.Text.Json;
using System.Text.Json.Nodes;
using Npgsql;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Operations.Restrictions;
using ProjectRpg.Backend.Riding;
using ProjectRpg.Operations.Core.Restrictions;

static partial class Program
{
    sealed record Admission(Person Person, Guid Session, string Server, string Token, long Generation)
    {
        public string Route(long? generation = null) => $"/api/game-servers/sessions/{Session:D}/members/{Person.Character.CharacterId:D}/riding?connectionGeneration={generation ?? Generation}&catalogVersion=7&gameplayHash={Options().GameplayHash}";
    }
    static async Task<Admission> Admit(Fixture f, bool confirm = true, bool unlock = true)
    {
        var p = await f.New(); if (unlock) { await f.Post(p, p.Command(Guid.NewGuid())); await Put(f, p, PreferenceCommand(Guid.NewGuid())); }
        var now = DateTimeOffset.UtcNow; var session = await f.Game.CreateDungeonSessionAsync(p.Account, p.Character.CharacterId, "fixture", "Normal", now, now.AddHours(1), default);
        string server = "riding-snapshot-" + sequence;
        await f.Game.AssignDungeonServerAsync(session.DungeonSessionId, server, "127.0.0.1:1", now, now.AddHours(1), default);
        var tokens = new AccessTokenService(TimeProvider.System); var credential = tokens.Issue(TimeSpan.FromHours(1));
        await f.Game.StoreGameServerCredentialAsync(credential.TokenHash, server, session.DungeonSessionId, credential.ExpiresAt, default);
        long generation = await Consume(f, p, session.DungeonSessionId, server);
        if (confirm) Check(await f.Game.ConfirmDungeonMemberAdmissionAsync(session.DungeonSessionId, server, p.Account, p.Character.CharacterId, generation, DateTimeOffset.UtcNow, default), "Real admission confirmation");
        return new(p, session.DungeonSessionId, server, credential.Token, generation);
    }
    static async Task<long> Consume(Fixture f, Person p, Guid session, string server)
    {
        var token = new AccessTokenService(TimeProvider.System).Issue(TimeSpan.FromMinutes(5));
        Check(await f.Game.StoreJoinTicketAsync(token.TokenHash, p.Account, p.Character.CharacterId, session, token.ExpiresAt, default) == JoinTicketIssueResult.Issued, "Real join ticket issue");
        var result = await f.Game.ConsumeJoinTicketAsync(token.TokenHash, server, session, DateTimeOffset.UtcNow, default, Guid.NewGuid());
        Check(result is not null, "Real consumed ticket generation"); return result!.ConnectionGeneration;
    }
    static Task<JsonNode> Snapshot(Fixture f, Admission a, int status = 200, long? generation = null) => f.Raw(a.Token, a.Route(generation), null, status);
    static async Task RestrictedStatus(Fixture f, string token, string route, int status)
    {
        using var q = new HttpRequestMessage(HttpMethod.Get, route); q.Headers.Authorization = new("Bearer", token);
        using var r = await f.Client.SendAsync(q); Check((int)r.StatusCode == status, "Common authentication rejects restricted credential before route");
    }
    static async Task SnapshotCases(bool pg)
    {
        sequence = 3000; await using var f = await Fixture.Create(pg);
        await Case("snapshot_confirmed_scope", async () =>
        {
            var a = await Admit(f); var result = await Snapshot(f, a);
            Check(result["dungeonSessionId"]!.GetValue<string>() == a.Session.ToString("D") && result["characterId"]!.GetValue<string>() == a.Person.Character.CharacterId.ToString("D")
                && result["serverId"]!.GetValue<string>() == a.Server && result["connectionGeneration"]!.GetValue<string>() == "1", "Snapshot is bound to server/session/character/generation");
            Check(result["gameplayHash"]!.GetValue<string>() == Options().GameplayHash && result["catalogVersion"]!.GetValue<int>() == 7, "Pinned server gameplay manifest");
            Check(result["rosterRevision"]!.GetValue<string>() == "1" && result["preferencesRevision"]!.GetValue<string>() == "1"
                && result["mounts"]!.AsArray().Count == 1 && result["preferences"]!["selectedMountId"]!.GetValue<string>() == "horse", "Authoritative persisted collection/preferences");
            Check(DateTimeOffset.Parse(result["validUntilUtc"]!.GetValue<string>()) > DateTimeOffset.Parse(result["observedAtUtc"]!.GetValue<string>()) && result["ready"] is null, "Bounded observation, no fabricated gameplay Ready");
            var now = DateTimeOffset.UtcNow; await f.Game.StartDungeonSessionAsync(a.Session, a.Server, now, now.AddHours(1), default);
            await Snapshot(f, a);
            if (pg) await File.WriteAllTextAsync(Environment.GetEnvironmentVariable("PROJECT_RPG_RIDING_HTTP_PROOF")!, JsonSerializer.Serialize(new Probe(a.Token, a.Route(), "", result)));
        });
        await Case("snapshot_unowned_has_no_fallback", async () =>
        {
            var a = await Admit(f, unlock: false); var result = await Snapshot(f, a);
            Check(result["mounts"]!.AsArray().Count == 0 && result["rosterRevision"]!.GetValue<string>() == "0"
                && result["preferencesRevision"]!.GetValue<string>() == "0" && result["preferences"]!["selectedMountId"] is null, "Unregistered ownership stays empty; no development unlock/default mount");
            Check((await f.Items.FindAsync(a.Person.Item.ItemId, default))!.State.Quantity == 2, "Possessing a registration item does not grant a mount");
        });
        await Case("snapshot_principal_and_session_isolation", async () =>
        {
            var a = await Admit(f); var b = await Admit(f);
            await f.Raw(null, a.Route(), null, 401); await f.Raw(a.Person.Token, a.Route(), null, 403); await f.Raw(f.Admin, a.Route(), null, 403);
            await f.Raw(b.Token, a.Route(), null, 403);
            string missing = a.Route().Replace(a.Person.Character.CharacterId.ToString("D"), Guid.NewGuid().ToString("D"));
            var unknown = await f.Raw(a.Token, missing, null, 404);
            var other = await f.Raw(a.Token, a.Route().Replace(a.Person.Character.CharacterId.ToString("D"), b.Person.Character.CharacterId.ToString("D")), null, 404);
            Check(JsonNode.DeepEquals(unknown, other), "Foreign and absent member produce same result");
        });
        await Case("snapshot_confirmation_disconnect_and_generation", async () =>
        {
            var a = await Admit(f, false); await Snapshot(f, a, 404);
            Check(await f.Game.ConfirmDungeonMemberAdmissionAsync(a.Session, a.Server, a.Person.Account, a.Person.Character.CharacterId, a.Generation, DateTimeOffset.UtcNow, default), "Confirm pending generation");
            await Snapshot(f, a);
            await f.Game.RecordDungeonMemberDisconnectionAsync(a.Session, a.Server, a.Person.Account, a.Person.Character.CharacterId, a.Generation, DateTimeOffset.UtcNow, default);
            await Snapshot(f, a, 404);
            long next = await Consume(f, a.Person, a.Session, a.Server); Check(next == a.Generation + 1, "Reconnect advances generation");
            await Snapshot(f, a, 404); await Snapshot(f, a, 404, next);
            Check(await f.Game.ConfirmDungeonMemberAdmissionAsync(a.Session, a.Server, a.Person.Account, a.Person.Character.CharacterId, next, DateTimeOffset.UtcNow, default), "Confirm new socket clears only old disconnect");
            var current = await Snapshot(f, a, generation: next); Check(current["connectionGeneration"]!.GetValue<string>() == "2", "Only current confirmed generation reads snapshot");
            await Snapshot(f, a, 404);
        });
        await Case("snapshot_query_manifest_and_disabled", async () =>
        {
            var a = await Admit(f); string route = a.Route();
            foreach (string invalid in new[] { route.Split('?')[0], route+"&extra=1", route+"&connectionGeneration=1", route.Replace("connectionGeneration=1", "connectionGeneration=01"),
                route.Replace("connectionGeneration=1", "connectionGeneration=0"), route.Replace("catalogVersion=7", "catalogVersion=07"), route.Replace(Options().GameplayHash!, "bad") }) await f.Raw(a.Token, invalid, null, 400);
            await f.Raw(a.Token, route.Replace("catalogVersion=7", "catalogVersion=8"), null, 400);
            await using var off = await Fixture.Create(pg, new(), f); var disabled = await Snapshot(off, a, 404);
            Check(disabled["error"]!.GetValue<string>() == "RidingDisabled", "No startup catalog means no server snapshot");
        });
        await Case("snapshot_read_does_not_mutate_authority", async () =>
        {
            var a = await Admit(f); var before = await f.Game.GetDungeonMembershipSnapshotAsync(a.Session, a.Server, default);
            var view = await ((IRidingPreferencesStore)f.Game).GetRidingOwnerViewAsync(a.Person.Account, a.Person.Character.CharacterId, default);
            await Snapshot(f, a); await Snapshot(f, a);
            var after = await f.Game.GetDungeonMembershipSnapshotAsync(a.Session, a.Server, default);
            Check(JsonSerializer.Serialize(before!.Members) == JsonSerializer.Serialize(after!.Members) && before.State == after.State, "Snapshot does not confirm, reconnect, renew or revoke membership");
            var current = await ((IRidingPreferencesStore)f.Game).GetRidingOwnerViewAsync(a.Person.Account, a.Person.Character.CharacterId, default);
            Check(JsonSerializer.Serialize(view) == JsonSerializer.Serialize(current) && (await f.Items.FindAsync(a.Person.Item.ItemId, default))!.Revision == 2, "No preferences/item/roster writes");
        });
        await Case("snapshot_abandonment_and_restricted_server", async () =>
        {
            var a = await Admit(f); await f.Game.AbandonDungeonSessionAsync(a.Session, a.Person.Account, a.Person.Character.CharacterId, DateTimeOffset.UtcNow, default);
            var store = (IRidingAdmissionSnapshotStore)f.Game;
            Check(await store.GetRidingAdmissionSnapshotAsync(a.Server, a.Session, a.Person.Character.CharacterId, a.Generation, default) is null, "Abandonment cannot retain riding evidence");
            await RestrictedStatus(f, a.Token, a.Route(), 403);
        });
        await Case("snapshot_current_account_restriction", async () =>
        {
            var a = await Admit(f);
            IAccountRestrictionCommitter restrictions = pg ? new PostgresRestrictionStore(f.Data!, TimeProvider.System) : new MemoryRestrictionStore((InMemoryGameRepository)f.Game, TimeProvider.System);
            var result = await restrictions.CommitAsync(new(new(Guid.NewGuid(), a.Person.Account, 0, DateTimeOffset.UtcNow.AddHours(1), "Riding snapshot restriction"), "fixture", Guid.NewGuid(), null), default);
            Check(result.IsSuccess, "Real account restriction committed"); await Snapshot(f, a, 404);
        });
        if (pg) await Case("snapshot_pg_expired_live_lease", async () =>
        {
            var a = await Admit(f); var other = await Admit(f); await using var c = await f.Data!.OpenConnectionAsync();
            await using (var wrong = new NpgsqlCommand("UPDATE character_session_leases SET dungeon_session_id=@other WHERE character_id=@id", c))
            { wrong.Parameters.AddWithValue("other", other.Session); wrong.Parameters.AddWithValue("id", a.Person.Character.CharacterId); await wrong.ExecuteNonQueryAsync(); }
            await Snapshot(f, a, 404);
            await using (var restore = new NpgsqlCommand("UPDATE character_session_leases SET dungeon_session_id=@session WHERE character_id=@id", c))
            { restore.Parameters.AddWithValue("session", a.Session); restore.Parameters.AddWithValue("id", a.Person.Character.CharacterId); await restore.ExecuteNonQueryAsync(); }
            await using var expire = new NpgsqlCommand("UPDATE character_session_leases SET expires_at=clock_timestamp()-interval '1 second' WHERE character_id=@id", c); expire.Parameters.AddWithValue("id", a.Person.Character.CharacterId); await expire.ExecuteNonQueryAsync();
            await Snapshot(f, a, 404);
            Check(await ((IRidingAdmissionSnapshotStore)f.Game).GetRidingAdmissionSnapshotAsync(a.Server, a.Session, a.Person.Character.CharacterId, a.Generation, default) is null, "Expired lease fails without cleanup worker");
        });
    }
}
