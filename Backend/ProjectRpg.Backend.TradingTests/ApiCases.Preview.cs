using System.Text.Json;
using System.Text.Json.Nodes;
using Npgsql;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

internal static partial class ApiCases
{
    private static async Task PreviewCases(Fixture f)
    {
        int before = checks;
        var a = await f.Person(); var b = await f.Person(); var outsider = await f.Person();
        var own = await PreviewItem(f, a, 50);
        var other = await PreviewItem(f, b, 51);
        var foreign = await PreviewItem(f, outsider, 52);
        var locked = await PreviewItem(f, a, 53, locked: true);
        var bound = await PreviewItem(f, a, 54, bound: true);
        var expired = await PreviewItem(f, a, 55, expired: true);
        var disallowed = await PreviewItem(f, a, 56, disallowed: true);
        var id = Guid.NewGuid(); string route = Route(b, id) + "/preview";
        await Get(f, "invalid-token", route, 401); await Get(f, f.Admin, route, 403);
        var s = (await Send(f, a.Token, Route(a,id)+"/invite", new { requestId=Guid.NewGuid(),inviteeCharacterId=b.Id }))["session"]!.AsObject();
        s = (await Send(f,b.Token,Route(b,id)+"/accept",Command(s)))["session"]!.AsObject();
        async Task Offer(Person who, params ItemRecord[] records)
        {
            s=(await Send(f,who.Token,Route(who,id)+"/offer",new {requestId=Guid.NewGuid(),expectedRevision=Rev(s),offer=new {
                items=records.Select(i=>new {itemId=i.ItemId,expectedRevision=i.Revision.ToString(),quantity=1}),currencyAmount="0"}}))["session"]!.AsObject();
        }
        await Offer(a, own); await Offer(b, other);
        await Get(f, outsider.Token, Route(outsider,id)+"/preview",404); await Get(f, outsider.Token,route,403);
        await Get(f,b.Token,Route(b,Guid.NewGuid())+"/preview",404);
        var preview=(await Get(f,b.Token,route))!;
        var detail=preview["initiatorItems"]![0]!["details"]!;
        Check(preview["isOpen"]!.GetValue<bool>()&&preview["allItemsAvailable"]!.GetValue<bool>(),"live item availability");
        Check(detail["definitionName"]!.GetValue<string>()=="Api.Ore"&&detail["definitionVersion"]!.GetValue<int>()==1,"server definition preview");
        Check(detail["statValues"]![0]!["value"]!.GetValue<double>()==42.5&&detail["equipmentProgression"]!["level"]!.GetValue<int>()==2,"exact stats and growth");
        Check(detail["durability"]!["current"]!.GetValue<int>()==7&&detail["bindState"]!.GetValue<string>()=="Unbound"&&detail["instanceTags"]![0]!.GetValue<string>()=="Item.Test.Affix","durability, binding and gameplay tags");
        Check(JsonNode.DeepEquals(preview["session"],s)&&preview["initiatorItems"]![0]!["expectedRevision"]!.GetValue<string>()=="1","session and item version binding");
        var wire=preview.ToJsonString();
        foreach(var privateText in new[]{a.Account,b.Account,outsider.Account,"PrivateCreationMarker","generationSeed","slotIndex","ownerId","reservation","exchange"})
            Check(!wire.Contains(privateText,StringComparison.OrdinalIgnoreCase),"private field hidden: "+privateText);
        var snapshot=await f.Store.PreviewAsync(id,new(b.Account,b.Id),default);
        Check(snapshot!.InitiatorItems[0].Details!.StatValues is not ItemStatValue[],"detached read-only stat projection");
        await Offer(a,foreign,locked,bound,expired,disallowed,own with {Revision=own.Revision+1},
            own with {State=own.State with {InstanceId=Guid.NewGuid()}});
        preview=(await Get(f,b.Token,route))!;
        Check(!preview["allItemsAvailable"]!.GetValue<bool>()&&preview["initiatorItems"]!.AsArray().All(i=>i!["details"] is null&&i["availability"]!.GetValue<string>()=="Unavailable"),"foreign/missing/stale/locked/bound/expired/disallowed uniformly concealed");
        Check(preview["inviteeItems"]![0]!["details"] is not null,"valid opposite side survives invalid offer");
        await Offer(a,own);
        if(f.Data is not null)
        {
            await PreviewMvcc(f,id,b,own);
            s=(await Get(f,b.Token,Route(b,id)))!["session"]!.AsObject();
        }
        else
        {
            var changed=await f.Items.CommitAsync(new(Guid.NewGuid(),"Test.ChangePreview","change",own.Owner,1,
                [new(own.Revision,own with {State=own.State with {StatValues=[new("Stat.Attack",99)]}})]),f.Clock.Now,default);
            Check(changed.Status==ItemRepositoryCommitStatus.Committed,"preview did not reserve the item");
        }
        Check(snapshot.InitiatorItems[0].Details!.StatValues[0].Value==42.5,"previous preview cannot mutate with storage");
        preview=(await Get(f,b.Token,route))!;
        Check(preview["initiatorItems"]![0]!["details"] is null,"changed item invalidates detail without exposing new revision");
        var rejected=await Send(f,a.Token,Route(a,id)+"/ready",Consent(s),409);
        Check(rejected["rejection"]!.GetValue<string>()=="item_revision_conflict","Ready still rejects stale viewed item atomically");
        s=rejected["session"]!.AsObject();
        await Offer(a,(await f.Items.FindAsync(own.ItemId,default))!);
        f.Options.Enabled=false;
        Check((await Get(f,b.Token,route))!["allItemsAvailable"]!.GetValue<bool>(),"authenticated preview available during API disable");
        f.Options.Enabled=true;
        var cancelled=await Send(f,a.Token,Route(a,id)+"/cancel",Command(s));
        preview=(await Get(f,b.Token,route))!;
        Check(!preview["isOpen"]!.GetValue<bool>()&&preview["initiatorItems"]![0]!["details"] is null&&preview["inviteeItems"]![0]!["details"] is null,"closed trade does not reveal live assets");
        Check(JsonNode.DeepEquals(cancelled["session"],preview["session"]),"closed receipt unchanged by preview");
        // Completed and timed-out previews also expose no current asset details.
        var complete=await f.Store.ExecuteAsync(new(Guid.NewGuid(),Guid.NewGuid(),new(a.Account,a.Id),TradeCommandKind.Invite,Invitee:new(b.Account,b.Id),CurrencyCode:"Gold"),default);
        var state=complete.Session;
        async Task Step(TradeParticipant actor,TradeCommandKind kind,TradeOffer? offer=null)
        { state=(await f.Store.ExecuteAsync(new(Guid.NewGuid(),state.State.TradeId,actor,kind,state.State.Revision,state.State.OfferRevision,Offer:offer),default)).Session; }
        var pa=state.State.Initiator;var pb=state.State.Invitee;
        await Step(pb,TradeCommandKind.Accept);var current=(await f.Items.FindAsync(own.ItemId,default))!;
        await Step(pa,TradeCommandKind.ReplaceOffer,new([new(current.ItemId,current.Revision,1)],0));
        await Step(pa,TradeCommandKind.Ready);await Step(pb,TradeCommandKind.Ready);await Step(pa,TradeCommandKind.Confirm);await Step(pb,TradeCommandKind.Confirm);
        preview=(await Get(f,b.Token,Route(b,state.State.TradeId)+"/preview"))!;
        Check(preview["session"]!["completed"]!.GetValue<bool>()&&!preview["isOpen"]!.GetValue<bool>()&&preview["initiatorItems"]![0]!["details"] is null,"completed preview hides transferred live item");
        var due=await f.Store.ExecuteAsync(new(Guid.NewGuid(),Guid.NewGuid(),pa,TradeCommandKind.Invite,Invitee:pb,CurrencyCode:"Gold"),default);
        await f.MakeExpired(due.Session.State.TradeId);
        preview=(await Get(f,b.Token,Route(b,due.Session.State.TradeId)+"/preview"))!;
        Check(!preview["isOpen"]!.GetValue<bool>()&&!preview["allItemsAvailable"]!.GetValue<bool>(),"deadline without worker is non-inspectable");
        Console.WriteLine($"PASS trade preview: {checks-before} assertions; owned exact-revision details, privacy, snapshot, no writes, stale Ready and closed states");
    }

