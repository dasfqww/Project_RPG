using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

// Real loopback HTTP, production authentication/mapper/service/repositories; no UE or PG claims.
int assertions = 0;
void Check(bool value, string message) { ++assertions; if (!value) throw new InvalidOperationException(message); }
var now = DateTimeOffset.UtcNow;
var gate = new InMemoryTransactionGate(); IGameRepository game = new InMemoryGameRepository();
IEconomyRepository economy = new InMemoryEconomyRepository(gate);
await game.EnsureCreatedAsync(default);
async Task<GameCharacter> Person(string account)
{
    await game.UpsertAccountAsync(new(account, account, false, false), now, default);
    return await game.CreateCharacterAsync(account, "L" + Guid.NewGuid().ToString("N")[..16], now, default);
}
var a = await Person("76561198000000001"); var b = await Person("76561198000000002"); var outsider = await Person("76561198000000003");
var session = await game.CreateDungeonSessionAsync(a.SteamId,a.CharacterId,"Dungeon.Loot","Normal",now,now.AddHours(1),default);
await game.JoinDungeonSessionAsync(session.DungeonSessionId,b.SteamId,b.CharacterId,4,now,now.AddHours(1),default);
const string serverId = "loot-regression";
await game.AssignDungeonServerAsync(session.DungeonSessionId,serverId,"127.0.0.1:7777",now,now.AddHours(1),default);
await game.StartDungeonSessionAsync(session.DungeonSessionId,serverId,now,now.AddHours(1),default);
await economy.UpsertDefinitionAsync(new("RosterGold","Gold",CurrencyScope.Roster,1000,true),now,default);
await economy.UpsertDefinitionAsync(new("DisabledGold","Disabled",CurrencyScope.Character,1000,false),now,default);
var tokens = new AccessTokenService(TimeProvider.System); var server = tokens.Issue(TimeSpan.FromHours(1)); var player = tokens.Issue(TimeSpan.FromHours(1));
await game.StoreGameServerCredentialAsync(server.TokenHash,serverId,session.DungeonSessionId,server.ExpiresAt,default);
await game.StoreSessionAsync(player.TokenHash,a.SteamId,player.ExpiresAt,default);
var builder=WebApplication.CreateBuilder(); builder.Configuration.Sources.Clear(); builder.Configuration.AddInMemoryCollection();
builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
builder.Services.AddSingleton(game); builder.Services.AddSingleton<ICurrencyAuthorityRepository>(game); builder.Services.AddSingleton(economy);
builder.Services.AddSingleton<TimeProvider>(TimeProvider.System); builder.Services.AddSingleton(tokens); builder.Services.AddSingleton<CurrencyTransactionService>();
builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions {AdminToken=Guid.NewGuid().ToString("N")}));
builder.Services.AddSingleton<IOptions<SecurityTelemetryOptions>>(Options.Create(new SecurityTelemetryOptions())); builder.Services.AddScoped<BearerAuthenticator>();
await using var app=builder.Build(); int lost=0;
app.Use(async (context,next) =>
{
    if(context.Request.Headers["X-Test-Lose-Ack"] != "1") { await next(context); return; }
    var original=context.Response.Body;
    await using var buffer=new MemoryStream(); context.Response.Body=buffer;
    try { await next(context); if(context.Response.StatusCode==200) { ++lost; context.Abort(); } }
    finally { context.Response.Body=original; }
});
app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapEconomyApi(); await app.StartAsync();
using var client=new HttpClient { BaseAddress=new(app.Urls.Single()), Timeout=TimeSpan.FromSeconds(10) };
CurrencyTransactionRequestContract Command(Guid id, Guid? character=null, long amount=21, string code="RosterGold") =>
    new(id,character??a.CharacterId,session.DungeonSessionId,"WorldLoot","world-loot:"+id.ToString("D"),"World.Loot.Currency",[new(code,amount)]);
