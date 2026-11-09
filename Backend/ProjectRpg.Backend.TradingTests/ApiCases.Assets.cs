using System.Text.Json.Nodes;
using Npgsql;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

internal static partial class ApiCases
{
    private static async Task AssetCases(Fixture f)
    {
        int before = checks;
        var a = await f.Person(); var b = await f.Person(); var outsider = await f.Person(); var alt = await f.Person(a.Account);
        var ore = await f.Item(a);
        await SeedAssetMail(f, b, 1, "PeerOnlySecret");
        var pa = new TradeParticipant(a.Account, a.Id); var pb = new TradeParticipant(b.Account, b.Id);
        var state = (await f.Store.ExecuteAsync(new(Guid.NewGuid(), Guid.NewGuid(), pa, TradeCommandKind.Invite,
            Invitee: pb, CurrencyCode: "Gold"), default)).Session;
        string path = Route(a, state.State.TradeId) + "/assets";
        async Task Step(TradeParticipant who, TradeCommandKind kind, TradeOffer? offer = null)
        { state = (await f.Store.ExecuteAsync(new(Guid.NewGuid(), state.State.TradeId, who, kind, state.State.Revision, state.State.OfferRevision, Offer: offer), default)).Session; }
        await Get(f, "invalid-token", path, 401); await Get(f, f.Admin, path, 403); await Get(f, outsider.Token, path, 403);
        await Get(f, outsider.Token, Route(outsider, state.State.TradeId) + "/assets", 404);
        await Get(f, alt.Token, Route(alt, state.State.TradeId) + "/assets", 404);
        await Get(f, a.Token, Route(a, Guid.NewGuid()) + "/assets", 404);
        Check((await Get(f, a.Token, path, 409))!["error"]!.GetValue<string>() == "trade_not_completed", "invitation is not settlement proof");
        await Step(pb, TradeCommandKind.Accept);
        await Step(pa, TradeCommandKind.ReplaceOffer, new([new(ore.ItemId, ore.Revision, 3)], 20));
        await Step(pa, TradeCommandKind.Ready); await Step(pb, TradeCommandKind.Ready); await Step(pa, TradeCommandKind.Confirm);
        await Get(f, a.Token, path, 409); await Step(pb, TradeCommandKind.Confirm);
        var original = (await f.Store.AssetsAsync(state.State.TradeId, pa, default))!;
        Check(original!.Items.Single(i => i.ItemId == ore.ItemId).State.Quantity == 7, "post-commit outgoing quantity");
        Check(original.Items is not ItemRecord[] && original.Wallet.Balances is not CurrencyBalance[], "detached read-only asset collections");
        var other = (await Get(f, b.Token, Route(b, state.State.TradeId) + "/assets"))!;
        Check(other["items"]!.AsArray().Any(i => i!["definitionName"]!.GetValue<string>() == "Api.Ore" && i["state"]!["quantity"]!.GetValue<int>() == 3), "recipient sees received split");
        Check(other["items"]!.AsArray().Any(i => i!["location"]!["containerType"]!.GetValue<string>() == "Mail"), "snapshot includes own mail");
        var ctx = (await f.Game.GetCharacterEconomyContextAsync(a.Id, default))!;
        foreach (var (code, scope) in new[] { ("AssetsRoster", CurrencyScope.Roster), ("AssetsCharacter", CurrencyScope.Character) })
            await f.Economy.UpsertDefinitionAsync(new(code, code, scope, long.MaxValue, true), f.Clock.Now, default);
        await f.Economy.UpsertDefinitionAsync(new("AssetsDisabled", "Disabled", CurrencyScope.Character, 100, false), f.Clock.Now, default);
        var money = await f.Economy.CommitAsync(new(Guid.NewGuid(), a.Id, Guid.NewGuid(), "Test.AssetsMoney", Guid.NewGuid().ToString("N"), "fixture",
            [new("Gold", 9007199254740993), new("AssetsRoster", 7), new("AssetsCharacter", 11)]), ctx, f.Clock.Now, default);
        Check(money.Status == CurrencyTransactionStatus.Committed, "precision/scopes fixture");
        var fingerprint = await TargetFingerprint(f);
        var response = (await Get(f, a.Token, path))!;
        Check(response["isComplete"]!.GetValue<bool>() && response["characterId"]!.GetValue<Guid>() == a.Id
            && response["session"]!["completed"]!.GetValue<bool>(), "completed actor-bound complete snapshot");
        Check(response["wallet"]!["characterId"]!.GetValue<Guid>() == a.Id && response["wallet"]!["accountId"]!.GetValue<string>() == a.Account, "own wallet only");
        var balances = response["wallet"]!["balances"]!.AsArray();
        Check(balances.Single(x => x!["currencyCode"]!.GetValue<string>() == "Gold")!["balance"]!.GetValue<long>() == 9007199254741173, "exact int64 token beyond double (shared account also funded by alt)");
        Check(balances.Any(x => x!["currencyCode"]!.GetValue<string>() == "AssetsRoster" && x["balance"]!.GetValue<long>() == 7)
            && balances.Any(x => x!["currencyCode"]!.GetValue<string>() == "AssetsCharacter" && x["balance"]!.GetValue<long>() == 11)
            && !balances.Any(x => x!["currencyCode"]!.GetValue<string>() == "AssetsDisabled"), "existing enabled account/roster/character wallet semantics");
        Check(response["items"]!.AsArray().All(x => x!["owner"]!["ownerId"]!.GetValue<string>() == a.Id.ToString("D")), "all rows owned by selected actor");
        Check(!response.ToJsonString().Contains(b.Account, StringComparison.Ordinal) && !response.ToJsonString().Contains("PeerOnlySecret", StringComparison.Ordinal)
            && !response.ContainsKey("exchange") && !response.ContainsKey("receipt"), "no peer wallet/items or internal exchange receipt");
        await Get(f, a.Token, path);
        Check(fingerprint == await TargetFingerprint(f), "snapshot changes no database rows");
        f.Options.Enabled = false; await Get(f, a.Token, path); f.Options.Enabled = true;
        await f.Mode("Drain"); await Get(f, a.Token, path); await f.Mode("Open");

        // Later asset changes do not turn the historical completion into a new exchange.
        var current = (await f.Items.FindAsync(ore.ItemId, default))!;
        var consumed = await f.Items.CommitAsync(new(Guid.NewGuid(), "Test.AssetsConsume", Guid.NewGuid().ToString("N"), current.Owner, 1,
            [new(current.Revision, current with { State = current.State with { Quantity = 6 } })]), f.Clock.Now, default);
        Check(consumed.Status == ItemRepositoryCommitStatus.Committed, "post-trade item change");
        var latest = await f.Store.AssetsAsync(state.State.TradeId, pa, default);
        Check(latest!.Items.Single(i => i.ItemId == ore.ItemId).State.Quantity == 6
            && original.Items.Single(i => i.ItemId == ore.ItemId).State.Quantity == 7, "new current view and detached previous view");
        Check(original.Wallet.Balances.Single(x => x.Definition.CurrencyCode == "Gold").Balance == 180, "old wallet view unchanged");
        if (f.Data is not null) await AssetMvcc(f, state.State.TradeId, pa, ore.ItemId);
        // A new unfinished/cancelled exchange cannot masquerade as a completed refresh.
        var pending = await f.Store.ExecuteAsync(new(Guid.NewGuid(), Guid.NewGuid(), pa, TradeCommandKind.Invite, Invitee: pb, CurrencyCode: "Gold"), default);
        await Get(f, a.Token, Route(a, pending.Session.State.TradeId) + "/assets", 409);
        await f.Store.ExecuteAsync(new(Guid.NewGuid(), pending.Session.State.TradeId, pa, TradeCommandKind.Cancel, pending.Session.State.Revision), default);
        await Get(f, a.Token, Route(a, pending.Session.State.TradeId) + "/assets", 409);
        await AssetBounds(f, state.State.TradeId, b);
        var server = await f.Person(); var dungeon = await f.Game.CreateDungeonSessionAsync(server.Account, server.Id, "AssetsFixture", "Normal", f.Clock.Now, f.Clock.Now.AddHours(1), default);
        await f.Game.AssignDungeonServerAsync(dungeon.DungeonSessionId, "assets-fixture-server", "127.0.0.1:1", f.Clock.Now, f.Clock.Now.AddMinutes(10), default);
        var credential = new AccessTokenService(f.Clock).Issue(TimeSpan.FromMinutes(10));
        await f.Game.StoreGameServerCredentialAsync(credential.TokenHash, "assets-fixture-server", dungeon.DungeonSessionId, credential.ExpiresAt, default);
        await Get(f, credential.Token, path, 403);
        await f.Restrict(b); await Get(f, b.Token, Route(b, state.State.TradeId) + "/assets", 401); await Get(f, a.Token, path);
        Console.WriteLine($"PASS completed trade assets: {checks-before} assertions; owned full current assets, precision/scopes, no writes, limits, recovery reads and PG MVCC where available");
    }

