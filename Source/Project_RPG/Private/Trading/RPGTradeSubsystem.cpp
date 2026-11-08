#include "Trading/RPGTradeSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Manager/HttpWebManager.h"
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
