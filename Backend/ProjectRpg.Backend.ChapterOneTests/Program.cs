using System.Text.Json;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Narrative;

var catalog = NarrativeCatalogLoader.Parse(await File.ReadAllBytesAsync(args[0]));
InMemoryTransactionGate gate = new();
IGameRepository game = new InMemoryGameRepository(gate, new InMemoryItemRepository(gate));
Clock clock = new(DateTimeOffset.UtcNow);
QuestManager quests = new();
NarrativeCoordinator Flow() => new((INarrativeRepository)game, catalog, quests, new(), new(quests), clock,
    (INarrativeDungeonEvidenceRepository)game);
DungeonObjectiveCoordinator Goals() => new((INarrativeRepository)game, (INarrativeDungeonEvidenceRepository)game,
    catalog, quests, new(), Flow(), clock);
NarrativeInteractionCoordinator Events() => new((INarrativeInteractionRepository)game, catalog, quests, new(), clock);
NarrativeStorySkip Skip() => new((INarrativeStorySkipRepository)game, catalog, Flow(), clock);
NarrativeAccountHistory History() => new((INarrativeHistoryRepository)game, catalog);
const string owner = "76561198000000381", server = "chapter-one-test";
await game.UpsertAccountAsync(new(owner, owner, false, false), clock.Now, default);
var character = await game.CreateCharacterAsync(owner, "ChapterOne", clock.Now, default);
var alt = await game.CreateCharacterAsync(owner, "ChapterAlt", clock.Now, default);
async Task<NarrativeView> View() => await Flow().LoadAsync(character.CharacterId, owner, default);
async Task Choose(string dialogue, string choice = "accept") {
    var view = await View(); clock.Advance();
    await Flow().ChooseAsync(character.CharacterId, owner, new(Guid.NewGuid(), view.State.Revision, catalog.Version, dialogue, choice), default);
}
async Task<Guid> Clear(string dungeon) {
    var session = await game.CreateDungeonSessionAsync(owner, character.CharacterId, dungeon, "Normal", clock.Now, clock.Now.AddHours(1), default);
    await game.AssignDungeonServerAsync(session.DungeonSessionId, server, "127.0.0.1:7777", clock.Now, clock.Now.AddHours(1), default);
    await game.StartDungeonSessionAsync(session.DungeonSessionId, server, clock.Now, clock.Now.AddHours(1), default);
    await game.EnqueueDungeonRewardSettlementAsync(session.DungeonSessionId, server, "chapter-test", new string('a',64), [], [], clock.Now, default);
    await game.ClaimNextDungeonRewardSettlementAsync(server, clock.Now, clock.Now.AddMinutes(1), default);
    await game.CompleteDungeonRewardSettlementAsync(session.DungeonSessionId, server, clock.Now, default);
    clock.Advance(); return session.DungeonSessionId;
}
void Check(bool ok, string message) { if (!ok) throw new Exception(message); Console.WriteLine("PASS " + message); }
Check(catalog.Quests["CH01.M03"].Title == "안쪽에서 남긴 흔적"
    && catalog.Quests["CH01.M04"].Title == "접근로 확보"
    && catalog.Dialogues["CH01.M04.Offer"].Text == "안쪽에서 응답이 돌아왔다. 봉쇄를 건드리지 않고 점검 통로로 접근하자.",
    "Authored Korean quest and field objective text preserved");
