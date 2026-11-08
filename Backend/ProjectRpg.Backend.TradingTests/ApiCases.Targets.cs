using System.Collections.Concurrent;
using System.Reflection;
using System.Text.Json.Nodes;
using Npgsql;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

internal static partial class ApiCases
{
    private static async Task TargetCases(Fixture f)
    {
        int before = checks;
        var a = await f.Person(); var b = await f.Person(); var other = await f.Person(); var alt = await f.Person(a.Account);
        async Task<string> Name(Person p) => (await f.Game.GetCharactersAsync(p.Account, default)).Single(c => c.CharacterId == p.Id).Name;
        string name = await Name(b), path = TargetRoute(a, name);
        await Get(f, "invalid-token", path, 401); await Get(f, f.Admin, path, 403);
        await Get(f, other.Token, path, 403);
        await Get(f, a.Token, $"/api/characters/{Guid.NewGuid():D}/trades/targets?name={name}", 403);
        foreach (var query in new[] { "", "?name=", "?name=a", "?name=" + new string('x', 21), "?name=%00ab",
                     "?name=" + new string(' ', 65).Replace(" ", "%20"), "?name=ab&name=cd", "?name=ab&extra=x", "?q=ab" })
            await Get(f, a.Token, Route(a) + "/targets" + query, 400);
        var found = (await Get(f, a.Token, TargetRoute(a, "  " + name.ToLowerInvariant() + "  ")))!;
        var target = found["targets"]!.AsArray().Single()!.AsObject();
        Check(target.Count == 2 && target["characterId"]!.GetValue<Guid>() == b.Id && target["name"]!.GetValue<string>() == name,
            "exact case-insensitive match returns only public character id and stored display name");
        Check(found.Count == 1 && !(found.ToJsonString().Contains(b.Account, StringComparison.Ordinal)), "no account, presence or private state in discovery");
        foreach (string input in new[] { name[..^1], "%_", "__", "DefinitelyMissing", await Name(a), await Name(alt) })
            Check((await Get(f, a.Token, TargetRoute(a, input)))!["targets"]!.AsArray().Count == 0, "no prefix/wildcard/own-account discovery");
        var unicode = await f.Game.CreateCharacterAsync(other.Account, "거래상대" + Guid.NewGuid().ToString("N")[..8], f.Clock.Now, default);
        Check((await Get(f, a.Token, TargetRoute(a, unicode.Name)))!["targets"]![0]!["characterId"]!.GetValue<Guid>() == unicode.CharacterId, "Unicode stored name");
        // Compare after creating the Unicode fixture: successful and empty reads must not write any game table.
        var fingerprint = await TargetFingerprint(f);
        long balanceA = await f.Balance(a), balanceB = await f.Balance(b);
        await Get(f, a.Token, path); await Get(f, a.Token, TargetRoute(a, "NotFound"));
        Check(fingerprint == await TargetFingerprint(f) && await f.Balance(a) == balanceA && await f.Balance(b) == balanceB,
            "target reads leave all database tables and assets unchanged");
        f.Options.Enabled = false; await Get(f, a.Token, path, 404); f.Options.Enabled = true;
        await f.Mode("Drain"); await Get(f, a.Token, path, 503); await f.Mode("Open");

        // Candidate discovery is advisory: an intervening invitation must still reject the old selection.
        var trade = Guid.NewGuid();
        var invited = (await Send(f, other.Token, Route(other, trade) + "/invite", new { requestId = Guid.NewGuid(), inviteeCharacterId = b.Id }))["session"]!.AsObject();
        Check((await Get(f, a.Token, path))!["targets"]!.AsArray().Count == 0, "busy target hidden");
        await Get(f, b.Token, TargetRoute(b, await Name(a)), 409);
        var stale = await Send(f, a.Token, Route(a, Guid.NewGuid()) + "/invite", new { requestId = Guid.NewGuid(), inviteeCharacterId = b.Id }, 409);
        Check(stale["error"]!.GetValue<string>() == "character_already_trading", "selected id never bypasses invitation busy check");
        await Send(f, other.Token, Route(other, trade) + "/cancel", Command(invited));
        Check((await Get(f, a.Token, path))!["targets"]!.AsArray().Count == 1, "cancelled session does not hide target");
        var waiting = await f.Game.CreateDungeonSessionAsync(b.Account, b.Id, "TargetFixture", "Normal", f.Clock.Now, f.Clock.Now.AddHours(1), default);
        Check((await Get(f, a.Token, path))!["targets"]!.AsArray().Count == 0, "Waiting admission lease hides target without an online-state claim");
        var lease = await Get(f, b.Token, TargetRoute(b, await Name(a)), 409);
        Check(lease!["error"]!.GetValue<string>() == "trade_requires_lobby", "leased actor cannot search");
        await Send(f, a.Token, Route(a, Guid.NewGuid()) + "/invite", new { requestId = Guid.NewGuid(), inviteeCharacterId = b.Id }, 409);
        await f.Game.AssignDungeonServerAsync(waiting.DungeonSessionId, "target-fixture-server", "127.0.0.1:1", f.Clock.Now, f.Clock.Now.AddMinutes(10), default);
        var credential = new AccessTokenService(f.Clock).Issue(TimeSpan.FromMinutes(10));
        await f.Game.StoreGameServerCredentialAsync(credential.TokenHash, "target-fixture-server", waiting.DungeonSessionId, credential.ExpiresAt, default);
        await Get(f, credential.Token, path, 403);

        var banned = await f.Person(); string bannedName = await Name(banned);
        Check((await Get(f, a.Token, TargetRoute(a, bannedName)))!["targets"]!.AsArray().Count == 1, "unrestricted target visible");
        await f.Restrict(banned);
        Check((await Get(f, a.Token, TargetRoute(a, bannedName)))!["targets"]!.AsArray().Count == 0, "restricted target indistinguishable from absent");
        await Get(f, banned.Token, TargetRoute(banned, name), 401);
        var remote = await f.Person(); string remoteName = await Name(remote);
        await TargetWorld(f, remote);
        Check((await Get(f, a.Token, TargetRoute(a, remoteName)))!["targets"]!.AsArray().Count == 0, "different world hidden");
        var dueActor = await f.Person(); var dueTarget = await f.Person(); var observer = await f.Person();
        string dueName = await Name(dueTarget);
        var due = await f.Store.ExecuteAsync(new(Guid.NewGuid(), Guid.NewGuid(), new(dueActor.Account, dueActor.Id),
            TradeCommandKind.Invite, Invitee: new(dueTarget.Account, dueTarget.Id), CurrencyCode: "Gold"), default);
        await f.MakeExpired(due.Session.State.TradeId);
        Check((await Get(f, observer.Token, TargetRoute(observer, dueName)))!["targets"]!.AsArray().Count == 1,
            "deadline hides no target even before an expiry worker closes its row");
        await TargetBudget(f);
        Console.WriteLine($"PASS target discovery: {checks - before} assertions; exact name/privacy, ownership, no writes, lease/Drain/restriction/busy/world, stale invitation and account search budget");
    }

