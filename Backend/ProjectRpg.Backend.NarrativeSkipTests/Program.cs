using System.Collections.Immutable;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Npgsql;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Narrative;

try
{
    Check(args.Length == 0 || args.SequenceEqual(["--postgres"]), "Unknown arguments");
    InMemoryTransactionGate gate = new();
    await Cases(new InMemoryGameRepository(gate, new InMemoryItemRepository(gate)), null, "Memory");
    if (args.Length != 0)
    {
        Check(Guid.TryParseExact(Environment.GetEnvironmentVariable("PROJECT_RPG_NARRATIVE_TEST_RUN"), "N", out _), "Owned runner required");
        var config = new NpgsqlConnectionStringBuilder(Environment.GetEnvironmentVariable("PROJECT_RPG_NARRATIVE_TEST_POSTGRES") ?? "");
        Check(config.Host == "127.0.0.1" && config.Database == "narrative_test", "Private database required");
        await using var source = NpgsqlDataSource.Create(config.ConnectionString);
        var migrations = await PostgresSchemaMigrations.LoadCatalogAsync(AppContext.BaseDirectory, default);
        await PostgresSchemaMigrations.ApplyAsync(source, migrations, default);
        await PostgresSchemaMigrations.ValidateAsync(source, migrations, default);
        await Cases(new PostgresGameRepository(source, NullLogger<PostgresGameRepository>.Instance), source, "Postgres");
    }
    Console.WriteLine("PASS story skip groups"); return 0;
}
catch (Exception e)
{
    Console.Error.WriteLine("FAIL skip: " + (e is Failure or NarrativeConflictException ? e.Message
        : e is PostgresException pg ? "SQLSTATE " + pg.SqlState : e.GetType().Name));
    Console.Error.WriteLine(e.StackTrace); return 1;
}

