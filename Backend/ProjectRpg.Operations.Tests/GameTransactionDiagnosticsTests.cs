using System.Diagnostics;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Diagnostics;

internal static class GameTransactionDiagnosticsTests
{
    private const string Secret = "private-tx-76561198077777777@example.invalid";
    private const string ParentTrace = "4bf92f3577b34da6a3ce929d0e0e4736";
    private const string ItemOperation = "PrivateCallerItemOperation";
    private const string CurrencyOperation = "PrivateCallerCurrencyOperation";

    internal static async Task RunAsync()
    {
        string root = Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")!;
        await using (var off = await HostFixture.StartAsync(Path.Combine(root,"transaction-off")))
            Check.That(off.App.Services.GetService<IGameTransactionObserver>() is null,"Receipt observation is off with game HTTP diagnostics.");
        using var listener = new ActivityListener { ShouldListenTo = _ => true,
            Sample = (ref ActivityCreationOptions<ActivityContext> _) => ActivitySamplingResult.AllDataAndRecorded };
        ActivitySource.AddActivityListener(listener);
        foreach (string? pg in new[] { (string?)null, Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_POSTGRES") }.Distinct())
            await Http(Path.Combine(root,pg is null?"transaction-memory":"transaction-postgres"),pg);
        await Http(Path.Combine(root,"transaction-broken-observer"),null,new Capture { Throw = true });
        await RepositoryFailure();
        Console.WriteLine("PASS Game transaction diagnostics: real item/currency HTTPS commits and replays, receipt-to-HTTP/trace search, rejection/privacy boundaries, unchanged replay state and optional observer failure isolation.");
    }

    private static void Configure(IServiceCollection services)
    {
        services.AddGameRepositoryContracts();
        services.AddSingleton<ItemOwnerAccessService>();
        services.AddSingleton<ItemTransactionService>();
        services.AddSingleton<CurrencyTransactionService>();
    }

    private static async Task Http(string root, string? pg, Capture? broken = null)
    {
        await using var host = await HostFixture.StartAsync(root,pg,gameDiagnostics:true,
            customize:services=>{Configure(services);if(broken is not null)services.AddSingleton<IGameTransactionObserver>(broken);},
            customizeRoutes:app=>{app.MapItemApi();app.MapEconomyApi();});
        await host.SeedAsync();
        var game = host.App.Services.GetRequiredService<IGameRepository>();
        var items = host.App.Services.GetRequiredService<IItemRepository>();
        var economy = host.App.Services.GetRequiredService<IEconomyRepository>();
        var clock = host.Clock;
        var character = await game.CreateCharacterAsync(HostFixture.Account,"Tx"+Guid.NewGuid().ToString("N")[..10],clock.Now,default);
        var session = await game.CreateDungeonSessionAsync(HostFixture.Account,character.CharacterId,"training","Normal",clock.Now,clock.Now.AddHours(1),default);
        const string serverId = "transaction-diagnostics-fixture";
        await game.AssignDungeonServerAsync(session.DungeonSessionId,serverId,"127.0.0.1:7777",clock.Now,clock.Now.AddHours(1),default);
        await game.StartDungeonSessionAsync(session.DungeonSessionId,serverId,clock.Now,clock.Now.AddHours(1),default);
        var token = host.App.Services.GetRequiredService<AccessTokenService>().Issue(TimeSpan.FromHours(1));
        await game.StoreGameServerCredentialAsync(token.TokenHash,serverId,session.DungeonSessionId,token.ExpiresAt,default);

        var command = ItemCommand(character.CharacterId);
        var itemContract = new ItemCommitRequestContract(command.RequestId,command.Operation,command.CommandFingerprint,
            new("Character",command.Actor.OwnerId),command.AffectedQuantity,
            [new(0,Contract(command.Mutations[0].NewRecord))]);
        var currencyContract = new CurrencyTransactionRequestContract(Guid.NewGuid(),character.CharacterId,session.DungeonSessionId,
            CurrencyOperation,Secret,Secret,[new("OPS_COIN",7)]);
        async Task<Guid> Send(string path, object body, int status, string? bearer = null, string? disposition = null)
        {
            using var request = new HttpRequestMessage(HttpMethod.Post,path) { Content=JsonContent.Create(body,body.GetType(),options:OperationsHttp.Json) };
            request.Headers.Authorization = new AuthenticationHeaderValue("Bearer",bearer??token.Token);
            request.Headers.Add("traceparent","00-"+ParentTrace+"-00f067aa0ba902b7-01");
            request.Headers.Add("X-Game-Request-Id","untrusted-business-trace");
            request.Headers.Add("X-Private-Input",Secret);
            using var response = await host.Client.SendAsync(request);
            string text = await response.Content.ReadAsStringAsync();
            Check.That((int)response.StatusCode==status,$"Transaction expected {status}, got {(int)response.StatusCode}: {text}");
            if(disposition is not null)
            {
                using var doc=JsonDocument.Parse(text);
                Check.That(doc.RootElement.GetProperty("status").GetString()==disposition,"Original business receipt disposition is preserved.");
            }
            Check.That(response.Headers.GetValues(GameHttpDiagnostics.TraceIdHeader).Single()==ParentTrace,"Receipt stays in the actual HTTP Activity.");
            return Guid.Parse(response.Headers.GetValues(GameHttpDiagnostics.RequestIdHeader).Single());
        }
        const string itemPath="/api/item-transactions/commit", currencyPath="/api/economy/transactions/commit";
        Guid itemFirst=await Send(itemPath,itemContract,200,disposition:"Committed");
        Guid currencyFirst=await Send(currencyPath,currencyContract,200,disposition:"Committed");
        string itemReceipt=JsonSerializer.Serialize(await items.TryGetCommitResultAsync(command.RequestId,default));
        string currencyReceipt=JsonSerializer.Serialize(await economy.TryGetTransactionResultAsync(currencyContract.RequestId,default));
        string? fingerprint=pg is null?null:await HostTests.FingerprintAsync(host);
        Guid itemRetry=await Send(itemPath,itemContract,200,disposition:"AlreadyCommitted");
        Guid currencyRetry=await Send(currencyPath,currencyContract,200,disposition:"AlreadyCommitted");
        Check.That(new[]{itemFirst,itemRetry,currencyFirst,currencyRetry}.Distinct().Count()==4,"Each receipt attempt retains its own HTTP number.");
        var record=command.Mutations[0].NewRecord;
        await Send(itemPath,itemContract with { Mutations=[new(0,Contract(record with { State=record.State with { Quantity=8 } }))] },409,disposition:"IdempotencyConflict");
        await Send(currencyPath,currencyContract with { Changes=[new("OPS_COIN",8)] },409,disposition:"IdempotencyConflict");
        Guid unauthorizedId=Guid.NewGuid();
        await Send(currencyPath,currencyContract with { RequestId=unauthorizedId,CharacterId=host.CharacterId },403,disposition:"SessionNotAuthorized");
        await Send(itemPath,itemContract with { RequestId=Guid.Empty },400);
        await Send(itemPath,itemContract,403,"fixture-admin-token");
        await Send(currencyPath,currencyContract,403,"fixture-admin-token");
        await Send(itemPath,itemContract,401,"invalid-private-token");

        // A direct background service call does not inherit or invent a request scope.
        if(broken is null)
            Check.That((await host.App.Services.GetRequiredService<ItemTransactionService>().CommitAsync(command,default)).Status
                ==ItemRepositoryCommitStatus.AlreadyCommitted,"Background replay remains supported without fictitious HTTP evidence.");
        Check.That(itemReceipt==JsonSerializer.Serialize(await items.TryGetCommitResultAsync(command.RequestId,default))
            &&currencyReceipt==JsonSerializer.Serialize(await economy.TryGetTransactionResultAsync(currencyContract.RequestId,default)),"Replay/conflict/observation leaves original immutable receipts unchanged.");
        Check.That((await items.FindAsync(record.ItemId,default)) is { Revision:1,State.Quantity:7 },"Item was applied once.");
        var context=await game.GetCharacterEconomyContextAsync(character.CharacterId,default);
        Check.That((await economy.LoadWalletAsync(context!,default)).Balances.Single(b=>b.Definition.CurrencyCode=="OPS_COIN").Balance==7,"Currency was applied once.");
        using(var denied=await host.Client.GetAsync("/ops/api/logs?transactionRequestId="+command.RequestId))
            Check.That(denied.StatusCode==HttpStatusCode.Unauthorized,"A known receipt ID grants no diagnostics permission.");
        await host.LoginAsync();
        if(broken is null)
        {
            foreach(var expected in new[]{(command.RequestId,itemFirst,itemRetry,"ItemTransaction.Commit"),(currencyContract.RequestId,currencyFirst,currencyRetry,"CurrencyTransaction.Commit")})
            {
                var found=await Search(host,"transactionRequestId",expected.Item1,2);
                Check.That(found.Select(e=>e.GetProperty("httpRequestId").GetGuid()).ToHashSet().SetEquals([expected.Item2,expected.Item3])
                    &&found.All(e=>e.GetProperty("traceId").GetString()==ParentTrace&&e.GetProperty("operationName").GetString()==expected.Item4),"Receipt search connects both HTTP attempts with real trace and fixed operation.");
            }
            var attempt=await Search(host,"httpRequestId",itemFirst,3);
            Check.That(attempt.Select(e=>e.GetProperty("eventName").GetString()).ToHashSet().SetEquals(
                ["game.http.started","game.transaction.receipt.committed","game.http.completed"]),"HTTP search includes the confirmed receipt between transport observations.");
        }
        if(fingerprint is not null)Check.That(fingerprint==await HostTests.FingerprintAsync(host),"Replay/refusal and operator log search do not mutate gameplay tables.");
        await host.StopAsync();
        var logs=Directory.GetFiles(Path.Combine(root,"logs"),"*.jsonl");
        string raw=string.Join('\n',logs.Select(File.ReadAllText));
        Check.That(!raw.Contains(Secret)&&!raw.Contains(ItemOperation)&&!raw.Contains(CurrencyOperation)&&!raw.Contains(token.Token)
            &&!raw.Contains("untrusted-business-trace"),"No body, caller operation/reason/fingerprint, credential or custom header crosses the log boundary.");
        var receipts=logs.SelectMany(File.ReadLines).Select(line=>JsonDocument.Parse(line)).Select(doc=>
        {using(doc)return doc.RootElement.GetProperty("Properties").GetProperty("OpsEvent").Deserialize<LogEvent>(OperationsHttp.Json)!;})
            .Where(e=>e.EventName.StartsWith("game.transaction.receipt.",StringComparison.Ordinal)).ToArray();
        if(broken is null)
        {
            Check.That(receipts.Length==4&&receipts.Count(e=>e.Outcome=="Committed")==2&&receipts.Count(e=>e.Outcome=="Replayed")==2,
                "Only confirmed/replayed results become receipt events; rejected/unscoped calls do not.");
            Check.That(receipts.All(e=>e.DungeonSessionId is null&&e.ItemId is null&&e.OperationRequestId is null&&e.OperatorId is null&&e.HttpStatus is null)
                &&receipts.Where(e=>e.TransactionRequestId==command.RequestId).All(e=>e.CharacterId is null)
                &&receipts.Where(e=>e.TransactionRequestId==currencyContract.RequestId).All(e=>e.CharacterId==character.CharacterId),"Only authoritative receipt character context is used; no session/owner/transport status is guessed.");
        }
        else
            Check.That(receipts.Length==0&&broken.Seen.Count==4&&broken.Seen.Count(e=>e.Disposition==GameTransactionDisposition.Committed)==2,
                "Both services preserve commits and replays when every observer call throws.");
    }

    private static async Task<JsonElement[]> Search(HostFixture host,string field,Guid id,int count)
    {
        string from=Uri.EscapeDataString(host.Clock.Now.AddMinutes(-1).UtcDateTime.ToString("O"));
        string to=Uri.EscapeDataString(host.Clock.Now.AddMinutes(1).UtcDateTime.ToString("O"));
        JsonElement[] events=[];
        for(int attempt=0;attempt<40;attempt++)
        {
            using var response=await host.Client.GetAsync($"/ops/api/logs?{field}={id}&fromUtc={from}&toUtc={to}");
            Check.That(response.IsSuccessStatusCode,"Protected persisted receipt search succeeds.");
            using var doc=JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            events=doc.RootElement.GetProperty("data").GetProperty("events").EnumerateArray().Select(e=>e.Clone()).ToArray();
            if(events.Length==count)break;
            await Task.Delay(50);
        }
        Check.That(events.Length==count,"Persisted search returns exactly the confirmed evidence.");
        return events;
    }

    private static ItemRepositoryCommitRequest ItemCommand(Guid characterId)
    {
        var owner=new ItemOwnerRef(ItemOwnerType.Character,characterId.ToString("D"));
        var record=new ItemRecord("RPGItemDefinition","Potion",1,owner,new(ItemContainerType.Inventory,owner.OwnerId,0),
            new(Guid.NewGuid(),7,7,[],[]),0,ItemLifecycleState.Active,
            new(ItemBindState.Unbound,new(5,10),null,Secret,false));
        return new(Guid.NewGuid(),ItemOperation,Secret,owner,7,[new(0,record)]);
    }

    private static ItemRecordContract Contract(ItemRecord record) => new(record.DefinitionType,record.DefinitionName,record.DefinitionVersion,
        new(record.Owner.Type.ToString(),record.Owner.OwnerId),new(record.Location.ContainerType.ToString(),record.Location.ContainerId,record.Location.SlotIndex),
        new(record.ItemId,record.State.GenerationSeed,record.State.Quantity,[],[]),record.Revision,record.LifecycleState.ToString(),
        new(record.Metadata.BindState.ToString(),new(record.Metadata.Durability.Current,record.Metadata.Durability.Maximum),
            record.Metadata.ExpiresAtUtc,record.Metadata.CreationSource,record.Metadata.IsLocked));

    private static async Task RepositoryFailure()
    {
        var capture=new Capture();var repo=new BrokenRepository();Exception? observed=null;
        try{await new ItemTransactionService(repo,TimeProvider.System,capture).CommitAsync(ItemCommand(Guid.NewGuid()),default);}
        catch(Exception e){observed=e;}
        Check.That(ReferenceEquals(observed,repo.Failure)&&capture.Seen.Count==0,"Repository exceptions remain unknown and propagate unchanged without a receipt observation.");
    }
    private sealed class Capture : IGameTransactionObserver
    {
        internal bool Throw;
        internal List<GameTransactionObservation> Seen=[];
        public void Observe(GameTransactionObservation observation){Seen.Add(observation);if(Throw)throw new IOException(Secret);}
    }
    private sealed class BrokenRepository : IItemRepository
    {
        internal Exception Failure=new InvalidOperationException(Secret);
        public Task<ItemRepositoryCommitResult?> TryGetCommitResultAsync(Guid id,CancellationToken ct)=>throw Failure;
        public Task<ItemRepositoryCommitResult> CommitAsync(ItemRepositoryCommitRequest request,DateTimeOffset now,CancellationToken ct)=>throw Failure;
        public Task<ItemRecord?> FindAsync(Guid id,CancellationToken ct)=>throw Failure;
        public Task<IReadOnlyList<ItemRecord>> FindByOwnerAsync(ItemOwnerRef owner,bool terminal,int limit,CancellationToken ct)=>throw Failure;
    }
}
