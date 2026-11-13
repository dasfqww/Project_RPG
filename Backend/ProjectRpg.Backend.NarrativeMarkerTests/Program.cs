using System.Net;
using System.Net.Http.Headers;
using System.Text.Json;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Narrative;

try { Check(args.Length==1,"Expected owned fixture output path");await Cases(args[0]);Console.WriteLine("PASS personal NPC marker metadata and actual HTTP");return 0; }
catch(Exception error){Console.Error.WriteLine("FAIL markers: "+error.Message);Console.Error.WriteLine(error.StackTrace);return 1;}
static void Check(bool valid,string reason){if(!valid)throw new InvalidOperationException(reason);}
static void Action(NarrativeView view,string action)
{
    var node=view.AvailableDialogues.Single(d=>d.QuestId=="Q");
    Check(node.QuestAction==action && node.EntryPoint && !node.Informational,"Incorrect authoritative action: "+action);
    var info=view.AvailableDialogues.Single(d=>d.Id=="Info");
    Check(info.QuestId is null && info.QuestAction is null && info.Informational,"Information is not a quest action");
}
static async Task Cases(string output)
{
    var gate=new InMemoryTransactionGate();var game=new InMemoryGameRepository(gate,new InMemoryItemRepository(gate));
    const string owner="76561198000009415",foreign="76561198000009416";var now=DateTimeOffset.UtcNow;
    await game.UpsertAccountAsync(new(owner,owner,false,false),now,default);await game.UpsertAccountAsync(new(foreign,foreign,false,false),now,default);
    GameCharacter character=await game.CreateCharacterAsync(owner,"MarkerPlayer",now,default);
    GameCharacter other=await game.CreateCharacterAsync(foreign,"MarkerForeign",now,default);
    var catalog=new NarrativeCatalog(1,[new("Q",1,[],["talk"],"Marker quest")],[new("Chapter",["Q"],["Story"],AllowStorySkip:true)], [
        new("Offer","Guide","Offer","Q",null,[new("accept","Accept",DialogueEffect.AcceptQuest)]),
        new("Talk","Guide","Talk","Q",QuestStatus.Active,[new("talk","Talk",DialogueEffect.RecordObjective,"talk")]),
        new("Report","Guide","Report","Q",QuestStatus.ReadyToReport,[new("report","Report",DialogueEffect.ReportQuest)]),
        new("Info","Guide","Information",null,null,[new("close","Close",DialogueEffect.None)],Informational:true)]);
    var commands=new NarrativeCoordinator(game,catalog,new(),new(),new(new()),TimeProvider.System);
    var tokens=new AccessTokenService(TimeProvider.System);var token=tokens.Issue(TimeSpan.FromMinutes(5));
    await game.StoreSessionAsync(token.TokenHash,owner,token.ExpiresAt,default);
    var builder=WebApplication.CreateBuilder(new WebApplicationOptions{EnvironmentName="Testing"});builder.Logging.ClearProviders();builder.WebHost.UseUrls("http://127.0.0.1:0");
    builder.Services.AddSingleton<IGameRepository>(game);builder.Services.AddSingleton(tokens);builder.Services.AddSingleton<TimeProvider>(TimeProvider.System);
    builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions()));builder.Services.Configure<SecurityTelemetryOptions>(_=>{});builder.Services.AddScoped<BearerAuthenticator>();
    builder.Services.AddSingleton(commands);builder.Services.AddSingleton(new NarrativeAccountHistory(game,catalog));
    await using var app=builder.Build();app.UseMiddleware<ApiAuthenticationMiddleware>();app.MapNarrativeApi();await app.StartAsync();
    try
    {
        using var client=new HttpClient{BaseAddress=new Uri(app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single())};
        string route=$"/api/characters/{character.CharacterId}/narrative";
        using(var response=await client.GetAsync(route))Check(response.StatusCode==HttpStatusCode.Unauthorized,"Anonymous metadata leaked");
        client.DefaultRequestHeaders.Authorization=new AuthenticationHeaderValue("Bearer",token.Token);
        using(var response=await client.GetAsync($"/api/characters/{other.CharacterId}/narrative"))Check(response.StatusCode==HttpStatusCode.Forbidden,"Other owner metadata leaked");
        async Task<string> Read(string expected)
        {
            string before=JsonSerializer.Serialize(await game.LoadNarrativeAsync(character.CharacterId,owner,default));
            using var response=await client.GetAsync(route);response.EnsureSuccessStatusCode();Check(response.Headers.CacheControl?.NoStore==true,"Private metadata cached");
            string body=await response.Content.ReadAsStringAsync();using var json=JsonDocument.Parse(body);
            var questNodes=json.RootElement.GetProperty("availableDialogues").EnumerateArray().Where(n=>n.GetProperty("questId").ValueKind==JsonValueKind.String).ToArray();
            Check(questNodes.Length==1 && questNodes[0].GetProperty("questAction").GetString()==expected && questNodes[0].GetProperty("questId").GetString()=="Q","HTTP action mismatch");
            Check(before==JsonSerializer.Serialize(await game.LoadNarrativeAsync(character.CharacterId,owner,default)),"Metadata read changed progress/receipt state");return body;
        }
        async Task Choose(string node,string choice)
        {
            var state=await game.LoadNarrativeAsync(character.CharacterId,owner,default);
            await commands.ChooseAsync(character.CharacterId,owner,new(Guid.NewGuid(),state.Revision,1,node,choice),default);
        }
        Action(await commands.LoadAsync(character.CharacterId,owner,default),"Accept");await Read("Accept");
        await Choose("Offer","accept");Action(await commands.LoadAsync(character.CharacterId,owner,default),"Progress");await Read("Progress");
        await Choose("Talk","talk");Action(await commands.LoadAsync(character.CharacterId,owner,default),"Report");string wire=await Read("Report");
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(output))!);await File.WriteAllTextAsync(output,wire);
        await Choose("Report","report");var done=await commands.LoadAsync(character.CharacterId,owner,default);
        Check(done.AvailableDialogues.All(d=>d.QuestId is null) && done.State.Revision==3 && done.State.CompletedChapters.Contains("Chapter"),"Completed quest still offers a marker");
        var raw=JsonSerializer.Serialize(done.State);await commands.LoadAsync(character.CharacterId,owner,default);
        Check(raw==JsonSerializer.Serialize(await game.LoadNarrativeAsync(character.CharacterId,owner,default)),"Read mutated completed state");
        Console.WriteLine("PASS actual player-only HTTP Accept/Progress/Report metadata, private scope/no-store, completion and read-only state");
    }
    finally {await app.StopAsync();}
}