async Task Reject(Func<Task> action, string code) {
    try { await action(); } catch(NarrativeConflictException e) when(e.Message == code) { Console.WriteLine("PASS reject " + code); return; }
    throw new Exception("Expected " + code);
}
var training = await Clear("Progression.Training");
await Reject(() => Choose("CH01.M03.Offer"), "dialogue_not_available");
await Choose("CH01.M01.Offer");
await Reject(() => Choose("CH01.D02", "confirm"), "dialogue_choice_not_available");
await Choose("CH01.D01", "confirm"); await Choose("CH01.D02", "confirm");
Check((await View()).State.Quests.Single(q=>q.QuestId=="CH01.M01").Status == QuestStatus.Completed, "M01 automatic completion after both approved explanations");
await Choose("CH01.M02.Offer");
var view = await View();
Check(view.DungeonObjectiveCandidates.Any(c=>c.DungeonSessionId==training), "Past Training receipt discovered only for explicitly authored goal");
await Goals().RecordAsync(character.CharacterId,owner,new(Guid.NewGuid(),view.State.Revision,catalog.Version,training,"CH01.M02","training"),default);
await Choose("CH01.M03.Offer");
var session = await game.CreateDungeonSessionAsync(owner, character.CharacterId, "Progression.Intro1", "Normal", clock.Now, clock.Now.AddHours(1), default);
await game.AssignDungeonServerAsync(session.DungeonSessionId, server,"127.0.0.1:7777",clock.Now,clock.Now.AddHours(1),default);
await game.StartDungeonSessionAsync(session.DungeonSessionId, server,clock.Now,clock.Now.AddHours(1),default);
async Task<long> Admit() {
    var ticket=new AccessTokenService(clock).Issue(TimeSpan.FromMinutes(5));
    await game.StoreJoinTicketAsync(ticket.TokenHash,owner,character.CharacterId,session.DungeonSessionId,ticket.ExpiresAt,default);
    var admitted=await game.ConsumeJoinTicketAsync(ticket.TokenHash,server,session.DungeonSessionId,clock.Now,default);
    Check(admitted is not null,"Admission consumed");
    Check(await game.ConfirmDungeonMemberAdmissionAsync(session.DungeonSessionId,server,owner,character.CharacterId,admitted!.ConnectionGeneration,clock.Now,default),"Admission confirmed");
    return admitted.ConnectionGeneration;
}
long generation=await Admit();
NarrativeInteractionCommand Event(int clue) => new(Guid.NewGuid(),"CH01.CL0"+clue,generation,clock.Now);
Task<NarrativeInteractionResult> Record(NarrativeInteractionCommand cmd)=>Events().RecordAsync(session.DungeonSessionId,character.CharacterId,server,cmd,default);
string growth=JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(character.CharacterId,clock.Now,default));
var early=Event(3); Check((await Record(early)).AppliedObjectives.IsEmpty,"Early signal observation grants nothing");
clock.Advance(); var cart=Event(1);
var copies=await Task.WhenAll(Record(cart),Record(cart));
Check(copies.All(x=>x.AppliedObjectives.Length==1)&&copies[0].Revision==copies[1].Revision,"Concurrent duplicate cart event has one durable revision");
Check((await Record(Event(1))).AppliedObjectives.IsEmpty,"Reinvestigating completed cart grants nothing");
await game.RecordDungeonMemberDisconnectionAsync(session.DungeonSessionId,server,owner,character.CharacterId,generation,clock.Now,default);
await Reject(()=>Record(Event(2)),"interaction_not_authorized");
Check((await View()).State.Quests.Single(q=>q.QuestId=="CH01.M03").CompletedObjectives.SequenceEqual(["cart"]),"Reopened coordinator retains cart across disconnect");
long oldGeneration=generation; generation=await Admit(); clock.Advance();
await Reject(()=>Record(Event(2) with {ConnectionGeneration=oldGeneration}),"interaction_not_authorized");
Check((await Record(Event(2))).AppliedObjectives.Length==1,"Reconnected current generation records brace");
Check((await Record(early)).AppliedObjectives.IsEmpty,"Early no-op receipt never retroactively credits signal");
clock.Advance(); var signal=Event(3); var result=await Record(signal); view=await View();
Check(view.State.Quests.Single(q=>q.QuestId=="CH01.M03").Status==QuestStatus.Completed,"Third fresh clue auto-completes M03 in the event transaction");
Check(view.AvailableDialogues.Any(d=>d.Id=="CH01.M04.Offer")&&!view.State.Quests.Any(q=>q.QuestId=="CH01.M04"),"M04 offered without automatic acceptance");
Check((await Record(signal)).Revision==result.Revision&&(await Record(Event(3))).AppliedObjectives.IsEmpty,"Completion response replay and reinvestigation never duplicate credit");
Check(view.State.CompletedChapters.IsEmpty,"M03 does not fabricate full chapter completion or skip eligibility");
Check((await Flow().LoadAsync(alt.CharacterId,owner,default)).State.Quests.IsEmpty,"Alt progress isolated");
Check(growth==JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(character.CharacterId,clock.Now,default)),"Narrative investigations do not grant growth or duplicate settlement");
await Choose("CH01.M04.Offer");
Check((await View()).State.Quests.Single(q=>q.QuestId=="CH01.M04").Status==QuestStatus.Active,"M04 still requires actual Intro1 evidence");
view=await View();
await Reject(()=>Goals().RecordAsync(character.CharacterId,owner,new(Guid.NewGuid(),view.State.Revision,catalog.Version,
    session.DungeonSessionId,"CH01.M04","approach"),default),"dungeon_clear_evidence_not_authorized");
