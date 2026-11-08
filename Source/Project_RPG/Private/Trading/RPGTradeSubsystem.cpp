#include "Trading/RPGTradeSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Manager/HttpWebManager.h"
#include "Manager/DataManager.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"

void URPGTradeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection); bEnding = false;
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickTrade), .25f);
}
void URPGTradeSubsystem::Deinitialize()
{
    bEnding = true; FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); TickHandle.Reset();
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
            { auto* Self = WeakThis.Get(); return Self ? Self->ReadContext() : FRPGTradeLobbyContext{}; });
        Owner->SetDefinitionResolver([WeakThis](const FRPGTradeItemDetails& Item, FText& Name, FText& Description)
            { const auto* Self = WeakThis.Get(); return Self && Self->ResolveItem(Item,Name,Description); });
    }
    return Owner;
}
bool URPGTradeSubsystem::TickTrade(float)
{
    if (bEnding) return false;
    if (auto Current = GetTradeOwner()) Current->Tick();
    return true;
}
bool URPGTradeSubsystem::IsAvailable() { const auto Current = GetTradeOwner(); return Current && Current->IsAvailable(); }
bool URPGTradeSubsystem::BlocksTransitions() { const auto Current = GetTradeOwner(); return Current && Current->BlocksTransitions(); }
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
