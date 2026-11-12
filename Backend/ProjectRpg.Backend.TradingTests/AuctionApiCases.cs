using System.Collections.Concurrent;
using System.Diagnostics;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.DependencyInjection.Extensions;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Npgsql;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using ProjectRpg.Backend.Operations.ServiceControl;
using ProjectRpg.Backend.Operations.Restrictions;

internal static class AuctionApiCases
{
    private static int checks, sequence;
    private static readonly TradeItemPolicy Policy = new([new("Material", "Ore", 1, true, true, 100)]);
    public static async Task<int> Run(string mode)
    {
        try
        {
            await using var f = await Fixture.Create(mode != "--auction-api-memory");
            if (mode == "--auction-api-restart") await ProgramRestart(f);
            else
            {
                await Commands(f); await Assets(f); await QueriesAndPrivacy(f); await InputAndAccess(f); await WorkerAndLimits(f); await WideAmounts(f);
                if (f.Data is null) await ExpiryFairness();
            }
            Console.WriteLine($"PASS {mode}: {checks} assertions; real bearer HTTP/DI and isolated assets. No auction UE/Steam claim."); return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }
    private static async Task Commands(Fixture f)
    {
        var seller = await f.Person(0); var buyer = await f.Person(); var foreign = await f.Person();
        var item = await f.Seed(seller); var id = Guid.NewGuid(); var reg = Registration(f, item);
        var registered = await f.Post(seller, id, "register", reg);
        Check(registered["listing"]!["currencyCode"]!.GetValue<string>() == "Gold" && (await f.Items.FindAsync(item.ItemId, default))!.State.Quantity == 6, "HTTP registration uses server terms and actual escrow");
        f.Options.FeeBasisPoints = 900;
        Check(JsonNode.DeepEquals(registered, await f.Post(seller, id, "register", reg)), "registration retry keeps original server terms after policy change");
        f.Options.FeeBasisPoints = 500;
        await f.Get(foreign, Root(foreign) + "/requests/" + reg.RequestId, 404);
        await f.Post(foreign, id, "cancel", new AuctionCommandRequest(Guid.NewGuid(), ExpectedRevision: "1"), 404);
        var body = Purchase();
        var duplicates = await Task.WhenAll(f.Post(buyer, id, "purchase", body), f.Post(buyer, id, "purchase", body));
        Check(JsonNode.DeepEquals(duplicates[0], duplicates[1]) && await f.Balance(buyer) == 301 && await f.Balance(seller) == 0, "parallel HTTP purchase pays/delivers once");
        Check((await f.Items.FindByOwnerAsync(new(ItemOwnerType.Character, buyer.Id.ToString("D")), false, 100, default)).Any(i => i.DefinitionName == "Ore" && i.State.Quantity == 4), "HTTP buyer receives actual item");
        await f.Post(buyer, id, "purchase", body with { TotalPrice = "700" }, 409);
        var p = await f.Get(seller, Root(seller) + $"/{id}/payable");
        Check(p!["remainingAmount"]!.GetValue<string>() == "665", "seller pending proceeds use registration's fee");
        var claim = new AuctionCommandRequest(Guid.NewGuid(), ExpectedRevision: "1", ExpectedCurrencyCode: "Gold", RequestedAmount: "200");
        var paid = await f.Post(seller, id, "claim", claim);
        Check(paid["paid"]!.GetValue<string>() == "200" && await f.Balance(seller) == 200 && paid["remaining"]!.GetValue<string>() == "465", "HTTP partial claim conserves payable/wallet");
        Check(JsonNode.DeepEquals(paid, await f.Post(seller, id, "claim", claim)), "HTTP claim retry immutable");
        await f.Post(buyer, id, "claim", claim with { RequestId = Guid.NewGuid() }, 404);
        await f.Get(buyer, Root(buyer) + $"/{id}/payable", 404);
        await f.Get(buyer, Root(buyer) + $"/{id}", 404);
        Check((await f.Get(seller, Root(seller) + $"/{id}"))!["status"]!.GetValue<string>() == "Sold", "seller sees sold summary without buyer identity");
        var alt = await f.Person(0, seller.Account);
        Check((await f.Get(alt, Root(alt) + $"/{id}/payable"))!["remainingAmount"]!.GetValue<string>() == "465", "same account alt may read shared wallet payable");
        await f.Get(alt, Root(alt) + "/requests/" + claim.RequestId, 404);
        f.Options.Enabled = false;
        await f.Get(buyer, Root(buyer), 404);
        await f.Post(buyer, Guid.NewGuid(), "purchase", Purchase(), 404);
        Check(JsonNode.DeepEquals(duplicates[0], await f.Get(buyer, Root(buyer) + "/requests/" + body.RequestId)), "feature disabled still allows exact committed purchase recovery");
        await f.Get(seller, Root(seller) + "/mine");
        var rest = await f.Post(alt, id, "claim", new AuctionCommandRequest(Guid.NewGuid(), ExpectedRevision: "2", ExpectedCurrencyCode: "Gold", RequestedAmount: "999"));
        Check(rest["paid"]!.GetValue<string>() == "465" && await f.Balance(seller) == 665, "disabled feature permits legitimate proceeds recovery through same account alt");
        f.Options.Enabled = true;
        if (f.Data is not null)
        {
            await f.Sql("CREATE TABLE auction_api_restart_fixture(token text,route text,result jsonb); INSERT INTO auction_api_restart_fixture VALUES(@token,@route,@result::jsonb)",
                ("token", buyer.Token), ("route", Root(buyer) + "/requests/" + body.RequestId), ("result", duplicates[0].ToJsonString()));
        }
    }
    private static async Task Assets(Fixture f)
    {
        var seller=await f.Person(0);var buyer=await f.Person();var outsider=await f.Person();var alt=await f.Person(0,buyer.Account);
        var item=await f.Seed(seller);var id=Guid.NewGuid();var reg=Registration(f,item);
        await f.Post(seller,id,"register",reg);var command=Purchase();var receipt=await f.Post(buyer,id,"purchase",command);
        string route=Root(buyer)+$"/requests/{command.RequestId}/assets";
        var first=await f.Get(buyer,route);Check(first["isComplete"]!.GetValue<bool>() && first["wallet"]!["balances"]![0]!["balance"]!.GetValue<long>()==301,"current assets after purchase");
        await f.Fund(buyer,1);var extra=await f.Seed(buyer,1,161,2);
        var fresh=await f.Get(buyer,route);Check(fresh["wallet"]!["balances"]![0]!["balance"]!.GetValue<long>()==302 && fresh["items"]!.AsArray().Count==first["items"]!.AsArray().Count+1,$"fresh current assets include later wallet/item changes; balance={fresh["wallet"]!["balances"]![0]!["balance"]}, before={first["items"]!.AsArray().Count}, after={fresh["items"]!.AsArray().Count}");
        var moved=await f.Items.CommitAsync(new(Guid.NewGuid(),"Test.Move","owned-mail-read",extra.Owner,0,[new(extra.Revision,extra with {Location=new(ItemContainerType.Mail,buyer.Id.ToString("D"),0)})]),f.Clock.Now,default);
        Check(moved.Status==ItemRepositoryCommitStatus.Committed,"owned mail fixture move");
        var mail=await f.Get(buyer,route);Check(mail["items"]!.AsArray().Any(x=>x!["location"]!["containerType"]!.GetValue<string>()=="Mail"),"current snapshot includes owned mail alongside inventory");
        Check(JsonNode.DeepEquals(receipt,await f.Get(buyer,Root(buyer)+$"/requests/{command.RequestId}")),"current read never rewrites historical receipt");
        Check(fresh["characterId"]!.GetValue<Guid>()==buyer.Id && fresh["requestId"]!.GetValue<Guid>()==command.RequestId,"snapshot pins original actor/request");
        await f.Get(outsider,Root(outsider)+$"/requests/{command.RequestId}/assets",404);
        await f.Get(alt,Root(alt)+$"/requests/{command.RequestId}/assets",404);
        await f.Get(buyer,Root(seller)+$"/requests/{command.RequestId}/assets",403);
        await f.Get(buyer,Root(buyer)+$"/requests/{Guid.NewGuid()}/assets",404);
        await f.Raw(null,route,null,401);await f.Raw(f.Admin,route,null,403);await f.Get(buyer,route+"?x=1",400);
        f.Options.Enabled=false;await f.Get(buyer,route);f.Options.Enabled=true;
        var registrationAssets=await f.Get(seller,Root(seller)+$"/requests/{reg.RequestId}/assets");
        Check(registrationAssets["items"]!.AsArray().All(x=>x!["owner"]!["ownerId"]!.GetValue<string>()==seller.Id.ToString("D")),"seller read excludes escrow and buyer assets");
        var claim=new AuctionCommandRequest(Guid.NewGuid(),ExpectedRevision:"1",ExpectedCurrencyCode:"Gold",RequestedAmount:"1");await f.Post(seller,id,"claim",claim);
        await f.Get(seller,Root(seller)+$"/requests/{claim.RequestId}/assets");
        var cancelledId=Guid.NewGuid();var r=Registration(f,(await f.Items.FindAsync(item.ItemId,default))!);r=r with {ExpectedItemRevision="2",Quantity=1};await f.Post(seller,cancelledId,"register",r);
        var cancel=new AuctionCommandRequest(Guid.NewGuid(),ExpectedRevision:"1");await f.Post(seller,cancelledId,"cancel",cancel);await f.Get(seller,Root(seller)+$"/requests/{cancel.RequestId}/assets");
    }
    private static async Task QueriesAndPrivacy(Fixture f)
    {
        var seller = await f.Person(0); var buyer = await f.Person(); var alt = await f.Person(0, seller.Account);
        List<Guid> ids = [];
        for (int n = 0; n < 4; n++)
        {
            var item = await f.Seed(seller, slot: 160 + n); var id = Guid.NewGuid(); ids.Add(id);
            await f.Post(seller, id, "register", Registration(f, item) with { TotalPrice = (800 + n).ToString(), ExpiresAt = f.Clock.Now.AddMinutes(40) });
        }
        var a = await f.Get(buyer, Root(buyer) + "?limit=2&definitionName=Ore&minimumPrice=800&maximumPrice=803&currencyCode=Gold");
        Privacy(a!);
        var first = a!["listings"]!.AsArray().Select(x => x!["listingId"]!.GetValue<Guid>()).ToArray();
        var cursor = a["nextCursor"]!.GetValue<string>();
        var b = await f.Get(buyer, Root(buyer) + "?limit=2&definitionName=Ore&minimumPrice=800&maximumPrice=803&currencyCode=Gold&cursor=" + Uri.EscapeDataString(cursor));
        var second = b!["listings"]!.AsArray().Select(x => x!["listingId"]!.GetValue<Guid>()).ToArray();
        Check(first.Concat(second).Distinct().Count() == 4 && first.Concat(second).All(ids.Contains) && b["nextCursor"] is null, "keyset pages handle identical expiry and exact filters without duplicates");
        foreach (var id in ids) Privacy((await f.Get(buyer, Root(buyer) + "/" + id))!);
        Check((await f.Get(alt, Root(alt) + "/mine?minimumPrice=800&maximumPrice=803"))!["listings"]!.AsArray().Count == 4, "own listings are account scoped");
        var cancelled = await f.Post(seller, ids[0], "cancel", new AuctionCommandRequest(Guid.NewGuid(), ExpectedRevision: "1"));
        Check(cancelled["listing"]!["status"]!.GetValue<string>() == "Cancelled", "HTTP cancellation returns unsold item");
        await f.Get(buyer, Root(buyer) + "/" + ids[0], 404);
        Check((await f.Get(seller, Root(seller) + "/" + ids[0]))!["status"]!.GetValue<string>() == "Cancelled", "owner history remains visible after close");
    }
    private static async Task InputAndAccess(Fixture f)
    {
        var owner = await f.Person(0); var foreign = await f.Person(); var id = Guid.NewGuid();
        await f.Raw(null, Root(owner), null, 401);
        await f.Raw(f.Admin, Root(owner), null, 403);
        await f.Raw(foreign.Token, Root(owner), null, 403);
        foreach (string query in new[] { "limit=0", "limit=51", "limit=1&limit=2", "definitionName=", "sellerAccount=private", "minimumPrice=01", "minimumPrice=9&maximumPrice=1", "cursor=bad!", "currencyCode=Gold%0A" })
            await f.Get(foreign, Root(foreign) + "?" + query, 400);
        var route = Root(owner) + $"/{id}/register";
        foreach (string json in new[] { "{}", "null", "[]", "{\"RequestId\":\"" + Guid.NewGuid() + "\"}", "{\"requestId\":\"" + Guid.NewGuid() + "\",\"seller\":\"fake\"}",
            "{\"requestId\":\"" + Guid.NewGuid() + "\",\"requestId\":\"" + Guid.NewGuid() + "\"}" })
            await f.Raw(owner.Token, route, json, 400);
        await f.Raw(owner.Token, route, new string('x', 8193), 413);
        await f.Raw(owner.Token, route, "{}", 415, "text/plain");
        using (var content = new UnknownLengthContent(new string('x', 9000))) await f.RawContent(owner.Token, route, content, 413);
        await f.Post(owner, id, "expire", new AuctionCommandRequest(Guid.NewGuid()), 404);
        var item = await f.Seed(owner); var reg = Registration(f, item);
        await f.Post(owner, id, "register", reg with { TotalPrice = "01" }, 400);
        await f.Post(owner, id, "register", reg with { TotalPrice = "-1" }, 400);
        await f.Post(owner, id, "register", reg with { TotalPrice = "9223372036854775808" }, 400);
        await f.Post(owner, id, "register", reg with { ExpiresAt = f.Clock.Now.AddSeconds(1) }, 400);
        await f.Post(owner, id, "register", reg);
        await f.Mode("Drain");
        await f.Post(foreign, id, "purchase", Purchase(), 503);
        await f.Post(owner, id, "cancel", new AuctionCommandRequest(Guid.NewGuid(), ExpectedRevision: "1"));
        await f.Mode("Open");
        var restricted = await f.Person(); await f.Restrict(restricted);
        var seller = await f.Person(0); var source = await f.Seed(seller); var active = Guid.NewGuid(); await f.Post(seller, active, "register", Registration(f, source));
        await f.Post(restricted, active, "purchase", Purchase(), 401); // restriction revokes the actual bearer session
        var dungeonActor = await f.Person();
        var dungeon = await f.Game.CreateDungeonSessionAsync(dungeonActor.Account, dungeonActor.Id, "ApiFixture", "Normal", f.Clock.Now, f.Clock.Now.AddMinutes(5), default);
        await f.Post(dungeonActor, active, "purchase", Purchase(), 409);
        await f.Game.AssignDungeonServerAsync(dungeon.DungeonSessionId, "auction-api-server", "127.0.0.1:1", f.Clock.Now, f.Clock.Now.AddMinutes(5), default);
        var credential = new AccessTokenService(f.Clock).Issue(TimeSpan.FromMinutes(5));
        await f.Game.StoreGameServerCredentialAsync(credential.TokenHash, "auction-api-server", dungeon.DungeonSessionId, credential.ExpiresAt, default);
        await f.Raw(credential.Token, Root(foreign), null, 403);
    }
    private static async Task WorkerAndLimits(Fixture f)
    {
        var actor = await f.Person(0); var alt = await f.Person(0, actor.Account);
        for (int n = 0; n < 30; n++) await f.Get(n % 2 == 0 ? actor : alt, Root(n % 2 == 0 ? actor : alt));
        var limited = await f.Get(actor, Root(actor), 429);
        Check(limited!["error"]!.GetValue<string>() == "auction_rate_limit", "search budget shared across account alts");
        var seller = await f.Person(0); var id = Guid.NewGuid(); await f.Post(seller, id, "register", Registration(f, await f.Seed(seller)));
        await f.Expire(id);
        await f.Get(await f.Person(), Root(seller) + "/" + id, 403); // identity checked before listing lookup
        var reader = await f.Person(); await f.Get(reader, Root(reader) + "/" + id, 404);
        var worker1 = new AuctionExpiryWorker(f.Queries, f.Escrow, NullLogger<AuctionExpiryWorker>.Instance);
        var worker2 = new AuctionExpiryWorker(f.Queries, f.Escrow, NullLogger<AuctionExpiryWorker>.Instance);
        await Task.WhenAll(worker1.SweepAsync(default), worker2.SweepAsync(default));
        Check((await f.Escrow.FindAsync(id, new(seller.Account, seller.Id), default))!.State.Status == AuctionListingStatus.Expired, "parallel expiry workers close due listing once");
        Check((await f.Items.FindByOwnerAsync(new(ItemOwnerType.Character, seller.Id.ToString("D")), false, 100, default)).Single(i => i.DefinitionName == "Ore").State.Quantity == 10, "worker returns exact quantity");
        if (f.Data is not null)
        {
            // Leave one due listing for the actual Program child to expire with the feature disabled.
            var pending = await f.Person(0); var pendingId = Guid.NewGuid(); await f.Post(pending, pendingId, "register", Registration(f, await f.Seed(pending))); await f.Expire(pendingId);
        }
    }
    private static async Task WideAmounts(Fixture f)
    {
        var seller = await f.Person(0); var buyer = await f.Person(0);
        await f.Economy.UpsertDefinitionAsync(new("WideGold", "WideGold", CurrencyScope.Account, long.MaxValue, true), f.Clock.Now, default);
        await f.Fund(buyer, long.MaxValue, "WideGold"); f.Options.CurrencyCode = "WideGold";
        var id = Guid.NewGuid(); var item = await f.Seed(seller); var registered = await f.Post(seller, id, "register", Registration(f, item) with { TotalPrice = long.MaxValue.ToString() });
        Check(registered["listing"]!["totalPrice"]!.GetValue<string>() == "9223372036854775807", "wire price preserves full int64 as string");
        var purchase = await f.Post(buyer, id, "purchase", Purchase() with { TotalPrice = long.MaxValue.ToString(), ExpectedCurrencyCode = "WideGold" });
        Check(purchase["balance"]!.GetValue<string>() == "0", "max int64 purchase debits exactly"); f.Options.CurrencyCode = "Gold";
    }
    private static async Task ExpiryFairness()
    {
        await using var f = await Fixture.Create(false);
        for (int n = 0; n < 51; n++)
        {
            var seller = await f.Person(0); var source = await f.Seed(seller); if (n < 50) await f.Seed(seller, 100, 161, 2);
            await f.Escrow.ExecuteAsync(new(Guid.NewGuid(), Guid.NewGuid(), new(seller.Account,seller.Id), AuctionEscrowKind.Register, Item:new(source.ItemId,source.Revision,10), TotalPrice:699,
                ExpiresAt:f.Clock.Now.AddMinutes(n < 50 ? 1 : 2), Terms:f.Options.Terms()), default);
        }
        f.Clock.Now += TimeSpan.FromMinutes(3);
        var tiny = new InMemoryAuctionRepository(f.Gate!, (InMemoryGameRepository)f.Game, (InMemoryItemRepository)f.Items, (InMemoryEconomyRepository)f.Economy, Policy, new(10,4));
        var worker = new AuctionExpiryWorker(f.Queries,tiny,NullLogger<AuctionExpiryWorker>.Instance);
        Check(await worker.SweepAsync(default)==0 && await worker.SweepAsync(default)==1, "50 blocked returns do not starve the later eligible listing");
    }
    private static async Task ProgramRestart(Fixture f)
    {
        string token, route, expected;
        await using (var c=await f.Data!.OpenConnectionAsync()) await using (var q=new NpgsqlCommand("SELECT token,route,result::text FROM auction_api_restart_fixture",c))
        await using (var r=await q.ExecuteReaderAsync()){Check(await r.ReadAsync(),"previous process receipt fixture present");token=r.GetString(0);route=r.GetString(1);expected=r.GetString(2);}
        var start = new ProcessStartInfo("dotnet") {WorkingDirectory=AppContext.BaseDirectory,UseShellExecute=false,RedirectStandardOutput=true,RedirectStandardError=true,CreateNoWindow=true};
        start.ArgumentList.Add(typeof(AuctionApplication).Assembly.Location);start.ArgumentList.Add("--urls");start.ArgumentList.Add("http://127.0.0.1:0");
        foreach(var (key,value) in new[]{("ASPNETCORE_ENVIRONMENT","Testing"),("DOTNET_ENVIRONMENT","Testing"),("Storage__Provider","Postgres"),
            ("ConnectionStrings__Postgres",Environment.GetEnvironmentVariable("PROJECT_RPG_AUCTION_API_TEST_POSTGRES")!),
            ("POSTGRES_CONNECTION_STRING",Environment.GetEnvironmentVariable("PROJECT_RPG_AUCTION_API_TEST_POSTGRES")!),
            ("Auth__AdminToken",Guid.NewGuid().ToString("N")),("Steam__AllowDevelopmentAuthentication","true"),("DungeonContent__DevelopmentProfile","Training"),
            ("Auctions__Enabled","false"),("RateLimits__Enabled","false"),("Kestrel__Endpoints__Http__Url","http://127.0.0.1:0"),
            ("Logging__LogLevel__Default","Warning"),("Logging__LogLevel__Microsoft.Hosting.Lifetime","Information")})start.Environment[key]=value;
        start.Environment.Remove("PROJECT_RPG_BACKEND_ADMIN_TOKEN");start.Environment.Remove("STEAM_WEB_API_PUBLISHER_KEY");
        var address=new TaskCompletionSource<string>(TaskCreationOptions.RunContinuationsAsynchronously);ConcurrentQueue<string> tail=new();
        void Observe(object sender,DataReceivedEventArgs line){if(line.Data is not string text)return;tail.Enqueue(text);while(tail.Count>30)tail.TryDequeue(out _);const string marker="Now listening on: ";int at=text.IndexOf(marker,StringComparison.Ordinal);if(at>=0)address.TrySetResult(text[(at+marker.Length)..]);}
        using Process process=new(){StartInfo=start,EnableRaisingEvents=true};process.OutputDataReceived+=Observe;process.ErrorDataReceived+=Observe;process.Exited+=(_,_)=>address.TrySetException(new Exception("Own backend exited before listening"));
        Check(process.Start(),"new Program process starts");process.BeginOutputReadLine();process.BeginErrorReadLine();
        try
        {
            using HttpClient client=new(){BaseAddress=new(await address.Task.WaitAsync(TimeSpan.FromSeconds(30)))};client.DefaultRequestHeaders.Authorization=new("Bearer",token);
            using var response=await client.GetAsync(route);Check(response.IsSuccessStatusCode && JsonNode.DeepEquals(JsonNode.Parse(expected),JsonNode.Parse(await response.Content.ReadAsStringAsync())),"actual Program DI/auth returns exact persisted purchase through recovery route while disabled");
            for(int n=0;n<80 && (await f.Queries.FindExpiredAsync(50,null,default)).Count!=0;n++)await Task.Delay(100);
            Check((await f.Queries.FindExpiredAsync(50,null,default)).Count==0,"actual Program hosted worker returns due items while disabled");
        }
        catch{Console.Error.WriteLine(string.Join(Environment.NewLine,tail));throw;}
        finally{if(!process.HasExited)process.Kill(entireProcessTree:true);await process.WaitForExitAsync();}
    }
    private static void Privacy(JsonNode node)
    {
        var text=node.ToJsonString();foreach(string key in new[]{"AccountId","accountId","sellerAccount","buyerAccount","walletOwner","generationSeed","escrowAtRegistration","creationSource","fingerprint","itemId"})Check(!text.Contains(key,StringComparison.Ordinal),"public auction projection excludes "+key);
        foreach(var entry in node is JsonObject o && o["listings"] is JsonArray list?list:new JsonArray(node.DeepClone()))Check(entry!["sellerCharacterId"] is null && entry["sellerProceeds"] is null,"public listing hides seller identity/proceeds");
    }
    private static AuctionCommandRequest Registration(Fixture f,ItemRecord i)=>new(Guid.NewGuid(),ItemId:i.ItemId,ExpectedItemRevision:i.Revision.ToString(),Quantity:4,TotalPrice:"699",ExpiresAt:f.Clock.Now.AddHours(1));
    private static AuctionCommandRequest Purchase()=>new(Guid.NewGuid(),ExpectedRevision:"1",ExpectedCurrencyCode:"Gold",TotalPrice:"699");
    private static string Root(Person p)=>$"/api/characters/{p.Id:D}/auctions";
    private static void Check(bool ok,string text){if(!ok)throw new Exception(text);checks++;}
    private sealed record Person(string Account,Guid Id,string Token);
    private sealed class TestClock:TimeProvider{public DateTimeOffset Now=DateTimeOffset.UtcNow;public override DateTimeOffset GetUtcNow()=>Now;}
    private sealed class UnknownLengthContent(string text):HttpContent
    {
        protected override bool TryComputeLength(out long length){length=0;return false;}
        protected override Task SerializeToStreamAsync(Stream stream,System.Net.TransportContext? context)=>stream.WriteAsync(Encoding.UTF8.GetBytes(text)).AsTask();
    }
    private sealed class Fixture:IAsyncDisposable
    {
        public NpgsqlDataSource? Data;public InMemoryTransactionGate? Gate;public TestClock Clock=new();public IGameRepository Game=null!;public IItemRepository Items=null!;public IEconomyRepository Economy=null!;
        public IAuctionEscrowRepository Escrow=null!;public IAuctionQueryRepository Queries=null!;public AuctionOptions Options=new(){Enabled=true};public string Admin=Guid.NewGuid().ToString("N");public HttpClient Client=null!;private WebApplication app=null!;
        public static async Task<Fixture>Create(bool pg)
        {
            var f=new Fixture();var b=WebApplication.CreateBuilder(new WebApplicationOptions{EnvironmentName="Testing",ContentRootPath=AppContext.BaseDirectory});b.Logging.ClearProviders();b.Configuration["Kestrel:Endpoints:Http:Url"]="http://127.0.0.1:0";b.WebHost.UseUrls("http://127.0.0.1:0");
            if(pg){var cs=new NpgsqlConnectionStringBuilder(Environment.GetEnvironmentVariable("PROJECT_RPG_AUCTION_API_TEST_POSTGRES"));if(cs.Host!="127.0.0.1"||cs.Database!="auction_api_test"||!Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_TRADE_TEST_RUN"),"N",out _))throw new Exception("Owned private DB required");f.Data=NpgsqlDataSource.Create(cs.ConnectionString);f.Game=new PostgresGameRepository(f.Data,NullLogger<PostgresGameRepository>.Instance);f.Items=new PostgresItemRepository(f.Data,NullLogger<PostgresItemRepository>.Instance);f.Economy=new PostgresEconomyRepository(f.Data);b.Services.AddSingleton(f.Data);}
            else{f.Gate=new(f.Clock);var items=new InMemoryItemRepository(f.Gate);var money=new InMemoryEconomyRepository(f.Gate);f.Game=new InMemoryGameRepository(f.Gate,items,f.Clock);f.Items=items;f.Economy=money;b.Services.AddSingleton(f.Gate);b.Services.AddSingleton(items);b.Services.AddSingleton(money);}
            await f.Game.EnsureCreatedAsync(default);await f.Economy.UpsertDefinitionAsync(new("Gold","Gold",CurrencyScope.Account,1000,true),f.Clock.Now,default);
            b.Services.AddSingleton(f.Game);b.Services.AddSingleton(f.Items);b.Services.AddSingleton(f.Economy);b.Services.AddSingleton(Policy);b.Services.AddSingleton<TimeProvider>(f.Clock);
            b.Services.AddSingleton<AccessTokenService>();b.Services.AddSingleton<IOptions<AuthOptions>>(Microsoft.Extensions.Options.Options.Create(new AuthOptions{AdminToken=f.Admin}));b.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Microsoft.Extensions.Options.Options.Create(new SecurityTelemetryOptions()));
            b.Services.AddGameRepositoryContracts();b.Services.AddAuctionServices(f.Options,pg);b.Services.RemoveAll<IHostedService>();b.Services.AddScoped<BearerAuthenticator>();
            f.app=b.Build();f.app.UseMiddleware<ApiAuthenticationMiddleware>();f.app.MapAuctionApi();await f.app.StartAsync();
            f.Escrow=f.app.Services.GetRequiredService<IAuctionEscrowRepository>();f.Queries=f.app.Services.GetRequiredService<IAuctionQueryRepository>();f.Client=new(){BaseAddress=new(f.app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single())};return f;
        }
        public async Task<Person>Person(long amount=1000,string? account=null){int n=Interlocked.Increment(ref sequence);account??=(76561800000000000L+n).ToString();await Game.UpsertAccountAsync(new(account,account,false,false),Clock.Now,default);var c=await Game.CreateCharacterAsync(account,"AuctionApi"+n,Clock.Now,default);var token=new AccessTokenService(Clock).Issue(TimeSpan.FromHours(6));await Game.StoreSessionAsync(token.TokenHash,account,token.ExpiresAt,default);var p=new Person(account,c.CharacterId,token.Token);if(amount!=0)await Fund(p,amount);return p;}
        public async Task Fund(Person p,long amount,string code="Gold"){var r=await Economy.CommitAsync(new(Guid.NewGuid(),p.Id,Guid.NewGuid(),"Test.Fund",Guid.NewGuid().ToString("N"),"test",[new(code,amount)]),(await Game.GetCharacterEconomyContextAsync(p.Id,default))!,Clock.Now,default);Check(r.Status==CurrencyTransactionStatus.Committed,"fund fixture");}
        public async Task<long>Balance(Person p)=>(await Economy.LoadWalletAsync((await Game.GetCharacterEconomyContextAsync(p.Id,default))!,default)).Balances.Single(x=>x.Definition.CurrencyCode=="Gold").Balance;
        public async Task<ItemRecord>Seed(Person p,int quantity=10,int slot=160,int seed=1){var owner=new ItemOwnerRef(ItemOwnerType.Character,p.Id.ToString("D"));var i=new ItemRecord("Material","Ore",1,owner,new(ItemContainerType.Inventory,owner.OwnerId,slot),new(Guid.NewGuid(),seed,quantity,["Item.Ore"],[new("Stat.Test",4)]),0,ItemLifecycleState.Active,new(ItemBindState.Unbound,new(0,0),null,"Test",false));var r=await Items.CommitAsync(new(Guid.NewGuid(),"Test.Seed","fixture",owner,quantity,[new(0,i)]),Clock.Now,default);Check(r.Status==ItemRepositoryCommitStatus.Committed,"seed fixture");return r.Records.Single();}
        public Task<JsonNode>Post(Person p,Guid id,string action,object body,int status=200)=>Raw(p.Token,Root(p)+$"/{id}/{action}",JsonSerializer.Serialize(body,new JsonSerializerOptions(JsonSerializerDefaults.Web)),status);
        public Task<JsonNode>Get(Person p,string route,int status=200)=>Raw(p.Token,route,null,status);
        public async Task<JsonNode>Raw(string? token,string route,string? body,int status,string type="application/json"){using var request=new HttpRequestMessage(body is null?HttpMethod.Get:HttpMethod.Post,route);if(body is not null)request.Content=new StringContent(body,Encoding.UTF8,type);return await Response(token,request,status);}
        public async Task RawContent(string token,string route,HttpContent content,int status){using var request=new HttpRequestMessage(HttpMethod.Post,route){Content=content};request.Content.Headers.ContentType=new("application/json");await Response(token,request,status);}
        private async Task<JsonNode>Response(string? token,HttpRequestMessage request,int status){if(token is not null)request.Headers.Authorization=new("Bearer",token);using var r=await Client.SendAsync(request);var text=await r.Content.ReadAsStringAsync();Check((int)r.StatusCode==status,$"{request.Method} {request.RequestUri}: expected {status}, got {(int)r.StatusCode}: {text}");if(status!=401 && !request.RequestUri!.AbsolutePath.EndsWith("/expire",StringComparison.Ordinal))Check(r.Headers.CacheControl?.NoStore==true,"private/error responses are no-store");if(status==429)Check(r.Headers.RetryAfter is not null,"rate limit retry header");return string.IsNullOrWhiteSpace(text)?new JsonObject():JsonNode.Parse(text)!;}
        public async Task Expire(Guid id){if(Data is null){Clock.Now=Clock.Now.AddHours(2);return;}await Sql("UPDATE auction_escrow_listings SET expires_at=statement_timestamp()-interval '1 hour',payload=jsonb_set(payload,'{State}',(payload->'State')||jsonb_build_object('CreatedAt',statement_timestamp()-interval '2 hours','ExpiresAt',statement_timestamp()-interval '1 hour')) WHERE listing_id=@id",("id",id));}
        public async Task Sql(string sql,params(string Name,object Value)[] values){await using var c=await Data!.OpenConnectionAsync();await using var q=new NpgsqlCommand(sql,c);foreach(var v in values)q.Parameters.AddWithValue(v.Name,v.Value);await q.ExecuteNonQueryAsync();}
        public async Task Mode(string mode){ProjectRpg.Operations.Core.ServiceControl.IServiceControlReader reader;ProjectRpg.Operations.Core.ServiceControl.IServiceControlCommitter writer;if(Data is null){var s=new MemoryServiceControlStore((InMemoryGameRepository)Game,Clock);reader=s;writer=s;}else{var s=new PostgresServiceControlStore(Data,Clock);reader=s;writer=s;}var current=await reader.ReadAsync(default);var r=await writer.CommitAsync(new(new(Guid.NewGuid(),current.Value!.Revision,mode,"",null,null,"controlled auction API fixture"),"test",Guid.NewGuid(),null),default);Check(r.IsSuccess,"mode fixture");}
        public async Task Restrict(Person p){ProjectRpg.Operations.Core.Restrictions.IAccountRestrictionCommitter s=Data is null?new MemoryRestrictionStore((InMemoryGameRepository)Game,Clock):new PostgresRestrictionStore(Data,Clock);Check((await s.CommitAsync(new(new(Guid.NewGuid(),p.Account,0,null,"controlled auction API fixture"),"test",Guid.NewGuid(),null),default)).IsSuccess,"restriction fixture");}
        public async ValueTask DisposeAsync(){Client?.Dispose();if(app is not null){await app.StopAsync();await app.DisposeAsync();}if(Data is not null)await Data.DisposeAsync();}
    }
}