    private static string TargetRoute(Person actor, string name) => Route(actor) + "/targets?name=" + Uri.EscapeDataString(name);

    private static async Task TargetBudget(Fixture f)
    {
        var actor = await f.Person(); var alt = await f.Person(actor.Account); var target = await f.Person();
        string name = (await f.Game.GetCharactersAsync(target.Account, default)).Single().Name;
        for (int i = 0; i < 30; i++) await Get(f, actor.Token, TargetRoute(actor, name));
        await Get(f, alt.Token, TargetRoute(alt, name), 429);
        // Exhausting discovery must not consume the independent invitation/recipient quota.
        var trade = Guid.NewGuid();
        var s = (await Send(f, actor.Token, Route(actor, trade) + "/invite", new { requestId = Guid.NewGuid(), inviteeCharacterId = target.Id }))["session"]!.AsObject();
        await Send(f, actor.Token, Route(actor, trade) + "/cancel", Command(s));
        var stranger = await f.Person(); await Get(f, stranger.Token, TargetRoute(stranger, name));
    }

    private static async Task TargetWorld(Fixture f, Person person)
    {
        var context = (await f.Game.GetCharacterEconomyContextAsync(person.Id, default))!;
        if (f.Data is not null)
        {
            await using var c = await f.Data.OpenConnectionAsync();
            await using var q = new NpgsqlCommand("UPDATE rosters SET world_id='target-other' WHERE roster_id=@id", c);
            q.Parameters.AddWithValue("id", context.RosterId); Check(await q.ExecuteNonQueryAsync() == 1, "controlled other-world fixture");
        }
        else
        {
            var field = typeof(InMemoryGameRepository).GetField("_rosters", BindingFlags.Instance | BindingFlags.NonPublic)!;
            var rosters = (ConcurrentDictionary<Guid, Roster>)field.GetValue(f.Game)!;
            rosters[context.RosterId] = rosters[context.RosterId] with { WorldId = "target-other" };
        }
    }

    private static async Task<string> TargetFingerprint(Fixture f)
    {
        if (f.Data is null) return "Memory assets checked separately";
        await using var c = await f.Data.OpenConnectionAsync();
        await using var tables = new NpgsqlCommand("SELECT table_name FROM information_schema.tables WHERE table_schema='public' AND table_type='BASE TABLE' ORDER BY table_name", c);
        List<string> names = [];
        await using (var rows = await tables.ExecuteReaderAsync()) while (await rows.ReadAsync()) names.Add(rows.GetString(0));
        List<string> hashes = [];
        foreach (string table in names)
        {
            string quoted = new NpgsqlCommandBuilder().QuoteIdentifier(table);
            await using var q = new NpgsqlCommand($"SELECT md5(coalesce(string_agg(to_jsonb(x)::text, E'\\n' ORDER BY to_jsonb(x)::text),'')) FROM public.{quoted} x", c);
            hashes.Add(table + ":" + (string)(await q.ExecuteScalarAsync())!);
        }
        return string.Join("\n", hashes);
    }
}