await game.EnqueueDungeonRewardSettlementAsync(session.DungeonSessionId,server,"chapter-m04-test",new string('b',64),[],[],clock.Now,default);
await game.ClaimNextDungeonRewardSettlementAsync(server,clock.Now,clock.Now.AddMinutes(1),default);
await game.CompleteDungeonRewardSettlementAsync(session.DungeonSessionId,server,clock.Now,default);
clock.Advance();view=await View();
Check(view.DungeonObjectiveCandidates.Any(c=>c.QuestId=="CH01.M04"&&c.DungeonSessionId==session.DungeonSessionId),
    "Only settled Intro1 is a candidate for M04");
var m04Command=new DungeonObjectiveCommand(Guid.NewGuid(),view.State.Revision,catalog.Version,session.DungeonSessionId,"CH01.M04","approach");
var m04=await Goals().RecordAsync(character.CharacterId,owner,m04Command,default);
Check(m04.State.Quests.Single(q=>q.QuestId=="CH01.M04").Status==QuestStatus.Completed,"Settled Intro1 auto-completes accepted M04");
Check((await Goals().RecordAsync(character.CharacterId,owner,m04Command,default)).State.Revision==m04.State.Revision,
    "Lost M04 response replays the original revision");
Check((await View()).DungeonObjectiveCandidates.All(c=>c.QuestId!="CH01.M04"),"Completed M04 has no repeat candidate");
Check(growth==JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(character.CharacterId,clock.Now,default)),
    "Recording M04 never repeats the settlement growth grant");
