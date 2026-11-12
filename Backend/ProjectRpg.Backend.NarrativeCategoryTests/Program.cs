using System.Collections.Immutable;
using System.Net;
using System.Net.Http.Headers;
using System.Text;
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

try
{
    Check(args.Length == 1, "Expected an owned output fixture path");
    Definitions();
    await Runtime(args[0]);
    Console.WriteLine("PASS category definitions, optional progression, compatibility and actual authenticated HTTP");
    return 0;
}
catch (Exception error)
{
    Console.Error.WriteLine("FAIL category: "+error.Message);
    Console.Error.WriteLine(error.StackTrace);
    return 1;
}

static void Check(bool valid, string message) { if (!valid) throw new InvalidOperationException(message); }
static QuestDefinition Q(string id, QuestCategory category = QuestCategory.Unspecified, params string[] parents)
    => new(id, 1, [.. parents], ["talk"], id+" title", Category: category);
static NarrativeCatalog Catalog(QuestDefinition[] quests, params ChapterDefinition[] chapters) => new(1, quests, chapters,
    quests.SelectMany(q => new DialogueDefinition[] {
        new(q.Id+".Offer", "Guide", "Offer", q.Id, null, [new("accept", "Accept", DialogueEffect.AcceptQuest)]),
        new(q.Id+".Talk", "Guide", "Talk", q.Id, QuestStatus.Active, [new("talk", "Talk", DialogueEffect.RecordObjective,"talk")]),
        new(q.Id+".Report", "Guide", "Report", q.Id, QuestStatus.ReadyToReport, [new("report", "Report", DialogueEffect.ReportQuest)]) }));
static void Reject(Func<NarrativeCatalog> action, string code)
{
    try { action(); } catch (Exception error) when (error.Message == code) { return; }
    throw new InvalidOperationException("Expected "+code);
}
static void Definitions()
{
    var main=Q("Main",QuestCategory.Main);var side=Q("Side",QuestCategory.Side);var legacy=Q("Legacy");
    _=Catalog([main,side,legacy], new ChapterDefinition("Chapter",[main.Id],["Story"],AllowStorySkip:true));
    _=Catalog([main,side with {Prerequisites=[main.Id]},legacy]); // side can follow the main story
    _=Catalog([side,legacy with {Prerequisites=[side.Id]}]); // standalone unclassified content is not relabelled
    Reject(()=>Catalog([main with {Category=(QuestCategory)999}]),"invalid_quest_category");
    Reject(()=>Catalog([main with {Prerequisites=[side.Id]},side]),"side_quest_required_by_main");
    Reject(()=>Catalog([main with {Prerequisites=[legacy.Id]},legacy with {Prerequisites=[side.Id]},side]),"side_quest_required_by_main");
    Reject(()=>Catalog([main,side],new ChapterDefinition("Chapter",[side.Id],[])),"side_quest_required_by_story");
    Reject(()=>Catalog([legacy with {Prerequisites=[side.Id]},side],new ChapterDefinition("Chapter",[legacy.Id],[])),"side_quest_required_by_story");
    var legacyCatalog=Catalog([legacy],new ChapterDefinition("OldChapter",[legacy.Id],[],AllowStorySkip:true));
    Check(legacyCatalog.Quests[legacy.Id].Category==QuestCategory.Unspecified,"Legacy default changed");

    var options=new JsonSerializerOptions(JsonSerializerDefaults.Web);
    var bytes=JsonSerializer.SerializeToUtf8Bytes(new { schemaVersion=1,catalogVersion=1,
        quests=new[]{ new { id="Q",version=1,prerequisites=Array.Empty<string>(),objectives=new[]{"talk"},category="Side" } },
        chapters=Array.Empty<object>(), dialogues=Catalog([Q("Q")]).Dialogues.Values.Select(d=>new {
            d.Id,d.Speaker,d.Text,d.QuestId,requiredStatus=d.RequiredStatus?.ToString(),
            choices=d.Choices.Select(c=>new {c.Id,c.Text,effect=c.Effect.ToString(),c.ObjectiveId})}) }, options);
    Check(NarrativeCatalogLoader.Parse(bytes).Quests["Q"].Category==QuestCategory.Side,"Authored Side not loaded");
    string text=Encoding.UTF8.GetString(bytes);
    foreach(string invalid in new[]{"42","true","null","\"Unknown\"","\"\""})
    {
        try {NarrativeCatalogLoader.Parse(Encoding.UTF8.GetBytes(text.Replace("\"Side\"",invalid,StringComparison.Ordinal)));}
        catch(NarrativeCatalogException) {continue;}
        throw new InvalidOperationException("Invalid category accepted: "+invalid);
    }
    Check(NarrativeCatalogLoader.Parse(Encoding.UTF8.GetBytes(text.Replace(",\"category\":\"Side\"","",StringComparison.Ordinal))).Quests["Q"].Category==QuestCategory.Unspecified,
        "Omitted authored category is not legacy compatible");
    Console.WriteLine("PASS definitions: enum, JSON types, direct/transitive optional prerequisites and legacy defaults");
}