    private static async Task SeedAssetMail(Fixture f, Person person, int count, string source = "Test.AssetsMail")
    {
        var owner = new ItemOwnerRef(ItemOwnerType.Character, person.Id.ToString("D"));
        foreach (var batch in Enumerable.Range(0, count).Chunk(ItemRecordRules.MaximumMutationCount))
        {
            var records = batch.Select(n => new ItemRecordMutation(0, new("Material", "Api.Mail", 1, owner,
                new(ItemContainerType.Mail, Guid.NewGuid().ToString("D"), n), new(Guid.NewGuid(), 1234, 1, [], []), 0,
                ItemLifecycleState.Active, new(ItemBindState.Unbound, new(0, 0), null, source, false)))).ToArray();
            var result = await f.Items.CommitAsync(new(Guid.NewGuid(), "Test.AssetsMail", Guid.NewGuid().ToString("N"), owner, batch.Length, records), f.Clock.Now, default);
            Check(result.Status == ItemRepositoryCommitStatus.Committed, "controlled active mail fixture: " + result.Status);
        }
    }
    private static async Task AssetBounds(Fixture f, Guid trade, Person person)
    {
        var actor = new TradeParticipant(person.Account, person.Id);
        var count = (await f.Store.AssetsAsync(trade, actor, default))!.Items.Count;
        await SeedAssetMail(f, person, TradeAssets.MaximumItems - count);
        Check((await Get(f, person.Token, Route(person, trade) + "/assets"))!["items"]!.AsArray().Count == 500, "exactly 500 is complete");
        await SeedAssetMail(f, person, 1);
        var failure = (await Get(f, person.Token, Route(person, trade) + "/assets", 409))!;
        Check(failure["error"]!.GetValue<string>() == "trade_assets_unavailable" && !failure.ContainsKey("items") && !failure.ContainsKey("wallet"), "501 never returns partial assets");
    }
    private static async Task AssetMvcc(Fixture f, Guid trade, TradeParticipant actor, Guid item)
    {
        var before = (await f.Store.AssetsAsync(trade, actor, default))!;
        await using var c = await f.Data!.OpenConnectionAsync(); await using var tx = await c.BeginTransactionAsync();
        await using (var q = new NpgsqlCommand("LOCK TABLE item_records IN ACCESS EXCLUSIVE MODE", c, tx)) await q.ExecuteNonQueryAsync();
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
        var pending = f.Store.AssetsAsync(trade, actor, timeout.Token);
        bool waiting = false;
        await using (var monitor = await f.Data.OpenConnectionAsync()) for (int i = 0; i < 100; i++)
        {
            await using var q = new NpgsqlCommand("SELECT EXISTS(SELECT 1 FROM pg_stat_activity WHERE datname=current_database() AND pid<>pg_backend_pid() AND wait_event_type='Lock' AND query LIKE '%WHERE owner_type=%')", monitor);
            if ((bool)(await q.ExecuteScalarAsync())!) { waiting = true; break; } await Task.Delay(20);
        }
        if (!waiting) { await tx.RollbackAsync(); await pending; throw new Exception("Asset read did not reach controlled MVCC point"); }
        await using (var q = new NpgsqlCommand("""
            UPDATE item_records SET quantity=quantity-1,revision=revision+1 WHERE item_id=@item;
            UPDATE currency_balances SET balance=balance-1,revision=revision+1 WHERE owner_type='Account' AND owner_id=@owner AND currency_code='Gold';
            """, c, tx)) { q.Parameters.AddWithValue("item", item); q.Parameters.AddWithValue("owner", actor.AccountId); await q.ExecuteNonQueryAsync(); }
        await tx.CommitAsync(); var old = (await pending)!;
        Check(old.Items.Single(x => x.ItemId == item).State.Quantity == before.Items.Single(x => x.ItemId == item).State.Quantity
            && old.Wallet.Balances.Single(x => x.Definition.CurrencyCode == "Gold").Balance == before.Wallet.Balances.Single(x => x.Definition.CurrencyCode == "Gold").Balance, "single PG snapshot does not mix old items and new wallet");
        var current = (await f.Store.AssetsAsync(trade, actor, default))!;
        Check(current.Items.Single(x => x.ItemId == item).State.Quantity == old.Items.Single(x => x.ItemId == item).State.Quantity - 1
            && current.Wallet.Balances.Single(x => x.Definition.CurrencyCode == "Gold").Balance == old.Wallet.Balances.Single(x => x.Definition.CurrencyCode == "Gold").Balance - 1, "next snapshot observes new item/wallet together");
    }
}
