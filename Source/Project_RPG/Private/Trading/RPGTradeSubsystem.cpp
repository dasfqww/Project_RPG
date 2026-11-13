#include "Trading/RPGTradeSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Manager/HttpWebManager.h"
#include "Manager/DataManager.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "Progression/RPGEquipmentMutationSubsystem.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"

void URPGTradeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection); bEnding = false;
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickTrade), .25f);
}
void URPGTradeSubsystem::Deinitialize()
{
    bEnding = true; FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); TickHandle.Reset();
    if (EquipmentReader.IsValid()) EquipmentReader->OnEquipmentMutationsChanged.RemoveDynamic(this,&ThisClass::HandleTradeBagChanged);
    EquipmentReader.Reset(); CachedBag = {}; bBagWaiting = false;
    if (Owner) Owner->Shutdown(); Owner.Reset();
    Super::Deinitialize();
}
FRPGTradeLobbyContext URPGTradeSubsystem::ReadContext() const
{
    FRPGTradeLobbyContext Result;
    auto* Player = GetLocalPlayer(); auto* GI = Player ? Player->GetGameInstance() : nullptr;
    auto* Web = GI ? GI->GetSubsystem<UHttpWebManager>() : nullptr;
    if (bEnding || !GI || GI->GetFirstGamePlayer() != Player || !Web || IsRunningDedicatedServer() || Web->IsConfiguredForDungeonServer()) return Result;
    Result.bEnabled = bTradingUIEnabled;
    Result.Endpoint = Web->GetApiUrl();
    Result.Account = FRPGBackendPlayerAuthState::IsSteamId(Web->GetAuthenticatedSteamId()) ? Web->GetAuthenticatedSteamId() : FString();
    Result.bAuthenticated = Web->HasAccessToken();
    auto* Flow = GI->GetSubsystem<URPGDungeonSessionSubsystem>();
    if (Flow) FGuid::Parse(Flow->GetSelectedCharacterId(), Result.Character);
    auto* Lobby = GI->GetSubsystem<URPGOnlineLobbySubsystem>();
    auto* PC = GI->GetWorld() ? Player->GetPlayerController(GI->GetWorld()) : nullptr;
    Result.bLocalLobby = Lobby && Lobby->IsLobbyController(PC);
    auto* Widget = Lobby ? Lobby->GetLobbyWidget() : nullptr;
    Result.bEligible = Result.bLocalLobby && Widget && Widget->GetOwningLocalPlayer() == Player && Widget->CanUseTrading();
    return Result;
}
TSharedPtr<FRPGTradeLobbyOwner> URPGTradeSubsystem::GetTradeOwner()
{
    if (bEnding) return nullptr;
    if (!Owner)
    {
        auto* Player = GetLocalPlayer(); auto* GI = Player ? Player->GetGameInstance() : nullptr;
        auto* Web = GI ? GI->GetSubsystem<UHttpWebManager>() : nullptr;
        if (!GI || GI->GetFirstGamePlayer() != Player || !Web) return nullptr;
        TWeakObjectPtr<ThisClass> WeakThis(this);
        Owner = FRPGTradeLobbyOwner::Create(Web->CreatePlayerTradeTransport(), [WeakThis]
            { auto* Self = WeakThis.Get(); return Self ? Self->ReadContext() : FRPGTradeLobbyContext{}; }, {},
            FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("TradingIntents/v1")));
        Owner->SetDefinitionResolver([WeakThis](const FRPGTradeItemDetails& Item, FText& Name, FText& Description)
            { const auto* Self = WeakThis.Get(); return Self && Self->ResolveItem(Item,Name,Description); });
        EnsureBagReader();
        Owner->SetBagSource([WeakThis] { auto* Self=WeakThis.Get(); return Self ? Self->ReadTradeBag() : FRPGTradeBagSnapshot{}; },
            [WeakThis] { auto* Self=WeakThis.Get(); return Self && Self->ReloadTradeBag(); });
        Owner->SetAssetSink([WeakThis]
        {auto* S=WeakThis.Get();if(!S || S->bEnding || !S->EquipmentReader.IsValid())return false;
            S->CachedBag={};return S->EquipmentReader->BeginExternalRead(S->ReadContext().Character,S->AssetTicket);},
            [WeakThis](const FRPGTradeAssets& Assets)
        {auto* S=WeakThis.Get();return S && !S->bEnding && S->EquipmentReader.IsValid()
            && S->EquipmentReader->ApplyExternalRead(S->AssetTicket,Assets.Items,Assets.Mail,Assets.Wallet);},
            [WeakThis]
        {const auto* S=WeakThis.Get();return S && !S->bEnding && S->EquipmentReader.IsValid()
            && S->EquipmentReader->IsExternalReadCurrent(S->AssetTicket);});
    }
    return Owner;
}
bool URPGTradeSubsystem::TickTrade(float)
{
    if (bEnding) return false;
    UpdateTradeBag();
    if (auto Current = GetTradeOwner()) Current->Tick();
    return true;
}
bool URPGTradeSubsystem::IsAvailable() { const auto Current = GetTradeOwner(); return Current && Current->IsAvailable(); }
bool URPGTradeSubsystem::BlocksTransitions() { const auto Current = GetTradeOwner(); return Current && Current->BlocksTransitions(); }
bool URPGTradeSubsystem::ReloadTradeBag()
{
    const auto Scope = ReadContext();
    if (bEnding || bBagWaiting || !Scope.bEligible || !Scope.bAuthenticated || !Scope.bLocalLobby || !Scope.Character.IsValid()
        || !EquipmentReader.IsValid()) return false;
    if (EquipmentReader->IsRequestInFlight() || EquipmentReader->IsItemsReadInFlight()) return false;
    // Restore existing durable equipment intent state before trusting a new bag read; never dispatch that intent here.
    CachedBag = {};
    const auto Recovery=EquipmentReader->OpenCharacter(Scope.Character);
    const auto Current=ReadContext();
    if (Recovery.State!=ERPGEquipmentMutationState::Idle || !Current.bEligible || !Current.bAuthenticated
        || Current.Endpoint!=Scope.Endpoint || Current.Account!=Scope.Account || Current.Character!=Scope.Character) return false;
    CachedBag = {}; CachedBag.Endpoint=Scope.Endpoint; CachedBag.Account=Scope.Account; CachedBag.Character=Scope.Character;
    CachedBag.bLoading=true; bBagWaiting=true; bBagSawLoading=false; BagReadDeadline=FPlatformTime::Seconds()+40;
    const bool Started=EquipmentReader->RefreshItems(Scope.Character);
    if (!Started) { bBagWaiting=false; CachedBag.bLoading=false; }
    return Started;
}
void URPGTradeSubsystem::EnsureBagReader()
{
    if(bEnding)return;
    auto* P=GetLocalPlayer();auto* GI=P?P->GetGameInstance():nullptr;
    if(!EquipmentReader.IsValid() && GI)EquipmentReader=GI->GetSubsystem<URPGEquipmentMutationSubsystem>();
    if(EquipmentReader.IsValid())EquipmentReader->OnEquipmentMutationsChanged.AddUniqueDynamic(this,&ThisClass::HandleTradeBagChanged);
}
FRPGTradeBagSnapshot URPGTradeSubsystem::ReadRegistrationBag(){EnsureBagReader();return ReadTradeBag();}
bool URPGTradeSubsystem::ReloadRegistrationBag(){EnsureBagReader();return ReloadTradeBag();}
FRPGTradeBagSnapshot URPGTradeSubsystem::ReadTradeBag() { UpdateTradeBag(); return CachedBag; }
void URPGTradeSubsystem::HandleTradeBagChanged()
{
    if (bEnding) return;
    if (bBagWaiting) UpdateTradeBag();
    else if (CachedBag.bReady) { CachedBag={}; if (Owner) Owner->NotifyBagChanged(); }
}
void URPGTradeSubsystem::UpdateTradeBag()
{
    if (bEnding) return;
    const auto Scope=ReadContext();
    if (Scope.Endpoint!=CachedBag.Endpoint || Scope.Account!=CachedBag.Account || Scope.Character!=CachedBag.Character)
    {
        const bool HadState=bBagWaiting || CachedBag.bReady;
        CachedBag={}; bBagWaiting=false;
        if (HadState && Owner) Owner->NotifyBagChanged();
        return;
    }
    if (!bBagWaiting) return;
    auto* Reader=EquipmentReader.Get();
    if (!Reader) { bBagWaiting=false; CachedBag.bLoading=false; return; }
    const auto View=Reader->GetState(CachedBag.Character);
    if (View.ItemsLoadState==ERPGInventoryProjectionLoadState::Loading) bBagSawLoading=true;
    const bool ReadBusy=Reader->IsItemsReadInFlight();
    if (ReadBusy && FPlatformTime::Seconds()<BagReadDeadline) return;
    bBagWaiting=false; CachedBag.bLoading=false;
    const bool Blocked=View.State==ERPGEquipmentMutationState::AwaitingServer || View.State==ERPGEquipmentMutationState::RecoveryRequired
        || View.State==ERPGEquipmentMutationState::AuthenticationRequired || View.State==ERPGEquipmentMutationState::StorageBlocked
        || View.State==ERPGEquipmentMutationState::Unavailable;
    const auto* Player=GetLocalPlayer(); const auto* GI=Player ? Player->GetGameInstance() : nullptr;
    const auto* Data=GI ? GI->GetSubsystem<UDataManager>() : nullptr;
    if (!ReadBusy && bBagSawLoading && !Blocked && !Reader->IsRequestInFlight() && View.ItemsLoadState==ERPGInventoryProjectionLoadState::Ready
        && View.CharacterId==CachedBag.Character && View.Items.Num()<500 && Data)
    {
        bool Valid=true;
        for (const auto& Entry : View.Items)
        {
            if (!Entry.GetItemId().IsValid() || Entry.GetRevision()<=0 || Entry.GetQuantity()<=0) {Valid=false;break;}
            FRPGTradeBagItem Item; Item.ItemId=Entry.GetItemId(); Item.Revision=Entry.GetRevision(); Item.Quantity=Entry.GetQuantity();
            FRPGItemDefinitionView Definition;
            const bool Known=Data->GetItemDefinitionViewCatalog().TryFindDefinitionView(Entry.GetDefinitionId(),Definition)
                && Definition.IsValid() && Definition.Snapshot.DefinitionId==Entry.GetDefinitionId() && Definition.Snapshot.DefinitionVersion==Entry.GetDefinitionVersion();
            const bool NotExpired=Entry.GetExpiresAtUtc().GetTicks()==0 || Entry.GetExpiresAtUtc()>FDateTime::UtcNow();
            const auto Tab=RPGItemInventoryPolicy::CategoryForDefinition(Entry.GetDefinitionId());
            const bool CorrectSlot=RPGItemInventoryPolicy::Contains(Tab,Entry.GetSlotIndex());
            Item.bEligible=Known && CorrectSlot && Entry.GetContainerType()==ERPGItemContainerType::Inventory && !Entry.IsLocked() && NotExpired
                && Entry.GetRevision()!=MAX_int64 && (Entry.GetBindState()==ERPGItemBindState::Unbound || Entry.GetBindState()==ERPGItemBindState::BindOnEquip);
            const FText Category=Tab==EItemCategory::Equip ? NSLOCTEXT("RPGTrade","BagEquipment","장비")
                : Tab==EItemCategory::Consume ? NSLOCTEXT("RPGTrade","BagConsumables","소비") : NSLOCTEXT("RPGTrade","BagOther","기타");
            Item.Label=Known && CorrectSlot ? FText::Format(NSLOCTEXT("RPGTrade","BagRow","{0} · {1} {2}칸"),Definition.DisplayName,Category,FText::AsNumber(Entry.GetSlotIndex()-RPGItemInventoryPolicy::FirstSlot(Tab)+1))
                : NSLOCTEXT("RPGTrade","UnknownBagItem","정의를 확인할 수 없는 물품");
            CachedBag.Items.Add(MoveTemp(Item));
        }
        if (Valid) { CachedBag.bReady=true; CachedBag.Token=FGuid::NewGuid(); CachedBag.ValidUntil=FPlatformTime::Seconds()+30; }
        else CachedBag.Items.Reset();
    }
    if (Owner) Owner->NotifyBagChanged();
}
bool URPGTradeSubsystem::ResolveItem(const FRPGTradeItemDetails& Item, FText& Name, FText& Description) const
{
    auto* Player = GetLocalPlayer(); auto* GI = Player ? Player->GetGameInstance() : nullptr;
    const auto* Data = GI ? GI->GetSubsystem<UDataManager>() : nullptr;
    FRPGItemDefinitionView View;
    const FPrimaryAssetId Id(FPrimaryAssetType(FName(*Item.DefinitionType)),FName(*Item.DefinitionName));
    if (!Data || !Data->GetItemDefinitionViewCatalog().TryFindDefinitionView(Id,View) || !View.IsValid()
        || View.Snapshot.DefinitionId.PrimaryAssetType.ToString() != Item.DefinitionType || View.Snapshot.DefinitionId.PrimaryAssetName.ToString() != Item.DefinitionName
        || View.Snapshot.DefinitionVersion != Item.DefinitionVersion) return false;
    TArray<FText> Lines;
    if (!View.Description.IsEmpty()) Lines.Add(View.Description);
    for (const auto& Tag : Item.Tags)
    { const auto* Label = TradeTagLabels.Find(Tag); if (!Label || Label->IsEmpty()) return false; Lines.Add(*Label); }
    for (const auto& Stat : Item.Stats)
    {
        const auto* Label = TradeStatLabels.Find(Stat.Tag); if (!Label || Label->IsEmpty()) return false;
        Lines.Add(FText::Format(NSLOCTEXT("RPGTrade", "StatValue", "{0}: {1}"),*Label,FText::FromString(FString::Printf(TEXT("%.17g"),Stat.Value))));
    }
    if (Item.Growth.IsSet())
    {
        const auto* Band = TradeGrowthLabels.Find(Item.Growth->Band+TEXT(":")+LexToString(Item.Growth->RuleVersion));
        if (!Band || Band->IsEmpty()) return false;
        Lines.Add(FText::Format(NSLOCTEXT("RPGTrade", "Growth", "{0} +{1}"),*Band,FText::AsNumber(Item.Growth->Level)));
    }
    Lines.Add(Item.BindState == TEXT("BindOnEquip") ? NSLOCTEXT("RPGTrade", "BindOnEquip", "장착 시 귀속") : NSLOCTEXT("RPGTrade", "Unbound", "귀속 없음"));
    if (Item.MaximumDurability > 0) Lines.Add(FText::Format(NSLOCTEXT("RPGTrade", "Durability", "내구도 {0}/{1}"),FText::AsNumber(Item.Durability),FText::AsNumber(Item.MaximumDurability)));
    if (Item.ExpiresAt.IsSet()) Lines.Add(FText::Format(NSLOCTEXT("RPGTrade", "ItemExpiry", "사용 기한: {0}"),FText::AsDateTime(Item.ExpiresAt.GetValue())));
    Name = View.DisplayName; Description = FText::Join(FText::FromString(TEXT("\n")),Lines); return true;
}
