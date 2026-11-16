#include "Shop/RPGShopSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Manager/HttpWebManager.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "Misc/Paths.h"

void URPGShopSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{Super::Initialize(Collection);bEnding=false;TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&ThisClass::TickShop),.25f);}
void URPGShopSubsystem::Deinitialize()
{bEnding=true;FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);TickHandle.Reset();if(Coordinator)Coordinator->Shutdown();Coordinator.Reset();Equipment.Reset();Super::Deinitialize();}
FRPGShopContext URPGShopSubsystem::ReadContext() const
{
    FRPGShopContext Out;auto* Player=GetLocalPlayer();auto* GI=Player?Player->GetGameInstance():nullptr;auto* Web=GI?GI->GetSubsystem<UHttpWebManager>():nullptr;
    if(bEnding||!GI||GI->GetFirstGamePlayer()!=Player||!Web||IsRunningDedicatedServer()||Web->IsConfiguredForDungeonServer())return Out;
    Out.Endpoint=Web->GetApiUrl();Out.Account=FRPGBackendPlayerAuthState::IsSteamId(Web->GetAuthenticatedSteamId())?Web->GetAuthenticatedSteamId():FString();Out.bAuthenticated=Web->HasAccessToken();
    auto* Flow=GI->GetSubsystem<URPGDungeonSessionSubsystem>();if(Flow)FGuid::Parse(Flow->GetSelectedCharacterId(),Out.Character);
    auto* Lobby=GI->GetSubsystem<URPGOnlineLobbySubsystem>();auto* PC=GI->GetWorld()?Player->GetPlayerController(GI->GetWorld()):nullptr;
    Out.bLocalLobby=Lobby&&Lobby->IsLobbyController(PC);auto* Widget=Lobby?Lobby->GetLobbyWidget():nullptr;
    Out.bEligible=Out.bLocalLobby&&Widget&&Widget->GetOwningLocalPlayer()==Player&&Widget->CanUseTrading();return Out;
}
TSharedPtr<FRPGShopCoordinator> URPGShopSubsystem::GetCoordinator()
{
    if(bEnding)return nullptr;
    if(!Coordinator)
    {
        auto* Player=GetLocalPlayer();auto* GI=Player?Player->GetGameInstance():nullptr;auto* Web=GI?GI->GetSubsystem<UHttpWebManager>():nullptr;
        if(!GI||GI->GetFirstGamePlayer()!=Player||!Web||IsRunningDedicatedServer()||Web->IsConfiguredForDungeonServer())return nullptr;
        const TWeakObjectPtr<ThisClass> Weak(this);
        Coordinator=MakeShared<FRPGShopCoordinator>(Web->CreatePlayerShopTransport(),[Weak]{const auto* S=Weak.Get();return S?S->ReadContext():FRPGShopContext{};},FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("ShopIntents/v1")));
        Equipment=GI->GetSubsystem<URPGEquipmentMutationSubsystem>();
        Coordinator->SetAssetSink([Weak]{auto* S=Weak.Get();return S&&!S->bEnding&&S->Equipment.IsValid()&&S->Equipment->BeginExternalRead(S->ReadContext().Character,S->AssetTicket);},
            [Weak](const FRPGTradeAssets& Assets){auto* S=Weak.Get();return S&&!S->bEnding&&S->Equipment.IsValid()&&S->Equipment->ApplyExternalRead(S->AssetTicket,Assets.Items,Assets.Mail,Assets.Wallet);},
            [Weak]{auto* S=Weak.Get();return S&&!S->bEnding&&S->Equipment.IsValid()&&S->Equipment->IsExternalReadCurrent(S->AssetTicket);});
    }
    return Coordinator;
}
bool URPGShopSubsystem::TickShop(float){if(bEnding)return false;if(const auto Owner=GetCoordinator())Owner->Tick();return true;}
bool URPGShopSubsystem::BlocksTransitions(){const auto Owner=GetCoordinator();return Owner&&Owner->BlocksTransitions();}
