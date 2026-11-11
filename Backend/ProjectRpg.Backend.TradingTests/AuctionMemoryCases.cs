using System.Text.Json;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;
using ProjectRpg.Backend.Operations.Restrictions;
using ProjectRpg.Backend.Operations.ServiceControl;

internal static class AuctionMemoryCases
{
    private static readonly TradeItemPolicy Policy = new([new("Material", "Ore", 1, true, true, 100)]);
    private static int checks, sequence;
    public static async Task<int> Run()
    {
        try { await Lifecycle(); await DeliveryAndGuards(); await Access(); await Reservations(); await Rollback(); await Races(); await Scopes();
            Console.WriteLine($"PASS Auction Memory: {checks} assertions. Actual shared Memory assets; no process restart persistence/PG/API/UE claim."); return 0; }
        catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }
    private static async Task Lifecycle()
    {
        var f = await Fixture.Create(); var seller = await f.Person(); var buyer = await f.Person(); await f.Fund(seller, 1000); await f.Fund(buyer, 1000);
        var source = await f.Seed(seller); var reg = f.Register(seller, source); var held = (await f.Store.ExecuteAsync(reg, default)).Listing;
        Check((await f.Items.FindAsync(source.ItemId, default))!.State.Quantity == 6 && held.Split is not null, "partial escrow source/provenance");
        var cmd = Buy(held, buyer); var result = await f.Store.PurchaseAsync(cmd, default);
        Check(await f.Balance(buyer) == 301 && await f.Balance(seller) == 1000 && result.Listing.Sale!.Amounts == new AuctionSaleAmounts(699,665,34), "one payment and exact fee/net; full seller unchanged");
        var delivered = (await f.Items.FindAsync(held.EscrowAtRegistration.ItemId, default))!;
        Check(delivered.Owner.OwnerId == buyer.CharacterId.ToString("D") && delivered.State.Quantity == 4 && delivered.State.GenerationSeed == source.State.GenerationSeed, "split identity/options delivered");
        Check(Json(result) == Json(await f.NewStore().PurchaseAsync(cmd, default)), "new repository on same gate shares receipt/state");
        var array = (IList<ItemRecord>)result.Items; array[0] = array[0] with { Revision = 999 };
        Check((await f.Store.PurchaseAsync(cmd, default)).Items[0].Revision != 999, "returned collections isolated from durable-in-process JSON");
        Check((await f.Store.ExecuteAsync(reg, default)).Listing.Sale is null, "original registration receipt immutable after sale");
        await Reject(() => f.Store.PurchaseAsync(cmd with { ExpectedTotalPrice = 1 }, default), "auction_idempotency_conflict");
        await Reject(() => f.Store.PurchaseAsync(cmd with { RequestId = reg.RequestId }, default), "auction_idempotency_conflict");
        var p = (await f.Store.FindPayableAsync(cmd.ListingId, seller, default))!; var claim = Claim(p, seller, 300);
        var zero = await f.Store.ClaimAsync(claim, default);
        Check(zero.Paid == 0 && zero.Payable == p, "full bank keeps payable and revision");
        await f.Fund(seller, -500);
        Check((await f.Store.ClaimAsync(claim, default)).Paid == 0 && await f.Balance(seller) == 500, "old zero receipt never pays newly available room");
        var paid = await f.Store.ClaimAsync(claim with { RequestId = Guid.NewGuid() }, default);
        Check(paid.Paid == 300 && paid.Payable.RemainingAmount == 365 && await f.Balance(seller) == 800, "requested partial amount conserved");
        var next = await f.Store.ClaimAsync(Claim(paid.Payable,seller,long.MaxValue),default);
        Check(next.Paid == 200 && next.Payable.RemainingAmount == 165 && await f.Balance(seller) == 1000, "partial claim respects wallet cap");
        await Reject(() => f.Store.ClaimAsync(claim with { RequestedAmount = 301 }, default), "auction_idempotency_conflict");
        await Reject(() => f.Store.ClaimAsync(claim with { RequestId = cmd.RequestId }, default), "auction_idempotency_conflict");
        await Reject(() => f.Store.ExecuteAsync(Close(held) with { RequestId = claim.RequestId }, default), "auction_idempotency_conflict");
        await Reject(() => f.Store.PurchaseAsync(cmd with { RequestId = claim.RequestId }, default), "auction_idempotency_conflict");
    }
    private static async Task DeliveryAndGuards()
    {
        var f=await Fixture.Create();var s=await f.Person();var b=await f.Person();await f.Fund(b,1000);
        var source=await f.Seed(s);var target=await f.Seed(b,96);var l=(await f.Store.ExecuteAsync(f.Register(s,source),default)).Listing;
        var held=l.EscrowAtRegistration;
        var theft = held with {Owner=source.Owner,Location=source.Location};
        Check((await f.Items.CommitAsync(new(Guid.NewGuid(),"Test.Theft","test",source.Owner,4,[new(held.Revision,theft)]),f.Clock.Now,default)).Status==ItemRepositoryCommitStatus.ValidationFailed,"normal mutation cannot steal escrow");
        await Reject(()=>f.Store.PurchaseAsync(Buy(l,b) with {ExpectedRevision=2},default),"auction_revision_conflict");
        await Reject(()=>f.Store.PurchaseAsync(Buy(l,b) with {ExpectedCurrencyCode="Other"},default),"auction_price_conflict");
        await Reject(()=>f.Store.PurchaseAsync(Buy(l,s),default),"auction_self_purchase");
        await Reject(()=>f.NewStore(new([])).PurchaseAsync(Buy(l,b),default),"item_trade_not_allowed");
        await f.Store.PurchaseAsync(Buy(l,b),default);
        Check((await f.Items.FindAsync(target.ItemId,default))!.State.Quantity==100 && (await f.Items.FindAsync(held.ItemId,default))!.LifecycleState==ItemLifecycleState.Consumed,"merge target and vault tombstone");
        var tomb=(await f.Items.FindAsync(held.ItemId,default))!;
        Check((await f.Items.CommitAsync(new(Guid.NewGuid(),"Test.Revive","test",source.Owner,1,[new(tomb.Revision,theft with {Revision=tomb.Revision,State=theft.State with {Quantity=1}})]),f.Clock.Now,default)).Status==ItemRepositoryCommitStatus.ValidationFailed,"normal mutation cannot revive consumed vault ID");
        var small=await Fixture.Create();var ss=await small.Person();var bb=await small.Person();await small.Fund(bb,1000);
        var ll=(await small.Store.ExecuteAsync(small.Register(ss,await small.Seed(ss)),default)).Listing;
        await Reject(()=>small.NewStore(cap:3).PurchaseAsync(Buy(ll,bb),default),"auction_delivery_capacity");
        Check(await small.Balance(bb)==1000 && (await small.Store.FindAsync(ll.State.ListingId,ss,default))!.State.Status==AuctionListingStatus.Active,"capacity failure leaves payment/escrow pending");
        for(int slot=160;slot<240;slot++)await small.Seed(bb,100,slot,slot);
        var mail=await small.Store.PurchaseAsync(Buy(ll,bb),default);
        Check(mail.Items.Single().Location.ContainerType==ItemContainerType.Mail,"full material tab delivers overflow to mail");
        var returner=await Fixture.Create();var rp=await returner.Person();var ri=await returner.Seed(rp);
        var rl=(await returner.Store.ExecuteAsync(returner.Register(rp,ri),default)).Listing;
        await returner.Store.ExecuteAsync(Close(rl),default);
        Check((await returner.Items.FindAsync(ri.ItemId,default))!.State.Quantity==10 && (await returner.Items.FindAsync(rl.EscrowAtRegistration.ItemId,default))!.LifecycleState==ItemLifecycleState.Consumed,"cancel merges source and tombstones split");
        var exp=await Fixture.Create();var ep=await exp.Person();var ei=await exp.Seed(ep,expiry:exp.Clock.Now.AddMinutes(2));
        var ec=exp.Register(ep,ei,10) with {ExpiresAt=exp.Clock.Now.AddMinutes(1)};var el=(await exp.Store.ExecuteAsync(ec,default)).Listing;
        exp.Clock.Now=exp.Clock.Now.AddMinutes(3);
        var expired=await exp.Store.ExecuteAsync(Close(el) with {Kind=AuctionEscrowKind.Expire},default);
        Check(expired.Items.Single().LifecycleState==ItemLifecycleState.Expired,"expired item returns only expired tombstone");
        try { f.NewStore(economy:new(new InMemoryTransactionGate()));throw new Exception("mismatched gate accepted"); } catch(InvalidOperationException){checks++;}
    }
    private static async Task Access()
    {
        var f=await Fixture.Create();var s=await f.Person();var b=await f.Person();var foreign=await f.Person();await f.Fund(b,1000);
        var l=(await f.Store.ExecuteAsync(f.Register(s,await f.Seed(s)),default)).Listing;var cmd=Buy(l,b);
        Check(await f.Store.FindAsync(cmd.ListingId,foreign,default) is null,"foreign seller listing hidden");
        await Reject(()=>f.Store.PurchaseAsync(cmd with {Buyer=b with {CharacterId=foreign.CharacterId}},default),"character_not_owned");
        await f.Game.CreateDungeonSessionAsync(b.AccountId,b.CharacterId,"Test","Normal",f.Clock.Now,f.Clock.Now.AddMinutes(1),default);
        await Reject(()=>f.Store.PurchaseAsync(cmd,default),"auction_requires_lobby");f.Clock.Now=f.Clock.Now.AddMinutes(2);
        await f.Mode("Drain");await Expect<ServiceDrainingException>(()=>f.Store.PurchaseAsync(cmd,default));await f.Mode("Open");
        await f.Game.CreateDungeonSessionAsync(s.AccountId,s.CharacterId,"Test","Normal",f.Clock.Now,f.Clock.Now.AddMinutes(1),default);
        var result=await f.Store.PurchaseAsync(cmd,default);Check(result.Listing.Sale is not null,"seller dungeon lease does not block sale");
        Check(await f.Store.FindPayableAsync(cmd.ListingId,foreign,default) is null,"foreign payable hidden");
        var pending=(await f.Store.FindPayableAsync(cmd.ListingId,s,default))!;
        await Reject(()=>f.Store.ClaimAsync(Claim(pending,s,100),default),"auction_requires_lobby");
        await f.Restrict(b);await f.Mode("Drain");Check(Json(result)==Json(await f.Store.PurchaseAsync(cmd,default)),"receipt replay works after restriction and Drain");
        var cancel=await Fixture.Create();var p=await cancel.Person();var item=await cancel.Seed(p);var held=(await cancel.Store.ExecuteAsync(cancel.Register(p,item),default)).Listing;
        await cancel.Restrict(p);await cancel.Mode("Drain");var closed=await cancel.Store.ExecuteAsync(Close(held),default);
        Check(closed.Listing.State.Status==AuctionListingStatus.Cancelled,"restricted/Drain seller can recover unsold item");
    }
    private static async Task Reservations()
    {
        var f=await Fixture.Create();var s=await f.Person();var b=await f.Person();await f.Fund(b,1000);
        var target=await f.Seed(b,90);var source=await f.Seed(s);var reservations=new InMemoryTradeReservationRepository(f.Gate,f.Items,f.Money,f.Game,Policy);
        var h=new TradeReservationCommand(Guid.NewGuid(),Guid.NewGuid(),1,b,"Gold",new([new(target.ItemId,target.Revision,1)],400),f.Clock.Now.AddMinutes(5));
        await reservations.ReserveAsync(h,default);var l=(await f.Store.ExecuteAsync(f.Register(s,source),default)).Listing;
        await Reject(()=>f.Store.PurchaseAsync(Buy(l,b),default),"auction_insufficient_balance");
        await reservations.ReleaseAsync(h,default);
        await reservations.ReserveAsync(h with {ReservationId=Guid.NewGuid(),Offer=new([new(target.ItemId,target.Revision,1)],0)},default);
        var bought=await f.Store.PurchaseAsync(Buy(l,b),default);
        Check((await f.Items.FindAsync(target.ItemId,default))!.State.Quantity==90 && bought.Items.Single().State.Quantity==4,"reserved buyer merge target stays untouched");
    }
    private static async Task Rollback()
    {
        var f=await Fixture.Create();var s=await f.Person();var b=await f.Person();await f.Fund(b,1000);var l=(await f.Store.ExecuteAsync(f.Register(s,await f.Seed(s)),default)).Listing;var cmd=Buy(l,b);
        using var cancelled=new CancellationTokenSource();
        f.Clock.OnRead=()=>{if(f.Balance(b).GetAwaiter().GetResult()==301)cancelled.Cancel();};
        await Expect<OperationCanceledException>(()=>f.Store.PurchaseAsync(cmd,cancelled.Token));f.Clock.OnRead=null;
        Check(await f.Balance(b)==1000 && await f.Store.FindPayableAsync(cmd.ListingId,s,default) is null
            && Json((await f.Store.FindAsync(cmd.ListingId,s,default))!)==Json(l)
            && (await f.Items.FindAsync(l.EscrowAtRegistration.ItemId,default))!.Owner==AuctionEscrowRules.Owner(cmd.ListingId),"late purchase cancellation restores items, wallet, listing, payable and receipt");
        await f.Store.PurchaseAsync(cmd,default);var p=(await f.Store.FindPayableAsync(cmd.ListingId,s,default))!;var claim=Claim(p,s,150);
        using var cancelClaim=new CancellationTokenSource();f.Clock.OnRead=()=>{if(f.Balance(s).GetAwaiter().GetResult()==150)cancelClaim.Cancel();};
        await Expect<OperationCanceledException>(()=>f.Store.ClaimAsync(claim,cancelClaim.Token));f.Clock.OnRead=null;
        Check(await f.Balance(s)==0 && (await f.Store.FindPayableAsync(cmd.ListingId,s,default))==p,"late claim cancellation restores wallet/payable/receipt");
        Check((await f.Store.ClaimAsync(claim,default)).Paid==150,"same failed request retries successfully after rollback");
    }
    private static async Task Races()
    {
        for(int i=0;i<3;i++){
            var f=await Fixture.Create();var s=await f.Person();var a=await f.Person();var b=await f.Person();await f.Fund(a,1000);await f.Fund(b,1000);
            var l=(await f.Store.ExecuteAsync(f.Register(s,await f.Seed(s)),default)).Listing;
            var buyers=await Task.WhenAll(Task.Run(()=>Try(()=>f.Store.PurchaseAsync(Buy(l,a),default))),Task.Run(()=>Try(()=>f.Store.PurchaseAsync(Buy(l,b),default))));
            Check(buyers.Count(x=>x)==1 && await f.Balance(a)+await f.Balance(b)==1301,"two buyers have one winner/debit");
            var p=(await f.Store.FindPayableAsync(l.State.ListingId,s,default))!;var claim=Claim(p,s,500);
            var claims=await Task.WhenAll(Task.Run(()=>Try(()=>f.Store.ClaimAsync(claim,default))),Task.Run(()=>Try(()=>f.Store.ClaimAsync(claim with {RequestId=Guid.NewGuid()},default))));
            Check(claims.Count(x=>x)==1 && await f.Balance(s)==500 && (await f.Store.FindPayableAsync(p.ListingId,s,default))!.RemainingAmount==165,"different claim IDs one revision winner");
            var f2=await Fixture.Create();var ss=await f2.Person();var bb=await f2.Person();await f2.Fund(bb,1000);var ll=(await f2.Store.ExecuteAsync(f2.Register(ss,await f2.Seed(ss)),default)).Listing;
            var disposition=await Task.WhenAll(Task.Run(()=>Try(()=>f2.Store.PurchaseAsync(Buy(ll,bb),default))),Task.Run(()=>Try(()=>f2.Store.ExecuteAsync(Close(ll),default))));
            Check(disposition.Count(x=>x)==1 && await f2.Balance(bb)==((await f2.Store.FindAsync(ll.State.ListingId,ss,default))!.Sale is null?1000:301),"purchase/cancel atomically choose one disposition");
        }
    }
    private static async Task Scopes()
    {
        foreach(var scope in Enum.GetValues<CurrencyScope>()){
            var f=await Fixture.Create();var code="Scope"+scope;await f.Money.UpsertDefinitionAsync(new(code,code,scope,long.MaxValue,true),f.Clock.Now,default);
            var s=await f.Person();var alt=await f.Person(s.AccountId);var b=await f.Person();await f.Fund(b,long.MaxValue,code);
            var reg=f.Register(s,await f.Seed(s)) with {TotalPrice=long.MaxValue,Terms=new(code,1,long.MaxValue,TimeSpan.FromMilliseconds(1),TimeSpan.FromDays(7),333)};
            var l=(await f.Store.ExecuteAsync(reg,default)).Listing;var sold=await f.Store.PurchaseAsync(Buy(l,b),default);var p=(await f.Store.FindPayableAsync(reg.ListingId,s,default))!;
            if(scope==CurrencyScope.Character)Check(await f.Store.FindPayableAsync(reg.ListingId,alt,default)is null,"character scope blocks alt");else Check(await f.Store.FindPayableAsync(reg.ListingId,alt,default)==p,"shared wallet permits alt");
            var actor=scope==CurrencyScope.Character?s:alt;var paid=await f.Store.ClaimAsync(Claim(p,actor,long.MaxValue),default);
            Check(paid.Paid==sold.Listing.Sale!.Amounts.SellerPayable && paid.Payable.RemainingAmount==0 && await f.Balance(actor,code)==paid.Paid,"maximum int64 sale and claim for "+scope);
        }
    }
    private static AuctionPurchaseCommand Buy(AuctionEscrowListing l,TradeParticipant p)=>new(Guid.NewGuid(),l.State.ListingId,p,l.State.Revision,l.State.Terms.CurrencyCode,l.State.TotalPrice);
    private static AuctionClaimCommand Claim(AuctionSellerPayable p,TradeParticipant actor,long amount)=>new(Guid.NewGuid(),p.ListingId,actor,p.Revision,p.CurrencyCode,amount);
    private static AuctionEscrowCommand Close(AuctionEscrowListing l)=>new(Guid.NewGuid(),l.State.ListingId,l.State.Seller,AuctionEscrowKind.Cancel,l.State.Revision);
    private static string Json<T>(T value)=>JsonSerializer.Serialize(value);
    private static void Check(bool ok,string text){if(!ok)throw new Exception(text);checks++;}
    private static async Task Reject(Func<Task> action,string code){try{await action();}catch(TradeRuleException e)when(e.Code==code){checks++;return;}throw new Exception("Expected "+code);}
    private static async Task Expect<T>(Func<Task> action)where T:Exception{try{await action();}catch(T){checks++;return;}throw new Exception("Expected "+typeof(T).Name);}
    private static async Task<bool> Try(Func<Task> action){try{await action();return true;}catch(TradeRuleException e)when(e.Code is "auction_revision_conflict" or "auction_not_active" or "auction_payable_revision_conflict"){return false;}}
    private sealed class Clock:TimeProvider{public DateTimeOffset Now=new(2026,11,12,0,0,0,TimeSpan.Zero);public Action? OnRead;public override DateTimeOffset GetUtcNow(){OnRead?.Invoke();return Now;}}
    private sealed class Fixture
    {
        public Clock Clock=new();public InMemoryTransactionGate Gate=null!;public InMemoryItemRepository Items=null!;public InMemoryEconomyRepository Money=null!;public InMemoryGameRepository Game=null!;public InMemoryAuctionRepository Store=null!;
        public static async Task<Fixture>Create(){var f=new Fixture();f.Gate=new(f.Clock);f.Items=new(f.Gate);f.Money=new(f.Gate);f.Game=new(f.Gate,f.Items,f.Clock);f.Store=f.NewStore();await f.Money.UpsertDefinitionAsync(new("Gold","Gold",CurrencyScope.Account,1000,true),f.Clock.Now,default);return f;}
        public InMemoryAuctionRepository NewStore(TradeItemPolicy? policy=null,int cap=500,InMemoryEconomyRepository? economy=null)=>new(Gate,Game,Items,economy??Money,policy??Policy,new(10,cap));
        public async Task<TradeParticipant>Person(string? account=null){account??=(76561198790000000L+Interlocked.Increment(ref sequence)).ToString();await Game.UpsertAccountAsync(new(account,account,false,false),Clock.Now,default);var c=await Game.CreateCharacterAsync(account,"Memory"+Interlocked.Increment(ref sequence),Clock.Now,default);return new(account,c.CharacterId);}
        public async Task<ItemRecord>Seed(TradeParticipant p,int quantity=10,int slot=160,int seed=1,DateTimeOffset? expiry=null){var owner=new ItemOwnerRef(ItemOwnerType.Character,p.CharacterId.ToString("D"));var item=new ItemRecord("Material","Ore",1,owner,new(ItemContainerType.Inventory,owner.OwnerId,slot),new(Guid.NewGuid(),seed,quantity,[],[]),0,ItemLifecycleState.Active,new(ItemBindState.Unbound,new(0,0),expiry,"Test",false));var r=await Items.CommitAsync(new(Guid.NewGuid(),"Test.Seed","seed",owner,quantity,[new(0,item)]),Clock.Now,default);Check(r.Status==ItemRepositoryCommitStatus.Committed,"seed fixture");return r.Records.Single();}
        public AuctionEscrowCommand Register(TradeParticipant p,ItemRecord i,int quantity=4)=>new(Guid.NewGuid(),Guid.NewGuid(),p,AuctionEscrowKind.Register,Item:new(i.ItemId,i.Revision,quantity),TotalPrice:699,ExpiresAt:Clock.Now.AddHours(1),Terms:new("Gold",1,long.MaxValue,TimeSpan.FromMilliseconds(1),TimeSpan.FromDays(7),500));
        public async Task Fund(TradeParticipant p,long amount,string code="Gold"){var r=await Money.CommitAsync(new(Guid.NewGuid(),p.CharacterId,Guid.NewGuid(),"Test.Fund",Guid.NewGuid().ToString("N"),"test",[new(code,amount)]),(await Game.GetCharacterEconomyContextAsync(p.CharacterId,default))!,Clock.Now,default);Check(r.Status==CurrencyTransactionStatus.Committed,"fund fixture");}
        public async Task<long>Balance(TradeParticipant p,string code="Gold")=>(await Money.LoadWalletAsync((await Game.GetCharacterEconomyContextAsync(p.CharacterId,default))!,default)).Balances.Single(x=>x.Definition.CurrencyCode==code).Balance;
        public async Task Mode(string mode){var store=new MemoryServiceControlStore(Game,Clock);var current=await store.ReadAsync(default);var r=await store.CommitAsync(new(new(Guid.NewGuid(),current.Value!.Revision,mode,"",null,null,"controlled auction fixture"),"test",Guid.NewGuid(),null),default);Check(r.IsSuccess,"mode fixture");}
        public async Task Restrict(TradeParticipant p){var r=await new MemoryRestrictionStore(Game,Clock).CommitAsync(new(new(Guid.NewGuid(),p.AccountId,0,null,"controlled auction fixture"),"test",Guid.NewGuid(),null),default);Check(r.IsSuccess,"restriction fixture");}
    }
}