static async Task Runtime(string output)
{
    var gate=new InMemoryTransactionGate();var game=new InMemoryGameRepository(gate,new InMemoryItemRepository(gate));
    const string owner="76561198000009413";
    var now=DateTimeOffset.UtcNow;
    await game.UpsertAccountAsync(new(owner,owner,false,false),now,default);
    var hero=await game.CreateCharacterAsync(owner,"CategoryHero",now,default);
    var target=await game.CreateCharacterAsync(owner,"CategoryAlt",now,default);
    QuestDefinition[] definitions=[Q("Main",QuestCategory.Main),Q("Side",QuestCategory.Side),Q("Legacy")];
    var catalog=Catalog(definitions,new ChapterDefinition("Chapter",["Main"],["Story"],AllowStorySkip:true));
    var commands=new NarrativeCoordinator(game,catalog,new(),new(),new(new()),TimeProvider.System);
    async Task Choose(GameCharacter character,string node,string choice)
    {
        var state=await game.LoadNarrativeAsync(character.CharacterId,owner,default);
        await commands.ChooseAsync(character.CharacterId,owner,new(Guid.NewGuid(),state.Revision,1,node,choice),default);
    }
    await Choose(hero,"Main.Offer","accept");await Choose(hero,"Main.Talk","talk");await Choose(hero,"Main.Report","report");
    var done=await commands.LoadAsync(hero.CharacterId,owner,default);
    Check(done.State.CompletedChapters.Contains("Chapter") && done.State.StoryUnlocks.Contains("Story")
        && done.State.Quests.All(q=>q.QuestId!="Side"),"Main completion required optional side quest");
    var saved=JsonSerializer.Serialize(done.State);
    var reloadedCatalog=Catalog([definitions[0] with { Category=QuestCategory.Unspecified },definitions[1],definitions[2]],
        new ChapterDefinition("Chapter",["Main"],["Story"],AllowStorySkip:true));
    var reloaded=new NarrativeCoordinator(game,reloadedCatalog,new(),new(),new(new()),TimeProvider.System);
    Check(JsonSerializer.Serialize((await reloaded.LoadAsync(hero.CharacterId,owner,default)).State)==saved,"Category changed saved quest facts");
    await new NarrativeStorySkip(game,catalog,commands,TimeProvider.System).PreviewAsync(target.CharacterId,owner,"Chapter",default);
    // No preview or display metadata may fake completion or grant growth.
    Check((await game.LoadNarrativeAsync(target.CharacterId,owner,default)).Revision==0,"Preview mutated target");
    foreach(var q in definitions)await Choose(target,q.Id+".Offer","accept");
    var before=JsonSerializer.Serialize(await game.LoadNarrativeAsync(target.CharacterId,owner,default));
    var tokens=new AccessTokenService(TimeProvider.System);var token=tokens.Issue(TimeSpan.FromMinutes(5));
    await game.StoreSessionAsync(token.TokenHash,owner,token.ExpiresAt,default);
    var builder=WebApplication.CreateBuilder(new WebApplicationOptions{EnvironmentName="Testing"});
    builder.Logging.ClearProviders();builder.WebHost.UseUrls("http://127.0.0.1:0");
    builder.Services.AddSingleton<IGameRepository>(game);builder.Services.AddSingleton(tokens);builder.Services.AddSingleton<TimeProvider>(TimeProvider.System);
    builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions()));
    builder.Services.Configure<SecurityTelemetryOptions>(_=>{});builder.Services.AddScoped<BearerAuthenticator>();
    builder.Services.AddSingleton(commands);builder.Services.AddSingleton(new NarrativeAccountHistory(game,catalog));
    await using var app=builder.Build();app.UseMiddleware<ApiAuthenticationMiddleware>();app.MapNarrativeApi();await app.StartAsync();
    try
    {
        using var client=new HttpClient{BaseAddress=new Uri(app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single())};
        string route=$"/api/characters/{target.CharacterId}/narrative";
        using var anonymous=await client.GetAsync(route);Check(anonymous.StatusCode==HttpStatusCode.Unauthorized,"Snapshot lost auth gate");
        client.DefaultRequestHeaders.Authorization=new AuthenticationHeaderValue("Bearer",token.Token);
        using var response=await client.GetAsync(route);response.EnsureSuccessStatusCode();
        Check(response.Headers.CacheControl?.NoStore==true,"Snapshot caching changed");
        var wire=await response.Content.ReadAsByteArrayAsync();using var json=JsonDocument.Parse(wire);
        var quests=json.RootElement.GetProperty("quests").EnumerateArray().ToArray();
        foreach(var q in definitions)Check(quests.Single(e=>e.GetProperty("questId").GetString()==q.Id).GetProperty("category").GetString()==q.Category.ToString(),"HTTP category mismatch");
        Check(before==JsonSerializer.Serialize(await game.LoadNarrativeAsync(target.CharacterId,owner,default)),"Read changed persistence");
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(output))!);await File.WriteAllBytesAsync(output,wire);
    }
    finally {await app.StopAsync();}
    Console.WriteLine("PASS actual authenticated GET emits main/side/legacy categories; main progression, saved state and skip eligibility preserved");
}