static NarrativeCatalog Catalog()
{
    QuestDefinition[] quests = [new("Q.First", 1, [], ["talk"], "First"), new("Q.Second", 1, ["Q.First"], ["talk"], "Second"),
        new("Q.Next", 1, ["Q.Second"], ["talk"], "Next"), new("Q.Side", 1, [], ["talk"], "Side")];
    return new(21, quests, [new("CH.One", ["Q.First", "Q.Second"], ["Story.One"], "First chapter", true),
        new("CH.Two", ["Q.Next"], ["Story.Two"], "Second chapter", true), new("CH.Disabled", ["Q.Side"], [], "Optional")],
        quests.SelectMany(q => new DialogueDefinition[] {
            new(q.Id+".Offer", "NPC", "Offer", q.Id, null, [new("accept", "Accept", DialogueEffect.AcceptQuest)]),
            new(q.Id+".Talk", "NPC", "Talk", q.Id, QuestStatus.Active, [new("talk", "Talk", DialogueEffect.RecordObjective, "talk")]),
            new(q.Id+".Report", "NPC", "Report", q.Id, QuestStatus.ReadyToReport, [new("report", "Report", DialogueEffect.ReportQuest)]) }));
}
static async Task Cases(IGameRepository game, NpgsqlDataSource? source, string label)
{
    const string owner = "76561198000000321", other = "76561198000000322";
    var clock = TimeProvider.System; var catalog = Catalog(); var repo = (INarrativeRepository)game;
    NarrativeCoordinator Narrative(IGameRepository g) => new((INarrativeRepository)g, catalog, new(), new(), new(new()), clock);
    NarrativeStorySkip Skip(IGameRepository g) => new((INarrativeStorySkipRepository)g, catalog, Narrative(g), clock);
    var narrative = Narrative(game); var skip = Skip(game);
    async Task<GameCharacter> Character(string steam = owner)
    {
        await game.UpsertAccountAsync(new(steam, steam, false, false), clock.GetUtcNow(), default);
        return await game.CreateCharacterAsync(steam, "S"+Guid.NewGuid().ToString("N")[..12], clock.GetUtcNow(), default);
    }
    async Task Choose(GameCharacter c, string q, string node, string choice)
    {
        var state = await repo.LoadNarrativeAsync(c.CharacterId, c.SteamId, default);
        await narrative.ChooseAsync(c.CharacterId, c.SteamId, new(Guid.NewGuid(), state.Revision, 21, q+"."+node, choice), default);
    }
    async Task Complete(GameCharacter c, string q)
    { await Choose(c,q,"Offer","accept"); await Choose(c,q,"Talk","talk"); await Choose(c,q,"Report","report"); }
    StorySkipCommand Command(StorySkipPreview p) => new(Guid.NewGuid(), long.Parse(p.ExpectedRevision), p.CatalogVersion,
        p.ChapterId, p.PreviewHash, true);
    var hero = await Character(); var alt = await Character(); var foreign = await Character(other);
    await Reject(() => skip.PreviewAsync(alt.CharacterId, owner, "CH.One", default), "story_skip_not_eligible");
    await Complete(hero, "Q.First"); await Complete(hero, "Q.Second");
    await Reject(() => skip.PreviewAsync(foreign.CharacterId, other, "CH.One", default), "story_skip_not_eligible");
    await Reject(() => skip.PreviewAsync(hero.CharacterId, owner, "CH.One", default), "story_skip_already_finished");
    await Reject(() => skip.PreviewAsync(alt.CharacterId, other, "CH.One", default), "character_not_owned");
    await Reject(() => skip.PreviewAsync(alt.CharacterId, owner, "CH.Disabled", default), "story_skip_not_authored");
    await Reject(() => skip.PreviewAsync(alt.CharacterId, owner, "CH.Two", default), "story_skip_not_eligible");
    var initial = await skip.PreviewAsync(alt.CharacterId, owner, "CH.One", default);
    Check(initial.Cost == 0 && !initial.GrantsQuestRewards && initial.RequiresOriginalTraining && initial.Quests.Length == 2,
        "Approved policy or preview scope changed");
    var stale = Command(initial);
    await Reject(() => skip.ApplyAsync(foreign.CharacterId, other, stale, default), "story_skip_not_eligible");
    await Reject(() => skip.ApplyAsync(alt.CharacterId, owner, stale with { Confirmed=false }, default), "invalid_story_skip_command");
    await Reject(() => skip.ApplyAsync(alt.CharacterId, owner, stale with { PreviewHash=new string('a',64) }, default), "story_skip_preview_changed");
    using (var cancelled = new CancellationTokenSource())
    {
        cancelled.Cancel();
        try { await skip.ApplyAsync(alt.CharacterId, owner, stale, cancelled.Token); throw new Failure("Cancelled skip committed"); }
        catch (OperationCanceledException) { }
    }
    Check((await repo.LoadNarrativeAsync(alt.CharacterId,owner,default)).Revision == 0, "Preview/rejection wrote state");
    await Choose(alt,"Q.Side","Offer","accept");
    await Choose(alt,"Q.First","Offer","accept"); await Choose(alt,"Q.First","Talk","talk");
    var before = await repo.LoadNarrativeAsync(alt.CharacterId,owner,default);
    string facts = JsonSerializer.Serialize(before.Quests);
    await Reject(() => skip.ApplyAsync(alt.CharacterId, owner, stale, default), "narrative_revision_conflict");
    var preview = await skip.PreviewAsync(alt.CharacterId, owner, "CH.One", default); var command = Command(preview);
    await Reject(() => skip.ApplyAsync(alt.CharacterId, owner, command with { PreviewHash=initial.PreviewHash }, default), "story_skip_preview_changed");
    string? growth = source is null ? null : await Growth(source);
    var growthBefore = await ((ICharacterProgressionRepository)game).GetStoredProgressionSummariesAsync(owner, default);
    var skipped = await skip.ApplyAsync(alt.CharacterId, owner, command, default);
    Check(skipped.State.Revision == before.Revision+1 && skipped.State.StorySkips.Length == 1
        && skipped.State.StoryUnlocks.SequenceEqual(["Story.One"]) && JsonSerializer.Serialize(skipped.State.Quests) == facts,
        "Skip fabricated progress or lost partial facts");
    Check(skipped.Journal.All(q=>q.QuestId!="Q.First") && skipped.Journal.Any(q=>q.QuestId=="Q.Side")
        && skipped.AvailableDialogues.Any(d=>d.Id=="Q.Next.Offer")
        && skipped.AvailableDialogues.All(d=>!d.Id.StartsWith("Q.First.")&&!d.Id.StartsWith("Q.Second.")), "Skipped quests reopened or next story stayed locked");
    var growthAfter = await ((ICharacterProgressionRepository)game).GetStoredProgressionSummariesAsync(owner, default);
    Check(JsonSerializer.Serialize(growthBefore)==JsonSerializer.Serialize(growthAfter), "Growth/skill state changed");
    Check(!FirstSliceProgression.CanEnter(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(
        alt.CharacterId,clock.GetUtcNow(),default),FirstSliceProgression.Boss,"Normal"),"Story skip bypassed actual training milestones");
    if(source is not null)Check(growth==await Growth(source),"PG progression changed");
    var history = await new NarrativeAccountHistory((INarrativeHistoryRepository)game,catalog).LoadAsync(alt.CharacterId,owner,default);
    Check(history.Chapters.Single(c=>c.ChapterId=="CH.One") is { CompletedOnSelectedCharacter:false,CompletedOnOtherCharacter:true },
        "Skip became direct account completion");
    var isolated = new ContextRepository(new([skipped.State, NarrativeState.Create(Guid.NewGuid())],false));
    await Reject(()=>new NarrativeStorySkip(isolated,catalog,narrative,clock).PreviewAsync(skipped.State.CharacterId,owner,"CH.One",default),"story_skip_already_finished");
    var emptyId=isolated.Context.AccountStates[1].CharacterId;
    await Reject(()=>new NarrativeStorySkip(isolated,catalog,narrative,clock).PreviewAsync(emptyId,owner,"CH.One",default),"story_skip_not_eligible");
    await Reject(()=>Choose(alt,"Q.First","Report","report"),"dialogue_not_available");
    await Complete(alt,"Q.Next");
    var later = await repo.LoadNarrativeAsync(alt.CharacterId,owner,default);
    var replay = await skip.ApplyAsync(alt.CharacterId,owner,command,default);
    Check(replay.State.Revision==skipped.State.Revision && (await repo.LoadNarrativeAsync(alt.CharacterId,owner,default)).Revision==later.Revision,
        "Replay rewrote the current state");
    await Reject(()=>skip.ApplyAsync(alt.CharacterId,owner,command with { ChapterId="CH.Two" },default),"idempotency_conflict");
    await Reject(()=>narrative.ChooseAsync(alt.CharacterId,owner,new(command.RequestId,command.ExpectedRevision,21,"Q.Side.Talk","talk"),default),"idempotency_conflict");
    Console.WriteLine("PASS "+label+": policy, ownership, read-only preview, stale plans, partial/side facts, follow-up story, direct-history isolation, growth and replay");

    var concurrent=await Character(); var cp=await skip.PreviewAsync(concurrent.CharacterId,owner,"CH.One",default);var cc=Command(cp);
    var both=await Task.WhenAll(Task.Run(()=>skip.ApplyAsync(concurrent.CharacterId,owner,cc,default)),Task.Run(()=>skip.ApplyAsync(concurrent.CharacterId,owner,cc,default)));
    Check(both.All(v=>v.State.Revision==1)&&(await repo.LoadNarrativeAsync(concurrent.CharacterId,owner,default)).StorySkips.Length==1,"Concurrent same-ID skip duplicated");
    var racing=await Character(); var rp=await skip.PreviewAsync(racing.CharacterId,owner,"CH.One",default);
    async Task<bool> Race(){try{await skip.ApplyAsync(racing.CharacterId,owner,Command(rp),default);return true;}catch(NarrativeConflictException e)when(e.Message=="narrative_revision_conflict"){return false;}}
    Check((await Task.WhenAll(Task.Run(Race),Task.Run(Race))).Count(v=>v)==1,"Concurrent distinct commands did not serialize");
    var busy=await Character();var bp=await skip.PreviewAsync(busy.CharacterId,owner,"CH.One",default);
    await game.CreateDungeonSessionAsync(owner,busy.CharacterId,"test_dungeon","Normal",clock.GetUtcNow(),clock.GetUtcNow().AddHours(1),default);
    await Reject(()=>skip.ApplyAsync(busy.CharacterId,owner,Command(bp),default),"story_skip_dungeon_busy");
    await Reject(()=>skip.PreviewAsync(busy.CharacterId,owner,"CH.One",default),"story_skip_dungeon_busy");
    await game.CreateDungeonSessionAsync(owner,concurrent.CharacterId,"test_dungeon","Normal",clock.GetUtcNow(),clock.GetUtcNow().AddHours(1),default);
    Check((await skip.ApplyAsync(concurrent.CharacterId,owner,cc,default)).State.Revision==1,"Active-session receipt replay lost");
    Console.WriteLine("PASS "+label+": real task races and active-session fences; replay remains a read");

    var bad=skipped.State with { StorySkips=[skipped.State.StorySkips[0] with { QuestIds=["Q.Side"] }] };
    await Reject(()=>new NarrativeStorySkip(new ContextRepository(new([bad],false)),catalog,narrative,clock).PreviewAsync(alt.CharacterId,owner,"CH.One",default),"invalid_story_skip_state");
    // Read old serialized saves: the absent additive field is an empty array, not a fabricated skip.
    var legacyJson=System.Text.Json.Nodes.JsonNode.Parse(JsonSerializer.Serialize(NarrativeState.Create(Guid.NewGuid())))!.AsObject();
    Check(legacyJson.Remove(nameof(NarrativeState.StorySkips)),"Legacy fixture did not remove the new field");
    var legacy=JsonSerializer.Deserialize<NarrativeState>(legacyJson.ToJsonString());
    Check(legacy is not null && !legacy.StorySkips.IsDefault && legacy.StorySkips.IsEmpty,"Legacy default incompatible");
    try{_ = new NarrativeCatalog(21,catalog.Quests.Values,[..catalog.Chapters.Values,new("CH.Alias",["Q.First"],[])],catalog.Dialogues.Values);throw new Failure("Ambiguous skip chapter accepted");}
    catch(Exception e)when(e.Message=="ambiguous_story_skip_quests"){}
    var compatible=new NarrativeCatalog(23,catalog.Quests.Values,catalog.Chapters.Values,catalog.Dialogues.Values);
    Check((await new NarrativeCoordinator(repo,compatible,new(),new(),new(new()),clock).LoadAsync(alt.CharacterId,owner,default))
        .State.StorySkips.Length==1,"Unrelated catalog version broke existing skips");
    var incompatible=new NarrativeCatalog(23,catalog.Quests.Values,catalog.Chapters.Values.Select(c=>c.Id=="CH.One"?c with{StoryUnlocks=["Story.Replaced"]}:c),catalog.Dialogues.Values);
    await Reject(()=>new NarrativeCoordinator(repo,incompatible,new(),new(),new(new()),clock).LoadAsync(alt.CharacterId,owner,default),"invalid_story_skip_state");
    Console.WriteLine("PASS "+label+": malformed skip and ambiguous authoring rejected");
    if(source is not null)
    {
        var trained=await Character();
        Check(FirstSliceProgression.TryCredit(FirstSliceProgression.Create(trained.CharacterId),FirstSliceProgression.Training,
            "Normal",FirstSliceProgression.RewardVersion,out var trainedState),"Training fixture failed");
        await using(var c=await source.OpenConnectionAsync())
        await using(var q=new NpgsqlCommand("""
            INSERT INTO character_progression(character_id,state,updated_at) VALUES(@id,@state,@now)
            ON CONFLICT(character_id) DO UPDATE SET state=EXCLUDED.state,updated_at=EXCLUDED.updated_at;
            """,c))
        {
            q.Parameters.AddWithValue("id",trained.CharacterId);
            q.Parameters.AddWithValue("state",NpgsqlTypes.NpgsqlDbType.Jsonb,JsonSerializer.Serialize(trainedState,new JsonSerializerOptions(JsonSerializerDefaults.Web)));
            q.Parameters.AddWithValue("now",clock.GetUtcNow());await q.ExecuteNonQueryAsync();
        }
        var beforeTraining=await ((ICharacterProgressionRepository)game).LoadProgressionAsync(trained.CharacterId,clock.GetUtcNow(),default);
        var trainingPlan=await skip.PreviewAsync(trained.CharacterId,owner,"CH.One",default);
        await skip.ApplyAsync(trained.CharacterId,owner,Command(trainingPlan),default);
        var afterTraining=await ((ICharacterProgressionRepository)game).LoadProgressionAsync(trained.CharacterId,clock.GetUtcNow(),default);
        Check(afterTraining is {Experience:100,Milestones:1}&&JsonSerializer.Serialize(beforeTraining)==JsonSerializer.Serialize(afterTraining)
            &&!FirstSliceProgression.CanEnter(afterTraining,FirstSliceProgression.Boss,"Normal"),"Skip changed existing XP/skills or completed remaining training");
        await MembershipRace(game,source,skip,await Character(),clock);
        var failed=await Character();var fp=await skip.PreviewAsync(failed.CharacterId,owner,"CH.One",default);var fc=Command(fp);
        await using(var conn=await source.OpenConnectionAsync())
        {
            await using(var q=new NpgsqlCommand($"""
                CREATE FUNCTION reject_owned_skip() RETURNS trigger LANGUAGE plpgsql AS $$
                BEGIN IF NEW.character_id='{failed.CharacterId:D}'::uuid THEN RAISE EXCEPTION 'owned skip receipt rejection'; END IF; RETURN NEW; END $$;
                CREATE TRIGGER reject_owned_skip BEFORE INSERT ON character_narrative_commands FOR EACH ROW EXECUTE FUNCTION reject_owned_skip();
                """,conn)){await q.ExecuteNonQueryAsync();}
            try{await skip.ApplyAsync(failed.CharacterId,owner,fc,default);throw new Failure("Receipt failure committed");}
            catch(PostgresException e)when(e.SqlState=="P0001"){}
            finally{await using var q=new NpgsqlCommand("DROP TRIGGER reject_owned_skip ON character_narrative_commands; DROP FUNCTION reject_owned_skip();",conn);await q.ExecuteNonQueryAsync();}
        }
        Check((await repo.LoadNarrativeAsync(failed.CharacterId,owner,default)) is { Revision:0,StorySkips.IsEmpty:true },"Receipt failure failed to roll back state");
        var reopened=new PostgresGameRepository(source,NullLogger<PostgresGameRepository>.Instance);
        Check((await Skip(reopened).ApplyAsync(failed.CharacterId,owner,fc,default)).State.Revision==1,"Reopened retry failed");
        Check((await Narrative(reopened).LoadAsync(alt.CharacterId,owner,default)).State.Revision==later.Revision,"Reopened state lost skip/history");
        Console.WriteLine("PASS Postgres: receipt failure atomic rollback and fresh repository restoration");
    }
    var httpAlt=await Character();
    await Http(game,catalog,hero,httpAlt,foreign);
    await TypedGoals(game,label);
}
static async Task MembershipRace(IGameRepository game,NpgsqlDataSource source,NarrativeStorySkip skip,GameCharacter character,TimeProvider clock)
{
    var plan=await skip.PreviewAsync(character.CharacterId,character.SteamId,"CH.One",default);
    var command=new StorySkipCommand(Guid.NewGuid(),long.Parse(plan.ExpectedRevision),plan.CatalogVersion,plan.ChapterId,plan.PreviewHash,true);
    var config=new NpgsqlConnectionStringBuilder(Environment.GetEnvironmentVariable("PROJECT_RPG_NARRATIVE_TEST_POSTGRES")!)
    {ApplicationName="owned-skip-join-"+Guid.NewGuid().ToString("N")[..8]};
    await using var joiningSource=NpgsqlDataSource.Create(config.ConnectionString);
    var joiningRepo=new PostgresGameRepository(joiningSource,NullLogger<PostgresGameRepository>.Instance);
    await using var fenceConnection=await source.OpenConnectionAsync();await using var fence=await fenceConnection.BeginTransactionAsync();
    await using(var q=new NpgsqlCommand("SELECT character_id FROM characters WHERE character_id=@id FOR UPDATE",fenceConnection,fence))
    {q.Parameters.AddWithValue("id",character.CharacterId);Check(await q.ExecuteScalarAsync() is Guid,"Owner fence missing");}
    using var timeout=new CancellationTokenSource(TimeSpan.FromSeconds(15));
    var joining=joiningRepo.CreateDungeonSessionAsync(character.SteamId,character.CharacterId,"test_dungeon","Normal",clock.GetUtcNow(),clock.GetUtcNow().AddHours(1),timeout.Token);
    Task<NarrativeView>? applying=null;bool released=false;
    try
    {
        bool waiting=false;
        for(int i=0;i<100&&!waiting;i++)
        {
            await using var c=await source.OpenConnectionAsync(timeout.Token);
            await using var q=new NpgsqlCommand("SELECT EXISTS(SELECT 1 FROM pg_stat_activity WHERE application_name=@app AND wait_event_type='Lock')",c);
            q.Parameters.AddWithValue("app",config.ApplicationName);waiting=(bool)(await q.ExecuteScalarAsync(timeout.Token))!;
            if(!waiting)await Task.Delay(25,timeout.Token);
        }
        Check(waiting,"Concurrent membership did not wait on the owner fence");
        applying=skip.ApplyAsync(character.CharacterId,character.SteamId,command,timeout.Token);
        await fence.CommitAsync(timeout.Token);released=true;
        await joining;
        await Reject(()=>applying,"story_skip_dungeon_busy");
        Check((await ((INarrativeRepository)game).LoadNarrativeAsync(character.CharacterId,character.SteamId,default)).StorySkips.IsEmpty,
            "Concurrent admission bypassed skip session fence");
        Console.WriteLine("PASS Postgres: existing training XP/skills preserved; real membership FK/owner lock race rejects skip");
    }
    finally
    {
        timeout.Cancel();if(!released)await fence.RollbackAsync();
        try{await joining;}catch{ }
        if(applying is not null){try{await applying;}catch{ }}
    }
}
static async Task TypedGoals(IGameRepository game,string label)
{
    const string owner="76561198000000339",server="skip-goals";
    var clock=TimeProvider.System;DateTimeOffset then=clock.GetUtcNow().AddMinutes(-1);
    var definition=new QuestDefinition("Goals",1,[],["kill","inspect","clear"],
        DungeonObjectives:ImmutableDictionary<string,DungeonClearObjective>.Empty.Add("clear",new("test_dungeon","Normal")),
        InteractionObjectives:ImmutableDictionary<string,InteractionObjective>.Empty.Add("inspect",new("test_dungeon","Normal","Target")),
        KillObjectives:ImmutableDictionary<string,KillObjective>.Empty.Add("kill",new("test_dungeon","Normal","Enemy",1)));
    var catalog=new NarrativeCatalog(22,[definition],[new("Goals.Chapter",["Goals"],["Goals.Unlock"],AllowStorySkip:true)],
        [new("Goals.Offer","NPC","Offer","Goals",null,[new("accept","Accept",DialogueEffect.AcceptQuest)]),
         new("Goals.Report","NPC","Report","Goals",QuestStatus.ReadyToReport,[new("report","Report",DialogueEffect.ReportQuest)])]);
    await game.UpsertAccountAsync(new(owner,owner,false,false),clock.GetUtcNow(),default);
    var hero=await game.CreateCharacterAsync(owner,"G"+Guid.NewGuid().ToString("N")[..12],clock.GetUtcNow(),default);
    var alt=await game.CreateCharacterAsync(owner,"G"+Guid.NewGuid().ToString("N")[..12],clock.GetUtcNow(),default);
    var repo=(INarrativeRepository)game;
    // Trusted fixture for an already reported chapter; the live alt uses normal public services.
    await repo.CommitNarrativeAsync(hero.CharacterId,owner,new(Guid.NewGuid(),0,22,"fixture","seed"),s=>new ScenarioManager().Project(s with
    { Revision=1,Quests=[new("Goals",1,QuestStatus.Completed,["kill","inspect","clear"],
        ImmutableDictionary<string,DungeonObjectiveProgress>.Empty.Add("clear",new(then,Guid.NewGuid())),
        ImmutableDictionary<string,InteractionObjectiveProgress>.Empty.Add("inspect",new(then,Guid.NewGuid())),
        ImmutableDictionary<string,KillObjectiveProgress>.Empty.Add("kill",new(then,1,1,Guid.NewGuid())))] },catalog),clock.GetUtcNow(),default);
    var evidence=new Evidence();
    var narrative=new NarrativeCoordinator(repo,catalog,new(),new(),new(new()),clock,evidence);
    await narrative.ChooseAsync(alt.CharacterId,owner,new(Guid.NewGuid(),0,22,"Goals.Offer","accept"),default);
    var skip=new NarrativeStorySkip((INarrativeStorySkipRepository)game,catalog,narrative,clock);
    var plan=await skip.PreviewAsync(alt.CharacterId,owner,"Goals.Chapter",default);
    var result=await skip.ApplyAsync(alt.CharacterId,owner,new(Guid.NewGuid(),1,22,plan.ChapterId,plan.PreviewHash,true),default);
    string facts=JsonSerializer.Serialize(result.State.Quests);
    await narrative.LoadAsync(alt.CharacterId,owner,default);
    Check(evidence.Queries==0,"Skipped dungeon goal still emitted a clear candidate");
    var session=await game.CreateDungeonSessionAsync(owner,alt.CharacterId,"test_dungeon","Normal",clock.GetUtcNow(),clock.GetUtcNow().AddHours(1),default);
    await game.AssignDungeonServerAsync(session.DungeonSessionId,server,"127.0.0.1:7777",clock.GetUtcNow(),clock.GetUtcNow().AddHours(1),default);
    await game.StartDungeonSessionAsync(session.DungeonSessionId,server,clock.GetUtcNow(),clock.GetUtcNow().AddHours(1),default);
    var ticket=new AccessTokenService(clock).Issue(TimeSpan.FromMinutes(10));
    Check(await game.StoreJoinTicketAsync(ticket.TokenHash,owner,alt.CharacterId,session.DungeonSessionId,ticket.ExpiresAt,default)==JoinTicketIssueResult.Issued,"Goal ticket failed");
    var consumed=await game.ConsumeJoinTicketAsync(ticket.TokenHash,server,session.DungeonSessionId,clock.GetUtcNow(),default);
    Check(consumed is not null,"Goal admission failed");long generation=consumed!.ConnectionGeneration;
    Check(await game.ConfirmDungeonMemberAdmissionAsync(session.DungeonSessionId,server,owner,alt.CharacterId,generation,clock.GetUtcNow(),default),"Goal confirmation failed");
    var killed=await new NarrativeKillCoordinator((INarrativeInteractionRepository)game,catalog,new(),new(),clock)
        .RecordAsync(session.DungeonSessionId,alt.CharacterId,server,new(Guid.NewGuid(),"Enemy",generation,clock.GetUtcNow()),default);
    var inspected=await new NarrativeInteractionCoordinator((INarrativeInteractionRepository)game,catalog,new(),new(),clock)
        .RecordAsync(session.DungeonSessionId,alt.CharacterId,server,new(Guid.NewGuid(),"Target",generation,clock.GetUtcNow()),default);
    Check(killed.AppliedObjectives.IsEmpty&&inspected.AppliedObjectives.IsEmpty,"Post-skip server event recorded a fact");
    evidence.Value=new(session.DungeonSessionId,server,alt.CharacterId,owner,"test_dungeon","Normal",clock.GetUtcNow());
    await Reject(()=>new DungeonObjectiveCoordinator(repo,evidence,catalog,new(),new(),narrative,clock)
        .RecordAsync(alt.CharacterId,owner,new(Guid.NewGuid(),2,22,session.DungeonSessionId,"Goals","clear"),default),"quest_was_story_skipped");
    var after=await repo.LoadNarrativeAsync(alt.CharacterId,owner,default);
    Check(after.Revision==2&&JsonSerializer.Serialize(after.Quests)==facts,"Skipped typed goal changed historical progress");
    Console.WriteLine("PASS "+label+": post-skip real admitted server kill/interaction are no-ops; clear command and candidate generation blocked");
}
static async Task<string> Growth(NpgsqlDataSource source)
{
    await using var c=await source.OpenConnectionAsync();
    await using var q=new NpgsqlCommand("SELECT coalesce(jsonb_agg(to_jsonb(p) ORDER BY character_id),'[]'::jsonb)::text FROM character_progression p",c);
    return (string)(await q.ExecuteScalarAsync())!;
}
static async Task Http(IGameRepository game,NarrativeCatalog catalog,GameCharacter hero,GameCharacter alt,GameCharacter foreign)
{
    var clock=TimeProvider.System;var tokens=new AccessTokenService(clock);var player=tokens.Issue(TimeSpan.FromMinutes(10));var server=tokens.Issue(TimeSpan.FromMinutes(10));
    await game.StoreSessionAsync(player.TokenHash,alt.SteamId,player.ExpiresAt,default);
    var session=await game.CreateDungeonSessionAsync(hero.SteamId,hero.CharacterId,"test_dungeon","Normal",clock.GetUtcNow(),clock.GetUtcNow().AddHours(1),default);
    await game.AssignDungeonServerAsync(session.DungeonSessionId,"skip-http","127.0.0.1:7777",clock.GetUtcNow(),clock.GetUtcNow().AddHours(1),default);
    await game.StartDungeonSessionAsync(session.DungeonSessionId,"skip-http",clock.GetUtcNow(),clock.GetUtcNow().AddHours(1),default);
    await game.StoreGameServerCredentialAsync(server.TokenHash,"skip-http",session.DungeonSessionId,server.ExpiresAt,default);
    string admin=Guid.NewGuid().ToString("N");
    var builder=WebApplication.CreateBuilder(new WebApplicationOptions{EnvironmentName="Testing"});builder.Logging.ClearProviders();builder.WebHost.UseUrls("http://127.0.0.1:0");
    builder.Services.AddSingleton<IGameRepository>(game);builder.Services.AddSingleton(tokens);builder.Services.AddSingleton<TimeProvider>(clock);
    builder.Services.AddSingleton<IOptions<AuthOptions>>(Options.Create(new AuthOptions{AdminToken=admin}));builder.Services.Configure<SecurityTelemetryOptions>(_=>{});builder.Services.AddScoped<BearerAuthenticator>();
    var narrative=new NarrativeCoordinator((INarrativeRepository)game,catalog,new(),new(),new(new()),clock);
    builder.Services.AddSingleton(narrative);builder.Services.AddSingleton(new NarrativeStorySkip((INarrativeStorySkipRepository)game,catalog,narrative,clock));
    builder.Services.AddSingleton(new NarrativeAccountHistory((INarrativeHistoryRepository)game,catalog));
    await using var app=builder.Build();app.UseMiddleware<ApiAuthenticationMiddleware>();app.MapNarrativeApi();await app.StartAsync();
    try
    {
        using var client=new HttpClient{BaseAddress=new Uri(app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single())};
        string root=$"/api/characters/{alt.CharacterId}/narrative",preview=root+"/story-skip-preview?chapterId=CH.One",apply=root+"/story-skips";
        Check((await client.GetAsync(preview)).StatusCode==HttpStatusCode.Unauthorized,"Anonymous preview allowed");
        foreach(string token in new[]{admin,server.Token}){client.DefaultRequestHeaders.Authorization=new("Bearer",token);Check((await client.GetAsync(preview)).StatusCode==HttpStatusCode.Forbidden,"Non-player preview allowed");Check((await client.PostAsJsonAsync(apply,new{})).StatusCode==HttpStatusCode.Forbidden,"Non-player skip allowed");}
        client.DefaultRequestHeaders.Authorization=new("Bearer",player.Token);
        foreach(var id in new[]{foreign.CharacterId,Guid.NewGuid()})Check((await client.GetAsync(preview.Replace(alt.CharacterId.ToString(),id.ToString()))).StatusCode==HttpStatusCode.Forbidden,"Foreign/missing identity disclosed");
        using var read=await client.GetAsync(preview);Check(read.IsSuccessStatusCode&&read.Headers.CacheControl?.NoStore==true,"Private preview failed");
        var p=(await read.Content.ReadFromJsonAsync<StorySkipPreview>())!;
        Check(!JsonSerializer.Serialize(p).Contains(hero.CharacterId.ToString()),"Source character leaked");
        var request=new NarrativeStorySkipRequest(Guid.NewGuid(),p.ExpectedRevision,p.CatalogVersion,p.ChapterId,p.PreviewHash,true);
        Check((await client.PostAsJsonAsync(apply,request with{Confirmed=false})).StatusCode==HttpStatusCode.BadRequest,"Unconfirmed skip accepted");
        Check((await client.PostAsJsonAsync(apply,request with{ExpectedRevision="1.0"})).StatusCode==HttpStatusCode.BadRequest,"Non-integral revision accepted");
        using var result=await client.PostAsJsonAsync(apply,request);Check(result.IsSuccessStatusCode&&result.Headers.CacheControl?.NoStore==true,"HTTP skip failed");
        var body=await result.Content.ReadFromJsonAsync<JsonElement>();Check(body.GetProperty("revision").ValueKind==JsonValueKind.String&&body.GetProperty("storySkippedChapters")[0].GetString()=="CH.One"&&body.GetProperty("quests").GetArrayLength()==0,"HTTP skips/facts shape wrong");
        using var replay=await client.PostAsJsonAsync(apply,request);Check(replay.IsSuccessStatusCode,"HTTP replay failed");
        using var current=await client.GetAsync(root);Check(current.IsSuccessStatusCode,"Current narrative incompatible after skip");
        var view=await current.Content.ReadFromJsonAsync<JsonElement>();Check(view.GetProperty("revision").GetString()=="1"&&view.GetProperty("storyUnlocks")[0].GetString()=="Story.One","HTTP replay duplicated or unlock lost");
        Console.WriteLine("PASS authenticated HTTP: player-only, own character, private preview, confirmation, string revisions, apply/replay and additive snapshot");
    }
    finally{await app.StopAsync();}
}
static async Task Reject(Func<Task> action,string code){try{await action();throw new Failure("Expected rejection: "+code);}catch(NarrativeConflictException e)when(e.Message==code){}}
static void Check(bool condition,string message){if(!condition)throw new Failure(message);}
sealed class Failure(string message):Exception(message);
sealed class ContextRepository(NarrativeStorySkipContext context):INarrativeStorySkipRepository
{
    public NarrativeStorySkipContext Context=>context;
    public Task<NarrativeStorySkipContext> LoadStorySkipContextAsync(Guid id,string owner,CancellationToken ct)=>Task.FromResult(context);
    public Task<NarrativeState> CommitStorySkipAsync(Guid id,string owner,StorySkipCommand command,Func<NarrativeStorySkipContext,NarrativeState> transition,DateTimeOffset now,CancellationToken ct)=>throw new NotSupportedException();
}
sealed class Evidence:INarrativeDungeonEvidenceRepository
{
    public int Queries;
    public NarrativeDungeonClearEvidence? Value;
    public Task<NarrativeDungeonClearEvidence?> LoadNarrativeDungeonClearEvidenceAsync(Guid session,Guid character,CancellationToken ct)=>Task.FromResult(Value);
    public Task<ImmutableArray<DungeonObjectiveCandidate>> FindNarrativeDungeonObjectiveCandidatesAsync(Guid id,string owner,IReadOnlyList<NarrativeDungeonObjectiveQuery> queries,CancellationToken ct)
    {Queries=queries.Count;return Task.FromResult(ImmutableArray<DungeonObjectiveCandidate>.Empty);}
}
