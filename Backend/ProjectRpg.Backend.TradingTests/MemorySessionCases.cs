using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using ProjectRpg.Backend.Operations.Restrictions;
using ProjectRpg.Backend.Operations.ServiceControl;

internal static class MemorySessionCases
{
    private static int checks;
    private static readonly TradeItemPolicy Policy = new([new("Material", "Ore", 1, true, false, 100)]);
    public static async Task<int> Run()
    {
        try
        {
            var common = await Fixture.Create();
            await SharedContract(common.Store, common.A, common.B, () => common.Balance(common.A), () => common.Balance(common.B));
            await SwapsAndRollback(); await ReadyCancellation(); await GatesAndExpiry(); await Races(); await CurrentDefinition();
            Console.WriteLine($"PASS Memory trade sessions: 6 groups, {checks} assertions; process-local atomicity only."); return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }
    // Run exactly the same commands/assertions against BOTH real repository implementations.
    internal static async Task SharedContract(ITradeSessionRepository store, TradeParticipant a, TradeParticipant b, Func<Task<long>> balanceA, Func<Task<long>> balanceB)
    {
        var invitation = new TradeSessionCommand(Guid.NewGuid(), Guid.NewGuid(), a, TradeCommandKind.Invite, Invitee: b, CurrencyCode: "Gold");
        var first = await store.ExecuteAsync(invitation, default);
        Check(JsonSerializer.Serialize(first) == JsonSerializer.Serialize(await store.ExecuteAsync(invitation, default)), "shared invite replay");
        var s = (await store.ExecuteAsync(Cmd(first.Session,b,TradeCommandKind.Accept),default)).Session;
        s = (await store.ExecuteAsync(Cmd(s,a,TradeCommandKind.ReplaceOffer) with {Offer=new([],20)},default)).Session;
        var old = Cmd(s,b,TradeCommandKind.Confirm);
        s=(await store.ExecuteAsync(Cmd(s,a,TradeCommandKind.Ready),default)).Session;
        s=(await store.ExecuteAsync(Cmd(s,b,TradeCommandKind.Ready),default)).Session;
        Check(await balanceA()==100 && await balanceB()==100,"shared Ready does not spend");
        await Reject(()=>store.ExecuteAsync(old,default),"trade_revision_conflict");
        s=(await store.ExecuteAsync(Cmd(s,a,TradeCommandKind.Confirm),default)).Session;
        var final=Cmd(s,b,TradeCommandKind.Confirm);var result=await store.ExecuteAsync(final,default);
        Check(result.Rejection is null && result.Session.Completed && result.Session.State.Status==TradeNegotiationStatus.Agreed,"shared final settlement");
        Check(await balanceA()==80 && await balanceB()==120,"shared bilateral balance");
        Check(JsonSerializer.Serialize(result)==JsonSerializer.Serialize(await store.ExecuteAsync(final,default)),"shared exact final replay");
        await Reject(()=>store.ExecuteAsync(final with {Kind=TradeCommandKind.Cancel},default),"trade_idempotency_conflict");
        await Reject(()=>store.ExecuteAsync(Cmd(result.Session,a,TradeCommandKind.Cancel),default),"trade_closed");
        var foreign=a with {AccountId=b.AccountId};Check(await store.FindAsync(final.TradeId,foreign,default)is null,"shared forged account hidden");
        await Reject(()=>store.ExecuteAsync(final with {Actor=foreign},default),"trade_participant_required");
        var copy=(await store.FindAsync(final.TradeId,a,default))!;
        if(copy.Exchange!.Currency is IList<TradeCurrencyMovement> list && !list.IsReadOnly) list[0]=list[0] with {NewBalance=999};
        Check((await store.FindAsync(final.TradeId,a,default))!.Exchange!.Currency[0].NewBalance==80,"shared returned result cannot alter stored receipt");
        Console.WriteLine("PASS shared Memory/PostgreSQL session contract: replay, CAS, consent, balances, ownership, isolation");
    }
    private static async Task SwapsAndRollback()
    {
        var f=await Fixture.Create();var aa=await f.Seed(f.A,80);var bb=await f.Seed(f.B,80,seedOffset:1000);
        var s=await f.Open();s=await f.Offer(s,f.A,new([new(aa[7].ItemId,1,1)],20));s=await f.Offer(s,f.B,new([new(bb[43].ItemId,1,1)],0));
        s=await f.ReadyBoth(s);s=(await f.Do(s,f.A,TradeCommandKind.Confirm)).Session;
        var final=Cmd(s,f.B,TradeCommandKind.Confirm);
        var holdA=s.InitiatorReservation!.Value;var holdB=s.InviteeReservation!.Value;
        f.Clock.When=()=>f.Items.FindAsync(aa[7].ItemId,default).Result!.Owner.OwnerId==f.B.CharacterId.ToString("D")
            && f.Balance(f.A).Result==80 && f.Holds.FindAsync(holdA,f.A,default).Result!.Status==TradeReservationStatus.Released
            && f.Holds.FindAsync(holdB,f.B,default).Result!.Status==TradeReservationStatus.Released;
        f.Clock.Trigger=()=>throw new InjectedFailure();
        try {await f.Store.ExecuteAsync(final,default);throw new Exception("Missing injected failure");}catch(InjectedFailure){checks++;}
        Check((await f.Items.FindAsync(aa[7].ItemId,default))!.Owner==aa[7].Owner && await f.Balance(f.A)==100 && await f.Balance(f.B)==100,"late failure rolls back items and both balances");
        Check((await f.Store.FindAsync(s.State.TradeId,f.A,default))!.State.Revision==s.State.Revision
            && (await f.Holds.FindAsync(holdA,f.A,default))!.Status==TradeReservationStatus.Active
            && (await f.Holds.FindAsync(holdB,f.B,default))!.Status==TradeReservationStatus.Active,"late failure rolls back consent and BOTH release tombstones");
        var completed=await f.Store.ExecuteAsync(final,default);
        Check(completed.Session.Completed && (await f.Items.FindAsync(aa[7].ItemId,default))!.Location.SlotIndex==203,"same request succeeds and full material bags swap");
        var reopened=new InMemoryTradeSessionRepository(f.Gate,f.Game,f.Items,f.Economy,Policy);
        Check(JsonSerializer.Serialize(completed)==JsonSerializer.Serialize(await reopened.ExecuteAsync(final,default)),"same-gate repository recreation replays receipt");
        try{_ = new InMemoryTradeSessionRepository(new(),f.Game,f.Items,f.Economy,Policy);throw new Exception("Mixed gate accepted");}catch(InvalidOperationException){checks++;}
        var split=await Fixture.Create();var item=(await split.Seed(split.A,1,60))[0];var target=(await split.Seed(split.B,1,90))[0];
        var t=await split.Open();t=await split.Offer(t,split.A,new([new(item.ItemId,1,30)],5));t=await split.ReadyBoth(t);t=(await split.Do(t,split.A,TradeCommandKind.Confirm)).Session;
        var outp=await split.Do(t,split.B,TradeCommandKind.Confirm);
        Check(outp.Session.Completed && (await split.Items.FindAsync(item.ItemId,default))!.State.Quantity==30
            && (await split.Items.FindAsync(target.ItemId,default))!.State.Quantity==100
            && (await split.Items.FindAsync(outp.Session.Exchange!.Splits[0].SplitItemId,default))!.State.Quantity==20,"actual Memory split/merge/provenance");
    }
    private static async Task ReadyCancellation()
    {
        var f=await Fixture.Create();var s=await f.Open();s=await f.Offer(s,f.A,new([],30));var ready=Cmd(s,f.A,TradeCommandKind.Ready);
        Guid hold=new(SHA256.HashData(Encoding.UTF8.GetBytes("trade_ready|"+ready.RequestId.ToString("D"))).AsSpan(0,16));
        using var cancel=new CancellationTokenSource();
        f.Clock.When=()=>f.Holds.FindAsync(hold,f.A,default).Result is not null;
        f.Clock.Trigger=cancel.Cancel;
        try{await f.Store.ExecuteAsync(ready,cancel.Token);throw new Exception("Cancellation not observed");}catch(OperationCanceledException){checks++;}
        Check(await f.Holds.FindAsync(hold,f.A,default)is null && !(await f.Store.FindAsync(s.State.TradeId,f.A,default))!.State.InitiatorReady,"cancelled Ready restores reservation dictionary and consent");
        s=(await f.Store.ExecuteAsync(ready,default)).Session;s=(await f.Do(s,f.B,TradeCommandKind.Ready)).Session;
        var stale=Cmd(s,f.A,TradeCommandKind.Confirm);var oldA=s.InitiatorReservation!.Value;
        s=await f.Offer(s,f.A,new([],25));Check(!s.State.InviteeReady && (await f.Holds.FindAsync(oldA,f.A,default))!.Status==TradeReservationStatus.Released,"edit withdraws both holds");
        await Reject(()=>f.Store.ExecuteAsync(stale,default),"trade_revision_conflict");
        s=await f.ReadyBoth(s);s=(await f.Do(s,f.B,TradeCommandKind.Withdraw)).Session;
        Check(s.InitiatorReservation is null && s.InviteeReservation is null && s.State.InitiatorOffer.CurrencyAmount==25,"withdraw retains offer");
        await Reject(()=>f.Open(),"character_already_trading");
        s=(await f.Do(s,f.A,TradeCommandKind.Cancel)).Session;Check(s.State.Status==TradeNegotiationStatus.Cancelled,"cancel closes");
    }
    private static async Task GatesAndExpiry()
    {
        var f=await Fixture.Create();var s=await f.Open();s=await f.Offer(s,f.A,new([],20));s=await f.ReadyBoth(s);
        var id=s.InitiatorReservation!.Value;await f.Mode("Drain");var denied=await f.Do(s,f.B,TradeCommandKind.Confirm);
        Check(denied.Rejection=="service_draining" && (await f.Holds.FindAsync(id,f.A,default))!.Status==TradeReservationStatus.Released,"real service store Drain serializes with trade");
        s=(await f.Do(denied.Session,f.B,TradeCommandKind.Cancel)).Session;Check(s.State.Status==TradeNegotiationStatus.Cancelled,"cancel during Drain");
        await f.Mode("Open");s=await f.Open();s=await f.Offer(s,f.A,new([],20));s=await f.ReadyBoth(s);await f.Restrict();
        denied=await f.Do(s,f.B,TradeCommandKind.Confirm);Check(denied.Rejection=="account_restricted" && await f.Balance(f.A)==100,"real restriction store prevents counterparty confirmation");
        var expiry=await Fixture.Create();s=await expiry.Open();s=await expiry.Offer(s,expiry.A,new([],20));s=await expiry.ReadyBoth(s);
        expiry.Clock.Now=s.State.ExpiresAt;var expired=await expiry.Do(s,expiry.B,TradeCommandKind.Confirm);
        Check(expired.Session.State.Status==TradeNegotiationStatus.Expired && expired.Session.InitiatorReservation is null && await expiry.Balance(expiry.A)==100,"server-clock expiry closes without settlement");
        var bag=await Fixture.Create();var item=(await bag.Seed(bag.A,1))[0];await bag.Seed(bag.B,80,seedOffset:1000);
        s=await bag.Open();s=await bag.Offer(s,bag.A,new([new(item.ItemId,1,1)],10));s=await bag.ReadyBoth(s);s=(await bag.Do(s,bag.A,TradeCommandKind.Confirm)).Session;
        denied=await bag.Do(s,bag.B,TradeCommandKind.Confirm);Check(denied.Rejection=="trade_inventory_full" && denied.Session.InitiatorReservation is null && await bag.Balance(bag.A)==100,"capacity rejection releases without transfer");
    }
    private static async Task Races()
    {
        var f=await Fixture.Create();var s=await f.Open();s=await f.Offer(s,f.A,new([],20));var ready=Cmd(s,f.A,TradeCommandKind.Ready);
        var pair=await Concurrent(()=>f.Store.ExecuteAsync(ready,default),()=>f.Store.ExecuteAsync(ready,default));
        Check(JsonSerializer.Serialize(pair[0])==JsonSerializer.Serialize(pair[1]),"parallel identical Ready has one result");
        s=(await f.Do(pair[0].Session,f.B,TradeCommandKind.Ready)).Session;s=(await f.Do(s,f.A,TradeCommandKind.Confirm)).Session;
        var confirm=Cmd(s,f.B,TradeCommandKind.Confirm);var cancel=Cmd(s,f.A,TradeCommandKind.Cancel);
        async Task<TradeSessionResult?> Attempt(TradeSessionCommand cmd){try{return await f.Store.ExecuteAsync(cmd,default);}catch(TradeRuleException e)when(e.Code is "trade_revision_conflict" or "trade_closed"){return null;}}
        var race=await Concurrent(()=>Attempt(confirm),()=>Attempt(cancel));Check(race.Count(x=>x is not null)==1,"parallel final confirm/cancel has one winner");
        var current=(await f.Store.FindAsync(s.State.TradeId,f.A,default))!;Check(await f.Balance(f.A)==(current.Completed?80:100),"parallel winner agrees with balance");
        var admission=await Fixture.Create();s=await admission.Open();s=await admission.Offer(s,admission.A,new([],20));s=await admission.ReadyBoth(s);s=(await admission.Do(s,admission.A,TradeCommandKind.Confirm)).Session;
        var done=Cmd(s,admission.B,TradeCommandKind.Confirm);TradeSessionResult? tradeResult=null;
        await Concurrent(async()=>{tradeResult=await admission.Store.ExecuteAsync(done,default);return true;},
            async()=>{await admission.Game.CreateDungeonSessionAsync(admission.A.AccountId,admission.A.CharacterId,"Test","Normal",admission.Clock.Now,admission.Clock.Now.AddHours(1),default);return true;});
        Check(tradeResult!.Session.Completed?await admission.Balance(admission.A)==80:tradeResult.Rejection=="trade_requires_lobby"&&await admission.Balance(admission.A)==100,"actual admission/settlement share a linearization gate without deadlock");
    }
    private static async Task CurrentDefinition()
    {
        var f=await Fixture.Create();var s=await f.Open();s=await f.Offer(s,f.A,new([],20));s=await f.ReadyBoth(s);
        await f.Economy.UpsertDefinitionAsync(new("Gold","Gold",CurrencyScope.Account,110,true),f.Clock.Now,default);
        var wallet=await f.Economy.LoadWalletAsync((await f.Game.GetCharacterEconomyContextAsync(f.B.CharacterId,default))!,default);
        Check(wallet.Balances.Single().Definition.MaxBalance==110,"funded wallet uses latest definition instead of cached limit");
        s=(await f.Do(s,f.A,TradeCommandKind.Confirm)).Session;var result=await f.Do(s,f.B,TradeCommandKind.Confirm);
        Check(result.Rejection=="trade_balance_limit" && await f.Balance(f.A)==100 && await f.Balance(f.B)==100,"new balance limit rejects settlement atomically");
    }
    private static async Task<T[]> Concurrent<T>(Func<Task<T>> a,Func<Task<T>> b)
    {
        var start=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var one=Task.Run(async()=>{await start.Task;return await a();});var two=Task.Run(async()=>{await start.Task;return await b();});start.SetResult();
        return await Task.WhenAll(one,two).WaitAsync(TimeSpan.FromSeconds(10));
    }
    internal static TradeSessionCommand Cmd(TradeStoredSession s,TradeParticipant p,TradeCommandKind k)=>new(Guid.NewGuid(),s.State.TradeId,p,k,s.State.Revision,s.State.OfferRevision);
    private static void Check(bool value,string message){if(!value)throw new Exception(message);checks++;}
    private static async Task Reject(Func<Task> action,string code){try{await action();}catch(TradeRuleException e)when(e.Code==code){checks++;return;}throw new Exception("Expected "+code);}
    private sealed class InjectedFailure:Exception;
    private sealed class TestClock:TimeProvider
    {
        public DateTimeOffset Now=DateTimeOffset.UtcNow;public Func<bool>? When;public Action? Trigger;private bool checking;
        public override DateTimeOffset GetUtcNow(){if(!checking&&When is not null){checking=true;try{if(When()){When=null;var trigger=Trigger;Trigger=null;trigger?.Invoke();}}finally{checking=false;}}return Now;}
    }
    private sealed class Fixture
    {
        public TestClock Clock {get;}=new(); public InMemoryTransactionGate Gate {get;}
        public InMemoryItemRepository Items {get;} public InMemoryEconomyRepository Economy {get;} public InMemoryGameRepository Game {get;}
        public InMemoryTradeSessionRepository Store {get;} public InMemoryTradeReservationRepository Holds {get;}
        public TradeParticipant A {get;private set;}=null!;public TradeParticipant B {get;private set;}=null!;
        private Fixture(){Gate=new(Clock);Items=new(Gate);Economy=new(Gate);Game=new(Gate,Items,Clock);Store=new(Gate,Game,Items,Economy,Policy);Holds=new(Gate,Items,Economy,Game,Policy);}
        public static async Task<Fixture> Create(){var f=new Fixture();await f.Economy.UpsertDefinitionAsync(new("Gold","Gold",CurrencyScope.Account,100000,true),f.Clock.Now,default);f.A=await f.Person("76561198300000001","TraderA");f.B=await f.Person("76561198300000002","TraderB");return f;}
        private async Task<TradeParticipant> Person(string account,string name){await Game.UpsertAccountAsync(new(account,account,false,false),Clock.Now,default);var c=await Game.CreateCharacterAsync(account,name,Clock.Now,default);var p=new TradeParticipant(account,c.CharacterId);var r=await Economy.CommitAsync(new(Guid.NewGuid(),c.CharacterId,Guid.NewGuid(),"Test.Fund",Guid.NewGuid().ToString("N"),"test",[new("Gold",100)]),(await Game.GetCharacterEconomyContextAsync(c.CharacterId,default))!,Clock.Now,default);Check(r.Status==CurrencyTransactionStatus.Committed,"fixture funded");return p;}
        public async Task<long> Balance(TradeParticipant p)=>(await Economy.LoadWalletAsync((await Game.GetCharacterEconomyContextAsync(p.CharacterId,default))!,default)).Balances.Single().Balance;
        public async Task<TradeStoredSession> Open(){var invite=await Store.ExecuteAsync(new(Guid.NewGuid(),Guid.NewGuid(),A,TradeCommandKind.Invite,Invitee:B,CurrencyCode:"Gold"),default);return(await Do(invite.Session,B,TradeCommandKind.Accept)).Session;}
        public Task<TradeSessionResult> Do(TradeStoredSession s,TradeParticipant p,TradeCommandKind k)=>Store.ExecuteAsync(Cmd(s,p,k),default);
        public async Task<TradeStoredSession> Offer(TradeStoredSession s,TradeParticipant p,TradeOffer offer)=>(await Store.ExecuteAsync(Cmd(s,p,TradeCommandKind.ReplaceOffer) with {Offer=offer},default)).Session;
        public async Task<TradeStoredSession> ReadyBoth(TradeStoredSession s){var a=await Do(s,A,TradeCommandKind.Ready);Check(a.Rejection is null,"first ready");var b=await Do(a.Session,B,TradeCommandKind.Ready);Check(b.Rejection is null,"second ready");return b.Session;}
        public async Task<ItemRecord[]> Seed(TradeParticipant p,int count,int quantity=1,int seedOffset=0){List<ItemRecord> list=[];var owner=new ItemOwnerRef(ItemOwnerType.Character,p.CharacterId.ToString("D"));for(int slot=0;slot<count;slot++){ItemRecord item=new("Material","Ore",1,owner,new(ItemContainerType.Inventory,owner.OwnerId,160+slot),new(Guid.NewGuid(),quantity==1?slot+seedOffset+1:1,quantity,[],[]),0,ItemLifecycleState.Active,new(ItemBindState.Unbound,new(0,0),null,"Test",false));var r=await Items.CommitAsync(new(Guid.NewGuid(),"Test.Seed",Guid.NewGuid().ToString("N"),owner,quantity,[new(0,item)]),Clock.Now,default);Check(r.Status==ItemRepositoryCommitStatus.Committed,"seed item");list.Add(r.Records.Single());}return list.ToArray();}
        public async Task Mode(string mode){var store=new MemoryServiceControlStore(Game,Clock);var current=await store.ReadAsync(default);var result=await store.CommitAsync(new(new(Guid.NewGuid(),current.Value!.Revision,mode,"",null,null,"controlled trade fixture"),"test",Guid.NewGuid(),null),default);Check(result.IsSuccess,"service control fixture");}
        public async Task Restrict(){var store=new MemoryRestrictionStore(Game,Clock);var result=await store.CommitAsync(new(new(Guid.NewGuid(),A.AccountId,0,null,"controlled trade fixture"),"test",Guid.NewGuid(),null),default);Check(result.IsSuccess,"restriction fixture");}
    }
}