    private static async Task<ItemRecord> PreviewItem(Fixture f,Person p,int slot,bool locked=false,bool bound=false,bool expired=false,bool disallowed=false)
    {
        var owner=new ItemOwnerRef(ItemOwnerType.Character,p.Id.ToString("D"));
        ItemRecord item=new("Material",disallowed?"Private.Disallowed":"Api.Ore",1,owner,new(ItemContainerType.Inventory,owner.OwnerId,slot),
            new(Guid.NewGuid(),912345,1,["Item.Test.Affix"],[new("Stat.Attack",42.5)],FirstSliceEquipmentProgression.Create() with {Level=2}),
            0,ItemLifecycleState.Active,new(bound?ItemBindState.AccountBound:ItemBindState.Unbound,new(7,10),expired?f.Clock.Now.AddMinutes(-1):null,"PrivateCreationMarker",locked));
        var r=await f.Items.CommitAsync(new(Guid.NewGuid(),"Test.PreviewSeed",Guid.NewGuid().ToString("N"),owner,1,[new(0,item)]),f.Clock.Now,default);
        Check(r.Status==ItemRepositoryCommitStatus.Committed,"preview seed");return r.Records.Single();
    }

    private static async Task PreviewMvcc(Fixture f,Guid id,Person viewer,ItemRecord item)
    {
        // Hold the item SELECT after the session read, then commit an item/session change together.
        // REPEATABLE READ must return the old coherent pair, not old consent with new item data.
        await using var c=await f.Data!.OpenConnectionAsync();await using var tx=await c.BeginTransactionAsync();
        await using(var q=new NpgsqlCommand("LOCK TABLE item_records IN ACCESS EXCLUSIVE MODE",c,tx))await q.ExecuteNonQueryAsync();
        using var timeout=new CancellationTokenSource(TimeSpan.FromSeconds(15));
        var read=f.Store.PreviewAsync(id,new(viewer.Account,viewer.Id),timeout.Token);
        bool waiting=false;
        await using(var monitor=await f.Data.OpenConnectionAsync())
        {
            for(int i=0;i<100;i++)
            {
                await using var q=new NpgsqlCommand("SELECT EXISTS(SELECT 1 FROM pg_stat_activity WHERE datname=current_database() AND pid<>pg_backend_pid() AND wait_event_type='Lock' AND query LIKE '%WHERE item_id=ANY(%')",monitor);
                if((bool)(await q.ExecuteScalarAsync())!){waiting=true;break;}await Task.Delay(20);
            }
        }
        if(!waiting){await tx.RollbackAsync();await read;throw new Exception("Preview did not reach controlled item read");}
        await using(var q=new NpgsqlCommand("""
            UPDATE item_records SET revision=revision+1 WHERE item_id=@item;
            UPDATE trade_sessions SET revision=revision+1,payload=jsonb_set(payload,'{State,Revision}',to_jsonb(revision+1)) WHERE trade_id=@trade
            """,c,tx)){q.Parameters.AddWithValue("item",item.ItemId);q.Parameters.AddWithValue("trade",id);await q.ExecuteNonQueryAsync();}
        await tx.CommitAsync();var old=await read;
        Check(old!.InitiatorItems.Single().Details is not null&&old.InitiatorItems.Single().ExpectedRevision==item.Revision,"PG preview keeps old exact item version across a concurrent commit");
        var latest=await f.Store.PreviewAsync(id,new(viewer.Account,viewer.Id),default);
        Check(latest!.Session.State.Revision==old.Session.State.Revision+1&&latest.InitiatorItems.Single().Details is null,"next PG snapshot observes changed session/item without disclosure");
    }
}