Check((await View()).AvailableDialogues.Any(d=>d.Id=="CH01.M05.Offer"),"M05 offered after the actual M04 prerequisite");
session=await game.CreateDungeonSessionAsync(owner,character.CharacterId,"Progression.Intro2","Normal",clock.Now,clock.Now.AddHours(1),default);
await game.AssignDungeonServerAsync(session.DungeonSessionId,server,"127.0.0.1:7777",clock.Now,clock.Now.AddHours(1),default);
await game.StartDungeonSessionAsync(session.DungeonSessionId,server,clock.Now,clock.Now.AddHours(1),default);
generation=await Admit();
NarrativeInteractionCommand Daon()=>new(Guid.NewGuid(),"CH01.Daon",generation,clock.Now);
var earlyDaon=Daon();Check((await Record(earlyDaon)).AppliedObjectives.IsEmpty,"Unaccepted Daon observation grants nothing");
await game.EnqueueDungeonRewardSettlementAsync(session.DungeonSessionId,server,"chapter-m05-test",new string('c',64),[],[],clock.Now,default);
await game.ClaimNextDungeonRewardSettlementAsync(server,clock.Now,clock.Now.AddMinutes(1),default);
await game.CompleteDungeonRewardSettlementAsync(session.DungeonSessionId,server,clock.Now,default);
var previousIntro2=session.DungeonSessionId;clock.Advance();
await Choose("CH01.M05.Offer");view=await View();
Check(view.AvailableDialogues.All(d=>d.Id!="CH01.D05"),"Testimony hidden until a personal discovery is approved");
await Reject(()=>Choose("CH01.D05","confirm"),"dialogue_choice_not_available");
await Goals().RecordAsync(character.CharacterId,owner,new(Guid.NewGuid(),view.State.Revision,catalog.Version,previousIntro2,"CH01.M05","opportunity"),default);
Check((await View()).State.Quests.Single(q=>q.QuestId=="CH01.M05").CompletedObjectives.SequenceEqual(["opportunity"]),"Prior Intro2 receipt credits combat only; does not invent Daon or testimony");
session=await game.CreateDungeonSessionAsync(owner,character.CharacterId,"Progression.Intro2","Normal",clock.Now,clock.Now.AddHours(1),default);
await game.AssignDungeonServerAsync(session.DungeonSessionId,server,"127.0.0.1:7777",clock.Now,clock.Now.AddHours(1),default);
await game.StartDungeonSessionAsync(session.DungeonSessionId,server,clock.Now,clock.Now.AddHours(1),default);generation=await Admit();clock.Advance();
var discovery=Daon();var found=await Task.WhenAll(Record(discovery),Record(discovery));
Check(found.All(r=>r.AppliedObjectives.Length==1)&&found[0].Revision==found[1].Revision,"Concurrent discovery duplicates share one durable revision");
Check((await Record(Daon())).AppliedObjectives.IsEmpty,"Reinvestigation does not credit discovery twice");
await game.RecordDungeonMemberDisconnectionAsync(session.DungeonSessionId,server,owner,character.CharacterId,generation,clock.Now,default);
Check((await View()).State.Quests.Single(q=>q.QuestId=="CH01.M05").CompletedObjectives.Contains("discovery"),"Daon discovery persists across a reopened coordinator and disconnection");
oldGeneration=generation;generation=await Admit();clock.Advance();
await Reject(()=>Record(Daon() with {ConnectionGeneration=oldGeneration}),"interaction_not_authorized");
view=await View();Check(view.AvailableDialogues.Any(d=>d.Id=="CH01.D05"&&d.Text.Contains("버팀목은 제가 걸었습니다")),"Only approved discovery exposes the authored testimony");
var testimony=new NarrativeCommand(Guid.NewGuid(),view.State.Revision,catalog.Version,"CH01.D05","confirm");
var completed=await Flow().ChooseAsync(character.CharacterId,owner,testimony,default);
Check((await View()).State.Quests.Single(q=>q.QuestId=="CH01.M05").Status==QuestStatus.Completed,"Discovery plus testimony plus authentic prior combat completes M05 without combat replay");
await Flow().ChooseAsync(character.CharacterId,owner,testimony,default);
Check((await View()).State.CompletedChapters.IsEmpty&&(await View()).AvailableDialogues.Any(d=>d.Id=="CH01.D06.Preview"),"M05 exposes the rescue warning without fabricating a chapter or M06 completion");
Check(growth==JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(character.CharacterId,clock.Now,default)),"Daon event, testimony and replay grant no extra growth");
Check((await View()).AvailableDialogues.Any(d=>d.Id=="CH01.M06.Offer")&&!(await View()).State.Quests.Any(q=>q.QuestId=="CH01.M06"),"M06 is offered after M05, never auto accepted");
await game.EnqueueDungeonRewardSettlementAsync(session.DungeonSessionId,server,"chapter-m05-repeat",new string('d',64),[],[],clock.Now,default);
await game.ClaimNextDungeonRewardSettlementAsync(server,clock.Now,clock.Now.AddMinutes(1),default);
await game.CompleteDungeonRewardSettlementAsync(session.DungeonSessionId,server,clock.Now,default);
var priorIntro3=await Clear("Progression.Intro3");
await Reject(()=>Choose("CH01.D06","confirm"),"dialogue_not_available");
await Choose("CH01.M06.Offer");view=await View();
await Goals().RecordAsync(character.CharacterId,owner,new(Guid.NewGuid(),view.State.Revision,catalog.Version,priorIntro3,"CH01.M06","rescue"),default);
Check((await View()).State.Quests.Single(q=>q.QuestId=="CH01.M06").CompletedObjectives.SequenceEqual(["rescue"]),"Prior Intro3 receipt credits only its combat bundle, never a new briefing or patient scene");
session=await game.CreateDungeonSessionAsync(owner,character.CharacterId,"Progression.Intro3","Normal",clock.Now,clock.Now.AddHours(1),default);
await game.AssignDungeonServerAsync(session.DungeonSessionId,server,"127.0.0.1:7777",clock.Now,clock.Now.AddHours(1),default);
await game.StartDungeonSessionAsync(session.DungeonSessionId,server,clock.Now,clock.Now.AddHours(1),default);generation=await Admit();clock.Advance();
NarrativeInteractionCommand Patient()=>new(Guid.NewGuid(),"CH01.M06.Patient",generation,clock.Now);
var earlyPatient=Patient();Check((await Record(earlyPatient)).AppliedObjectives.IsEmpty,"Early patient observation cannot bypass the D06 briefing");
var d06=await View();var brief=new NarrativeCommand(Guid.NewGuid(),d06.State.Revision,catalog.Version,"CH01.D06","confirm");
await Flow().ChooseAsync(character.CharacterId,owner,brief,default);await Flow().ChooseAsync(character.CharacterId,owner,brief,default);
Check((await Record(earlyPatient)).AppliedObjectives.IsEmpty,"Replaying an early observation does not create a later scene");
await game.RecordDungeonMemberDisconnectionAsync(session.DungeonSessionId,server,owner,character.CharacterId,generation,clock.Now,default);
Check((await View()).State.Quests.Single(q=>q.QuestId=="CH01.M06").CompletedObjectives.Contains("briefing"),"M06 briefing persists independently of a combat attempt");
oldGeneration=generation;generation=await Admit();clock.Advance();
await Reject(()=>Record(Patient() with {ConnectionGeneration=oldGeneration}),"interaction_not_authorized");
var patient=Patient();var scene=await Task.WhenAll(Record(patient),Record(patient));
Check(scene.All(r=>r.AppliedObjectives.Length==1)&&scene[0].Revision==scene[1].Revision,"Concurrent current-generation patient scene is recognized exactly once");
Check((await View()).State.Quests.Single(q=>q.QuestId=="CH01.M06").Status==QuestStatus.Completed,"M06 completes only after briefing, personal scene and the complete combat receipt");
Check((await Record(Patient())).AppliedObjectives.IsEmpty,"Reinvestigation never duplicates M06 credit");
Check((await View()).AvailableDialogues.Any(d=>d.Id=="CH01.D07.Preview")&&(await View()).State.CompletedChapters.IsEmpty,"Internal shelter offers the return-route preview without final export or chapter completion");
Check(growth==JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(character.CharacterId,clock.Now,default)),"M06 acceptance, briefing, prior receipt and scene add no growth rewards");

