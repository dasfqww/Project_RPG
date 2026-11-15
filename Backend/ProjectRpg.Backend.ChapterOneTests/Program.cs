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
Console.WriteLine("PASS Chapter One M01-M04 integration");
sealed class Clock(DateTimeOffset now):TimeProvider { public DateTimeOffset Now=now; public override DateTimeOffset GetUtcNow()=>Now; public void Advance()=>Now=Now.AddSeconds(2); }
