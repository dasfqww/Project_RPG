#include "Narrative/RPGNarrativeProgressComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Manager/HttpWebManager.h"
#include "Narrative/RPGNarrativeCoordinator.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Player/RPGPlayerState.h"

URPGNarrativeProgressComponent::URPGNarrativeProgressComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = true; PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickInterval = 0.25f;
}
void URPGNarrativeProgressComponent::NotifyCommitted(const FGuid Character, const FGuid Session)
{
    if (GetOwner() && GetOwner()->HasAuthority() && Character.IsValid() && Session.IsValid()) ClientCommitted(Character, Session);
}
bool URPGNarrativeProgressComponent::ContextMatches() const
{
    const auto* Controller = Cast<APlayerController>(GetOwner());
    const auto* State = Controller ? Controller->GetPlayerState<ARPGPlayerState>() : nullptr;
    auto* Game = Controller ? Controller->GetGameInstance() : nullptr;
    const auto* Web = Game ? Game->GetSubsystem<UHttpWebManager>() : nullptr;
    const auto* Dungeon = Game ? Game->GetSubsystem<URPGDungeonSessionSubsystem>() : nullptr;
    FGuid Character, Session, Selected;
    return Controller && Controller->IsLocalController() && Controller->GetLocalPlayer() && State && State->HasAuthenticatedCharacter()
        && Web && Dungeon && Web->GetApiUrl() == Endpoint && Web->GetAuthenticatedSteamId() == Steam
        && State->GetAuthenticatedSteamId() == Steam && FGuid::Parse(State->GetBackendCharacterId(), Character) && Character == CharacterId
        && FGuid::Parse(State->GetBackendDungeonSessionId(), Session) && Session == SessionId
        && FGuid::Parse(Dungeon->GetSelectedCharacterId(), Selected) && Selected == CharacterId;
}
void URPGNarrativeProgressComponent::ClientCommitted_Implementation(const FGuid Character, const FGuid Session)
{
    const auto* Controller = Cast<APlayerController>(GetOwner());
    auto* Game = Controller ? Controller->GetGameInstance() : nullptr;
    const auto* Web = Game ? Game->GetSubsystem<UHttpWebManager>() : nullptr;
    if (!Web || !Character.IsValid() || !Session.IsValid()) return;
    const FGuid PreviousCharacter = CharacterId, PreviousSession = SessionId;
    const FString PreviousSteam = Steam, PreviousEndpoint = Endpoint;
    CharacterId = Character; SessionId = Session; Steam = Web->GetAuthenticatedSteamId(); Endpoint = Web->GetApiUrl();
    if (ContextMatches()) SetComponentTickEnabled(true);
    else { CharacterId = PreviousCharacter; SessionId = PreviousSession; Steam = PreviousSteam; Endpoint = PreviousEndpoint; }
}
void URPGNarrativeProgressComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime, TickType, Function);
    if (!ContextMatches()) { SetComponentTickEnabled(false); return; }
    const auto* Controller = Cast<APlayerController>(GetOwner());
    if (auto* Flow = Controller->GetLocalPlayer()->GetSubsystem<URPGNarrativeCoordinator>(); Flow && !Flow->IsBusy()
        && !Flow->HasPendingChoice() && FPlatformTime::Seconds() >= NextRefresh)
        if ((Flow->GetSelectedCharacterId() == CharacterId || Flow->SelectCharacter(CharacterId)) && Flow->Refresh())
        { NextRefresh = FPlatformTime::Seconds() + 0.5; SetComponentTickEnabled(false); }
}