Check((await View()).AvailableDialogues.Any(d=>d.Id=="CH01.M07.Offer"),"M07 offered after M06, not accepted automatically");
// Close the old scene membership before the next physical instance.
await game.EnqueueDungeonRewardSettlementAsync(session.DungeonSessionId,server,"close-m06",new string('e',64),[],[],clock.Now,default);
await game.ClaimNextDungeonRewardSettlementAsync(server,clock.Now,clock.Now.AddMinutes(1),default);
await game.CompleteDungeonRewardSettlementAsync(session.DungeonSessionId,server,clock.Now,default);
var bossReceipt=await Clear("Progression.FirstBoss");
await Choose("CH01.M07.Offer");
session=await game.CreateDungeonSessionAsync(owner,character.CharacterId,"Story.ChapterOneReturn","Normal",clock.Now,clock.Now.AddHours(1),default);
await game.AssignDungeonServerAsync(session.DungeonSessionId,server,"127.0.0.1:7777",clock.Now,clock.Now.AddHours(1),default);
await game.StartDungeonSessionAsync(session.DungeonSessionId,server,clock.Now,clock.Now.AddHours(1),default);generation=await Admit();clock.Advance();
NarrativeInteractionCommand Operation(string step)=>new(Guid.NewGuid(),"CH01.M07."+step,generation,clock.Now);
Check((await Record(Operation("export"))).AppliedObjectives.IsEmpty,"Boss death alone cannot export patients");
view=await View();
await Goals().RecordAsync(character.CharacterId,owner,new(Guid.NewGuid(),view.State.Revision,catalog.Version,bossReceipt,"CH01.M07","boss"),default);
Check((await View()).State.Quests.Single(q=>q.QuestId=="CH01.M07").CompletedObjectives.SequenceEqual(["boss"]),"Prior Normal receipt recognizes combat only; no safety/opening/export fabricated");
Check((await Record(Operation("brace"))).AppliedObjectives.IsEmpty,"Cannot open B before C is safe");
clock.Advance();var safety=Operation("safe");var safetyCopies=await Task.WhenAll(Record(safety),Record(safety));
Check(safetyCopies.All(x=>x.AppliedObjectives.Length==1)&&safetyCopies[0].Revision==safetyCopies[1].Revision,"Safety is committed once under duplicate delivery");
await game.RecordDungeonMemberDisconnectionAsync(session.DungeonSessionId,server,owner,character.CharacterId,generation,clock.Now,default);
await Reject(()=>Record(Operation("brace")),"interaction_not_authorized");
oldGeneration=generation;generation=await Admit();clock.Advance();
await Reject(()=>Record(Operation("brace") with {ConnectionGeneration=oldGeneration}),"interaction_not_authorized");
Check((await View()).State.Quests.Single(q=>q.QuestId=="CH01.M07").DungeonObjectives!["boss"].DungeonSessionId==bossReceipt,"Disconnect preserves the original combat anchor");
Check((await Record(Operation("brace"))).AppliedObjectives.Length==1,"Recovered scene opens B without another boss clear");
// A new admitted return instance must resume only the unfinished export.
await game.FinishDungeonSessionAsync(session.DungeonSessionId,server,DungeonSessionState.Failed,clock.Now,default);
session=await game.CreateDungeonSessionAsync(owner,character.CharacterId,"Story.ChapterOneReturn","Normal",clock.Now,clock.Now.AddHours(1),default);
await game.AssignDungeonServerAsync(session.DungeonSessionId,server,"127.0.0.1:7777",clock.Now,clock.Now.AddHours(1),default);
await game.StartDungeonSessionAsync(session.DungeonSessionId,server,clock.Now,clock.Now.AddHours(1),default);generation=await Admit();clock.Advance();
Check((await Record(Operation("safe"))).AppliedObjectives.IsEmpty,"New instance does not duplicate saved safety");
await Reject(() => Choose("CH01.M08.Offer"), "dialogue_not_available");
var export=Operation("export");await Record(export);await Record(export);
var m07=(await View()).State.Quests.Single(q=>q.QuestId=="CH01.M07");
Check(m07.Status==QuestStatus.Completed&&m07.DungeonObjectives!["boss"].DungeonSessionId==bossReceipt,"Only ordered approved final export completes M07 with original combat anchor");
Check((await View()).State.CompletedChapters.IsEmpty,"M07 is not M08/chapter completion or account skip entitlement");
Check(growth==JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(character.CharacterId,clock.Now,default)),"Extraction/recovery never duplicates growth rewards");
Check((await View()).AvailableDialogues.Any(d => d.Id == "CH01.M08.Offer"), "Final extraction exposes Mira's offer");
Check((await History().LoadAsync(alt.CharacterId, owner, default)).Chapters.IsEmpty, "Extraction alone makes no account history");
await Reject(() => Skip().PreviewAsync(alt.CharacterId, owner, "CH01", default), "story_skip_not_eligible");

