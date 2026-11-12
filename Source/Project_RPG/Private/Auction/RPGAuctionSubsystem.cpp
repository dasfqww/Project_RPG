#include "Auction/RPGAuctionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Manager/HttpWebManager.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "Misc/Paths.h"

void URPGAuctionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection); bEnding = false;
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickAuction), .25f);
}
void URPGAuctionSubsystem::Deinitialize()
{
    bEnding = true; FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); TickHandle.Reset();
    if (Coordinator) Coordinator->Shutdown(); Coordinator.Reset(); Super::Deinitialize();
    EquipmentReader.Reset();
}
FRPGAuctionLobbyContext URPGAuctionSubsystem::ReadContext() const
{
    FRPGAuctionLobbyContext Out;
    auto* Player = GetLocalPlayer(); auto* GI = Player ? Player->GetGameInstance() : nullptr;
    auto* Web = GI ? GI->GetSubsystem<UHttpWebManager>() : nullptr;
    if (bEnding || !GI || GI->GetFirstGamePlayer() != Player || !Web || IsRunningDedicatedServer() || Web->IsConfiguredForDungeonServer()) return Out;
    Out.Endpoint = Web->GetApiUrl();
    Out.Account = FRPGBackendPlayerAuthState::IsSteamId(Web->GetAuthenticatedSteamId()) ? Web->GetAuthenticatedSteamId() : FString();
    Out.bEnabled = bAuctionsEnabled; Out.bAuthenticated = Web->HasAccessToken();
    auto* Flow = GI->GetSubsystem<URPGDungeonSessionSubsystem>();
    if (Flow) FGuid::Parse(Flow->GetSelectedCharacterId(), Out.Character);
    auto* Lobby = GI->GetSubsystem<URPGOnlineLobbySubsystem>();
    auto* PC = GI->GetWorld() ? Player->GetPlayerController(GI->GetWorld()) : nullptr;
    Out.bLocalLobby = Lobby && Lobby->IsLobbyController(PC);
    auto* Widget = Lobby ? Lobby->GetLobbyWidget() : nullptr;
    Out.bEligible = Out.bLocalLobby && Widget && Widget->GetOwningLocalPlayer() == Player && Widget->CanUseTrading();
    return Out;
}
TSharedPtr<FRPGAuctionCoordinator> URPGAuctionSubsystem::GetCoordinator()
{
    if (bEnding) return nullptr;
    if (!Coordinator)
    {
        auto* Player = GetLocalPlayer(); auto* GI = Player ? Player->GetGameInstance() : nullptr;
        auto* Web = GI ? GI->GetSubsystem<UHttpWebManager>() : nullptr;
        if (!GI || GI->GetFirstGamePlayer() != Player || !Web || IsRunningDedicatedServer() || Web->IsConfiguredForDungeonServer()) return nullptr;
        const TWeakObjectPtr<ThisClass> Weak(this);
        Coordinator = MakeShared<FRPGAuctionCoordinator>(Web->CreatePlayerAuctionTransport(), [Weak]
            { const auto* Self = Weak.Get(); return Self ? Self->ReadContext() : FRPGAuctionLobbyContext{}; },
            FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AuctionIntents/v1")));
        EquipmentReader=GI->GetSubsystem<URPGEquipmentMutationSubsystem>();
        Coordinator->SetAssetSink([Weak]
        {auto* S=Weak.Get();return S && !S->bEnding && S->EquipmentReader.IsValid()
            && S->EquipmentReader->BeginExternalRead(S->ReadContext().Character,S->AssetTicket);},
        [Weak](const FRPGTradeAssets& Assets)
        {auto* S=Weak.Get();return S && !S->bEnding && S->EquipmentReader.IsValid()
            && S->EquipmentReader->ApplyExternalRead(S->AssetTicket,Assets.Items,Assets.Mail,Assets.Wallet);},
        [Weak]
        {auto* S=Weak.Get();return S && !S->bEnding && S->EquipmentReader.IsValid()
            && S->EquipmentReader->IsExternalReadCurrent(S->AssetTicket);});
    }
    return Coordinator;
}
bool URPGAuctionSubsystem::TickAuction(float)
{ if (bEnding) return false; if (const auto Owner = GetCoordinator()) Owner->Tick(); return true; }
bool URPGAuctionSubsystem::BlocksTransitions()
{ const auto Owner = GetCoordinator(); return Owner && Owner->BlocksTransitions(); }