async Task<HttpResponseMessage> Send(CurrencyTransactionRequestContract body,string? bearer=null,bool lose=false)
{
    using var request=new HttpRequestMessage(HttpMethod.Post,"/api/economy/transactions/commit");
    request.Headers.Authorization=new AuthenticationHeaderValue("Bearer",bearer??server.Token); request.Content=JsonContent.Create(body);
    if(lose) request.Headers.Add("X-Test-Lose-Ack","1");
    return await client.SendAsync(request);
}
async Task<long> Balance(GameCharacter character)
{
    var context=await game.GetCharacterEconomyContextAsync(character.CharacterId,default);
    return (await economy.LoadWalletAsync(context!,default)).Balances.Single(x=>x.Definition.CurrencyCode=="RosterGold").Balance;
}
var id=Guid.NewGuid(); var command=Command(id);
using(var denied=await Send(command,player.Token)) Check(denied.StatusCode==HttpStatusCode.Forbidden,"Player may not mint a currency pickup");
Check(await Balance(a)==0,"Player rejection must not mutate wallet");
bool lostAck=false;
try { using var ignored=await Send(command,lose:true); }
catch(HttpRequestException) { lostAck=true; }
Check(lostAck && lost==1,"Actual first committed HTTP response must be lost");
Check(await Balance(a)==21,"Lost acknowledgement still committed exactly once");
var replies=await Task.WhenAll(Enumerable.Range(0,4).Select(_=>Send(command)));
foreach(var reply in replies)
{
    using(reply) { var receipt=await reply.Content.ReadFromJsonAsync<CurrencyTransactionResponse>();
        Check(reply.StatusCode==HttpStatusCode.OK && receipt is {Status:"AlreadyCommitted"},"Concurrent exact retry must replay");
        Check(receipt!.RequestId==id && receipt.CharacterId==a.CharacterId && receipt.Changes.Single().Delta==21,"Original receipt identity/amount preserved"); }
}
Check(await Balance(a)==21 && await Balance(b)==0,"Retries must not pay twice or pay the other party member");
using(var swapped=await Send(Command(id,b.CharacterId))) Check(swapped.StatusCode==HttpStatusCode.Conflict,"Same drop ID cannot change recipient");
using(var changed=await Send(Command(id,amount:22))) Check(changed.StatusCode==HttpStatusCode.Conflict,"Same drop ID cannot change amount");
using(var foreign=await Send(Command(Guid.NewGuid(),outsider.CharacterId))) Check(foreign.StatusCode==HttpStatusCode.Forbidden,"Nonmember cannot receive world loot");
using(var disabled=await Send(Command(Guid.NewGuid(),code:"DisabledGold"))) Check(disabled.StatusCode==HttpStatusCode.Conflict,"Disabled currency rejected");
using(var limit=await Send(Command(Guid.NewGuid(),amount:1000))) Check(limit.StatusCode==HttpStatusCode.Conflict,"Wallet limit respected");
Check(await Balance(a)==21 && await Balance(b)==0,"Rejections preserve balances");
using(var next=await Send(Command(Guid.NewGuid(),amount:7))) Check(next.StatusCode==HttpStatusCode.OK,"Distinct next drop may pay");
Check(await Balance(a)==28,"Distinct next drop adds exact amount");
await game.AbandonDungeonSessionAsync(session.DungeonSessionId,a.SteamId,a.CharacterId,now,default);
using(var revoked=await Send(Command(Guid.NewGuid()))) Check(revoked.StatusCode==HttpStatusCode.Forbidden,"Abandoned member cannot collect");
using(var remaining=await Send(Command(Guid.NewGuid(),b.CharacterId,7))) Check(remaining.StatusCode==HttpStatusCode.OK,"Remaining member stays authorized");
Check(await Balance(a)==28 && await Balance(b)==7,"Abandonment does not change previous balances or remaining member ownership");
await app.StopAsync(); Console.WriteLine($"PASS Loot currency HTTP/Memory: {assertions} assertions; committed response lost once; exact parallel replay; ownership/limits/revocation preserved.");