// The station can take the report while the return scene is still open.
await Choose("CH01.M08.Offer");
await Reject(() => Choose("CH01.D08.Report", "report"), "dialogue_not_available");
await Choose("CH01.D08", "debrief");
view = await View();
Check(view.State.Quests.Single(q => q.QuestId == "CH01.M08").Status == QuestStatus.ReadyToReport
    && view.State.CompletedChapters.IsEmpty && view.AvailableDialogues.All(d => d.Id != "CH01.D09"),
    "Debrief survives a fresh coordinator but completion and epilogue wait for the final report");
await Reject(() => Choose("CH01.D09", "survivors"), "dialogue_not_available");
var report = new NarrativeCommand(Guid.NewGuid(), view.State.Revision, catalog.Version, "CH01.D08.Report", "report");
var reports = await Task.WhenAll(Flow().ChooseAsync(character.CharacterId, owner, report, default),
    Flow().ChooseAsync(character.CharacterId, owner, report, default));
Check(reports.All(r => r.State.Revision == view.State.Revision + 1 && r.State.CompletedChapters.SequenceEqual(["CH01"])),
    "Concurrent report delivery commits the chapter exactly once");
await Reject(() => Choose("CH01.D08.Report", "report"), "dialogue_not_available");

var ending = await View();
var aftermath = ending.AvailableDialogues.Single(d => d.Id == "CH01.D09");
Check(aftermath.EntryPoint && aftermath.Choices.Single(c => c.Id == "survivors").NextDialogueId == "CH01.A01"
    && aftermath.Choices.Single(c => c.Id == "cause").NextDialogueId == "CH01.A02"
    && ending.AvailableDialogues.Count(d => (d.Id is "CH01.A01" or "CH01.A02") && !d.EntryPoint) == 2,
    "Direct completion exposes both authored follow-up replies through the epilogue");
