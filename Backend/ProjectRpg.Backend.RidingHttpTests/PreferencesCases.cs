using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Npgsql;
using ProjectRpg.Backend.Riding;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

static partial class Program
{
    sealed class PreferencesCancelClock : TimeProvider
    {
        public Action? OnRead;
        public override DateTimeOffset GetUtcNow() { OnRead?.Invoke(); return DateTimeOffset.UtcNow; }
    }
    static object PreferenceCommand(Guid id, string revision = "0", string? selected = "horse", string[]? favorites = null, string mode = "Specific", string? mount = "horse") =>
        new { requestId = id, expectedRevision = revision, selectedMountId = selected, favoriteMountIds = favorites ?? new[] { "horse" }, shortcut = new { mode, mountId = mount } };
    static Task<JsonNode> Put(Fixture f, Person p, object body, int status = 200) => f.Raw(p.Token, p.Root + "/preferences", JsonSerializer.Serialize(body, Json), status, method: HttpMethod.Put);
    static void BadPreference(Action action, RidingFailure failure = RidingFailure.InvalidRequest)
    { bool rejected = false; try { action(); } catch (RidingRuleException e) when (e.Code == failure) { rejected = true; } Check(rejected, "Invalid preference rejected: " + failure); }
    static async Task PreferencesCases(bool pg)
    {
        sequence = 1000;
        await Case("preferences_canonical_rules", async () =>
        {
            var actor = new RidingRegistrationActor(Guid.Parse("10000000-0000-0000-0000-000000000001"), Guid.Parse("20000000-0000-0000-0000-000000000001"));
            var command = new RidingPreferencesCommand(Guid.Parse("30000000-0000-0000-0000-000000000001"), 0, null, ["wolf", "horse"], new(RidingShortcutMode.RandomFavorites, null));
            var culture = CultureInfo.CurrentCulture;
            try
            {
                CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("ar-SA");
                string expected = "ProjectRPG.Riding.Preferences/v1\ncharacter=10000000-0000-0000-0000-000000000001\nroster=20000000-0000-0000-0000-000000000001\nrequest=30000000-0000-0000-0000-000000000001\nrevision=0\nselected=-\nfavorites=horse,wolf\nmode=RandomFavorites\nmount=-\n";
                Check(RidingPreferencesRules.Fingerprint(actor, command) == Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(expected))), "Independent invariant canonical byte projection");
                Check(RidingPreferencesRules.Fingerprint(actor, command) == RidingPreferencesRules.Fingerprint(actor, command with { FavoriteMountIds = ["horse", "wolf"] }), "Favorites order has identical meaning");
            }
            finally { CultureInfo.CurrentCulture = culture; }
            foreach (var invalid in new[] { command with { FavoriteMountIds = ["horse", "horse"] }, command with { FavoriteMountIds = Enumerable.Range(0, 33).Select(i => "m" + i).ToArray() },
                command with { SelectedMountId = "" }, command with { Shortcut = new(RidingShortcutMode.Specific, null) }, command with { Shortcut = new(RidingShortcutMode.Selected, "horse") },
                command with { Shortcut = new((RidingShortcutMode)99, null) }, command with { ExpectedRevision = long.MaxValue } }) BadPreference(() => RidingPreferencesRules.Normalize(invalid));
            var catalog = Options().BuildCatalog()!; var owned = new RidingCollection(actor.RosterId, 1, [new("horse", 1, DateTimeOffset.UtcNow, Guid.NewGuid())]);
            var empty = new RidingPreferencesCommand(Guid.NewGuid(), 0, null, [], new(RidingShortcutMode.RandomFavorites, null));
            var planned = RidingPreferencesRules.Plan(empty, 0, catalog, owned);
            Check(planned.Revision == 1 && planned.SelectedMountId is null && planned.FavoriteMountIds.Count == 0 && planned.Shortcut.MountId is null, "Empty means no candidates, no automatic selection");
            BadPreference(() => RidingPreferencesRules.Plan(empty, 1, catalog, owned), RidingFailure.RevisionConflict);
            BadPreference(() => RidingPreferencesRules.Plan(empty with { SelectedMountId = "wolf" }, 0, catalog, owned), RidingFailure.DefinitionMismatch);
            BadPreference(() => RidingPreferencesRules.Plan(empty with { ExpectedRevision = long.MaxValue - 1 }, long.MaxValue, catalog, owned), RidingFailure.RevisionConflict);
            await Task.CompletedTask;
        });
        await using var f = await Fixture.Create(pg);
        await Case("preferences_save_receipt_and_assets", async () =>
        {
            var p = await f.New(); await f.Post(p, p.Command(Guid.NewGuid())); var id = Guid.NewGuid(); var body = PreferenceCommand(id);
            var initial = await f.Get(p); Check(initial["preferencesRevision"]!.GetValue<string>() == "0", "Unsaved revision0");
            var saved = await Put(f, p, body); Check(saved["outcome"]!.GetValue<string>() == "PreferencesSaved" && saved["receipt"]!["preferencesRevision"]!.GetValue<string>() == "1", "Atomic whole settings replacement");
            var current = await f.Get(p); Check(current["preferences"]!["selectedMountId"]!.GetValue<string>() == "horse" && current["preferences"]!["shortcut"]!["mode"]!.GetValue<string>() == "Specific", "Actual stored selection/shortcut");
            var replay = await Put(f, p, body); Check(replay["outcome"]!.GetValue<string>() == "AlreadyApplied" && JsonNode.DeepEquals(saved["receipt"], replay["receipt"]), "Original result is immutable");
            var receipt = await f.Get(p, p.Root + "/requests/" + id); Check(JsonNode.DeepEquals(receipt, saved["receipt"]), "Shared requests route dispatches settings receipt");
            Check((await f.Items.FindAsync(p.Item.ItemId, default))!.Revision == 2 && current["rosterRevision"]!.GetValue<string>() == "1", "Settings consume no item or roster revision");
            if (pg) await File.WriteAllTextAsync(Environment.GetEnvironmentVariable("PROJECT_RPG_RIDING_HTTP_PROOF")!, JsonSerializer.Serialize(new Probe(p.Token, p.Root, JsonSerializer.Serialize(body, Json), receipt)));
        });
        await Case("preferences_cas_and_same_request_concurrency", async () =>
        {
            var p = await f.New(); var store = (IRidingPreferencesStore)f.Game; var command = new RidingPreferencesCommand(Guid.NewGuid(), 0, null, [], new(RidingShortcutMode.Selected, null));
            var results = await Task.WhenAll(Enumerable.Range(0, 8).Select(_ => store.SaveRidingPreferencesAsync(p.Account, p.Character.CharacterId, command, Options().BuildCatalog()!, default)));
            Check(results.Count(r => !r.Replayed) == 1 && results.All(r => r.Receipt.Preferences.Revision == 1), "Concurrent identical requests commit once");
            async Task<string> Save(RidingPreferencesCommand c)
            { try { await store.SaveRidingPreferencesAsync(p.Account, p.Character.CharacterId, c, Options().BuildCatalog()!, default); return "saved"; } catch (RidingRuleException e) when (e.Code == RidingFailure.RevisionConflict) { return "stale"; } }
            var race = await Task.WhenAll(Save(command with { RequestId = Guid.NewGuid(), ExpectedRevision = 1 }), Save(command with { RequestId = Guid.NewGuid(), ExpectedRevision = 1, Shortcut = new(RidingShortcutMode.RandomFavorites, null) }));
            Check(race.Count(s => s == "saved") == 1 && race.Count(s => s == "stale") == 1, "CAS prevents lost updates");
            var view = await f.Get(p); Check(view["preferencesRevision"]!.GetValue<string>() == "2", "Only one successor revision");
            var original = await f.Get(p, p.Root + "/requests/" + command.RequestId); Check(original["preferencesRevision"]!.GetValue<string>() == "1", "Old receipt is not latest settings");
        });
        await Case("preferences_character_isolation_and_clear", async () =>
        {
            var p = await f.New(); var alt = await f.New(p.Account); await f.Post(p, p.Command(Guid.NewGuid())); await Put(f, p, PreferenceCommand(Guid.NewGuid()));
            var view = await f.Get(alt); Check(view["mounts"]!.AsArray().Count == 1 && view["preferencesRevision"]!.GetValue<string>() == "0" && view["preferences"]!["selectedMountId"] is null, "Roster shared, settings per character");
            await Put(f, alt, PreferenceCommand(Guid.NewGuid(), selected: null, favorites: [], mode: "RandomFavorites", mount: null));
            Check((await f.Get(p))["preferences"]!["selectedMountId"]!.GetValue<string>() == "horse", "Alternate settings cannot change owner selection");
            await Put(f, p, PreferenceCommand(Guid.NewGuid(), "1", null, [], "Selected", null));
            var cleared = await f.Get(p); Check(cleared["preferencesRevision"]!.GetValue<string>() == "2" && cleared["preferences"]!["favoriteMountIds"]!.AsArray().Count == 0 && cleared["preferences"]!["selectedMountId"] is null, "Explicit empty replacement clears all settings");
            var foreign = await f.New(); await f.Raw(foreign.Token, p.Root + "/preferences", JsonSerializer.Serialize(PreferenceCommand(Guid.NewGuid()), Json), 403, method: HttpMethod.Put);
        });
        await Case("preferences_shared_request_namespace", async () =>
        {
            var p = await f.New(); var registerId = Guid.NewGuid(); await f.Post(p, p.Command(registerId));
            await Put(f, p, PreferenceCommand(registerId), 409);
            var id = Guid.NewGuid(); await Put(f, p, PreferenceCommand(id));
            await f.Post(p, new { requestId = id, itemId = p.Item.ItemId, expectedItemRevision = "2", catalogVersion = 7 }, 409);
            var receipt = await f.Get(p, p.Root + "/requests/" + registerId); Check(receipt["operation"]!.GetValue<string>() == RidingRegistrationRules.Operation, "Registration receipt remains readable");
            var alt = await f.New(p.Account); await f.Get(alt, alt.Root + "/requests/" + id, 404);
            await Put(f, p, PreferenceCommand(id, "1"), 409);
            Check((await f.Items.FindAsync(p.Item.ItemId, default))!.State.Quantity == 1, "Cross-operation ID conflicts never consume again");
        });
        await Case("preferences_strict_shape_and_references", async () =>
        {
            var p = await f.New(); string json = JsonSerializer.Serialize(PreferenceCommand(Guid.NewGuid()), Json);
            await Put(f, p, PreferenceCommand(Guid.NewGuid()), 400);
            foreach (var bad in new[] { PreferenceCommand(Guid.NewGuid(), selected: ""), PreferenceCommand(Guid.NewGuid(), favorites: ["horse", "horse"]),
                PreferenceCommand(Guid.NewGuid(), favorites: Enumerable.Range(0, 33).Select(i => "m"+i).ToArray()), PreferenceCommand(Guid.NewGuid(), mode: "Specific", mount: null),
                PreferenceCommand(Guid.NewGuid(), mode: "Selected", mount: "horse"), PreferenceCommand(Guid.NewGuid(), revision: "00") }) await Put(f, p, bad, 400);
            await f.Raw(p.Token, p.Root + "/preferences", json[..^1] + ",\"requestId\":\"" + Guid.NewGuid() + "\"}", 400, method: HttpMethod.Put);
            await f.Raw(p.Token, p.Root + "/preferences", json[..^1] + ",\"other\":1}", 400, method: HttpMethod.Put);
            var other = await f.New(); await f.Raw(other.Token, other.Root + "/preferences", json + new string(' ', 16384), 413, chunked: true, method: HttpMethod.Put);
            await f.Raw(f.Admin, other.Root + "/preferences", "{}", 403, method: HttpMethod.Put);
            await f.Raw(null, other.Root + "/preferences", "{}", 401, method: HttpMethod.Put);
            Check((await f.Get(p))["preferencesRevision"]!.GetValue<string>() == "0", "All invalid requests leave revision0");
        });
        await Case("preferences_lobby_disable_and_recovery", async () =>
        {
            var p = await f.New(); var id = Guid.NewGuid(); var body = PreferenceCommand(id, selected: null, favorites: [], mode: "RandomFavorites", mount: null);
            var saved = await Put(f, p, body); var now = DateTimeOffset.UtcNow;
            await f.Game.CreateDungeonSessionAsync(p.Account, p.Character.CharacterId, "fixture", "Normal", now, now.AddHours(1), default);
            Check((await Put(f, p, body))["outcome"]!.GetValue<string>() == "AlreadyApplied", "Active dungeon cannot invalidate original success");
            await Put(f, p, PreferenceCommand(Guid.NewGuid(), "1", null, [], "Selected", null), 409);
            await using var off = await Fixture.Create(pg, new(), f);
            var replay = await Put(off, p, body); Check(JsonNode.DeepEquals(replay["receipt"], saved["receipt"]), "Disabled catalog preserves preferences receipt");
            await Put(off, p, PreferenceCommand(Guid.NewGuid(), "1", null, [], "Selected", null), 404);
            var view = await off.Get(p); Check(!view["preferencesEnabled"]!.GetValue<bool>() && view["preferencesReady"]!.GetValue<bool>() && view["preferencesRevision"]!.GetValue<string>() == "1", "Stored settings stay readable with writes disabled");
        });
        if (!pg) await Case("preferences_memory_cancel_rollback", async () =>
        {
            var clock = new PreferencesCancelClock(); var gate = new InMemoryTransactionGate(clock); var items = new InMemoryItemRepository(gate);
            var game = new InMemoryGameRepository(gate, items, clock); const string account = "76561600000000001"; var now = DateTimeOffset.UtcNow;
            await game.UpsertAccountAsync(new(account, account, false, false), now, default);
            var character = await game.CreateCharacterAsync(account, "PrefsCancel", now, default);
            var command = new RidingPreferencesCommand(Guid.NewGuid(), 0, null, [], new(RidingShortcutMode.Selected, null));
            using var cancel = new CancellationTokenSource(); clock.OnRead = cancel.Cancel;
            bool canceled = false;
            try { await game.SaveRidingPreferencesAsync(account, character.CharacterId, command, Options().BuildCatalog()!, cancel.Token); }
            catch (OperationCanceledException) { canceled = true; }
            clock.OnRead = null;
            Check(canceled, "Cancellation observed after initial token check via owned transaction clock");
            Check((await game.GetRidingOwnerViewAsync(account, character.CharacterId, default)).Preferences.Revision == 0
                && await game.GetRidingCommandReceiptAsync(account, character.CharacterId, command.RequestId, default) is null, "Memory settings and parent receipt rolled back together");
            Check(!(await game.SaveRidingPreferencesAsync(account, character.CharacterId, command, Options().BuildCatalog()!, default)).Replayed, "Original request retries after cancellation rollback");
        });
        if (pg) await Case("preferences_pg_atomic_rollback", async () =>
        {
            var p = await f.New(); await f.Post(p, p.Command(Guid.NewGuid())); var id = Guid.NewGuid();
            async Task Sql(string sql) { await using var c = await f.Data!.OpenConnectionAsync(); await using var q = new NpgsqlCommand(sql, c); await q.ExecuteNonQueryAsync(); }
            await Sql($"CREATE FUNCTION prefs_test_fail() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN IF NEW.request_id='{id:D}'::uuid THEN RAISE EXCEPTION 'prefs_rollback' USING ERRCODE='23514'; END IF; RETURN NEW; END $$; CREATE TRIGGER prefs_test_fail BEFORE INSERT ON riding_command_receipts FOR EACH ROW EXECUTE FUNCTION prefs_test_fail()");
            try { await Put(f, p, PreferenceCommand(id), 503); }
            finally { await Sql("DROP TRIGGER prefs_test_fail ON riding_command_receipts; DROP FUNCTION prefs_test_fail()"); }
            var view = await f.Get(p); Check(view["preferencesRevision"]!.GetValue<string>() == "0" && view["preferences"]!["favoriteMountIds"]!.AsArray().Count == 0, "Settings and favorites roll back after parent receipt insert failure");
            await f.Get(p, p.Root + "/requests/" + id, 404);
            Check((await Put(f, p, PreferenceCommand(id)))["outcome"]!.GetValue<string>() == "PreferencesSaved", "Same command retries after rollback");
            Check((await f.Items.FindAsync(p.Item.ItemId, default))!.State.Quantity == 1, "Rollback/retry never touches inventory");
        });
    }
}