await Reject(() => Choose("CH01.D09", "survivors"), "informational_dialogue_not_mutable");
Check((await View()).State.Revision == ending.State.Revision, "Reopening and reading the epilogue does not write progress");
Check(growth == JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(character.CharacterId, clock.Now, default)),
    "M08 report and replay grant no additional growth");

var directHistory = (await History().LoadAsync(alt.CharacterId, owner, default)).Chapters.Single();
Check(directHistory is { ChapterId: "CH01", CompletedOnOtherCharacter: true, CompletedOnSelectedCharacter: false },
    "Mira's report creates direct account history without completing an alt");
var altGrowth = JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(alt.CharacterId, clock.Now, default));
var plan = await Skip().PreviewAsync(alt.CharacterId, owner, "CH01", default);
Check(plan.Quests.Length == 8 && plan is { Cost: 0, GrantsQuestRewards: false, RequiresOriginalTraining: true },
    "Alt preview offers the eight main quests with the original training requirement");
var skipped = await Skip().ApplyAsync(alt.CharacterId, owner, new(Guid.NewGuid(), long.Parse(plan.ExpectedRevision),
    catalog.Version, plan.ChapterId, plan.PreviewHash, true), default);
Check(skipped.State.CompletedChapters.SequenceEqual(["CH01"]) && skipped.State.Quests.IsEmpty
    && skipped.AvailableDialogues.All(d => d.Id is not ("CH01.M08.Offer" or "CH01.D08.Report" or "CH01.D09")),
    "A story skip never fabricates a personal report or its epilogue");
Check(altGrowth == JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(alt.CharacterId, clock.Now, default))
    && (await History().LoadAsync(alt.CharacterId, owner, default)).Chapters.Single() == directHistory,
    "Story skip preserves alt growth and does not add direct account history");
Console.WriteLine("PASS Chapter One M01-M08 integration");
var content = new ProjectRpg.Backend.Application.DungeonContentCatalog(new ProjectRpg.Backend.Configuration.DungeonContentOptions {
    Entries=[new(){DungeonId="Story.ChapterOneReturn",MapPackage="/Game/Maps/Progression/L_ChapterOneReturn",Difficulties=["Normal"]},
        new(){DungeonId="Progression.FirstBoss",MapPackage="/Game/Maps/Progression/L_ChapterOneBoss",Difficulties=["Normal","Hard"]}] });
var admission=new DungeonStoryAdmission(content,catalog);
Check(await admission.GetErrorAsync(game,character.CharacterId,owner,"Story.ChapterOneReturn",default) is null,"M06-complete character may resume the story scene");
Check(await admission.GetErrorAsync(game,alt.CharacterId,owner,"Story.ChapterOneReturn",default)=="story_prerequisite_not_met","An alt who skipped M07 cannot enter the direct extraction scene");
Check(await admission.GetErrorAsync(game,alt.CharacterId,owner,"Progression.FirstBoss",default) is null,"Existing repeat/skip combat adds no narrative gate; original growth admission retains authority");

sealed class Clock(DateTimeOffset now):TimeProvider { public DateTimeOffset Now=now; public override DateTimeOffset GetUtcNow()=>Now; public void Advance()=>Now=Now.AddSeconds(2); }
