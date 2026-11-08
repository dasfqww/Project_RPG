#include "Narrative/RPGNarrativeInvestigationComponent.h"
#include "Actor/Narrative/RPGNarrativeInvestigation.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBaseCharacter.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameMode/RPGGameModeBase.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IHttpRequest.h"
#include "Manager/HttpWebManager.h"
#include "Narrative/RPGNarrativeCoordinator.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Player/RPGPlayerState.h"

URPGNarrativeInvestigationComponent::URPGNarrativeInvestigationComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickInterval = 0.25f;
}
void URPGNarrativeInvestigationComponent::Status(const FText& Text, const bool bRetry)
{
    if (auto* Target = LocalTarget.Get()) Target->SetLocalStatus(Text, bRetry);
    OnStatus.Broadcast(Text, bRetry);
}
bool URPGNarrativeInvestigationComponent::ClientContextMatches() const
{
    const auto* Controller = Cast<APlayerController>(GetOwner());
    const auto* State = Controller ? Controller->GetPlayerState<ARPGPlayerState>() : nullptr;
    auto* Game = Controller ? Controller->GetGameInstance() : nullptr;
    const auto* Web = Game ? Game->GetSubsystem<UHttpWebManager>() : nullptr;
    const auto* Dungeon = Game ? Game->GetSubsystem<URPGDungeonSessionSubsystem>() : nullptr;
    FGuid Character, Session, Selected;
    return Controller && Controller->IsLocalController() && Controller->GetLocalPlayer() && State && State->HasAuthenticatedCharacter()
        && Web && Web->GetApiUrl() == LocalEndpoint && Web->GetAuthenticatedSteamId() == LocalSteam
        && LocalSteam == State->GetAuthenticatedSteamId() && Dungeon
        && FGuid::Parse(State->GetBackendCharacterId(), Character) && Character == LocalCharacter
        && FGuid::Parse(State->GetBackendDungeonSessionId(), Session) && Session == LocalSession
        && FGuid::Parse(Dungeon->GetSelectedCharacterId(), Selected) && Selected == LocalCharacter;
}
bool URPGNarrativeInvestigationComponent::Investigate(ARPGNarrativeInvestigation* Target)
{
    auto* Controller = Cast<APlayerController>(GetOwner());
    if (!Controller || !Controller->IsLocalController() || !IsValid(Target) || Target->GetWorld() != GetWorld()) return false;
    if (LocalAction.IsValid()) return LocalTarget == Target && RetryPendingInvestigation();
    auto* State = Controller->GetPlayerState<ARPGPlayerState>();
    auto* Game = Controller->GetGameInstance(); auto* Web = Game ? Game->GetSubsystem<UHttpWebManager>() : nullptr;
    if (!State || !Web || !FGuid::Parse(State->GetBackendCharacterId(), LocalCharacter)
        || !FGuid::Parse(State->GetBackendDungeonSessionId(), LocalSession)
        || !Web->GetPlayerEquipmentContext(LocalEndpoint, LocalSteam) || !ClientContextMatches()) return false;
    if (auto* Previous = LocalTarget.Get()) Previous->SetLocalStatus(FText::GetEmpty(), false);
    LocalTarget = Target; LocalAction = FGuid::NewGuid(); NextClientRetry = FPlatformTime::Seconds() + 1;
    SetComponentTickEnabled(true);
    const FGuid Action = LocalAction;
    Status(NSLOCTEXT("RPGNarrative", "Investigating", "조사 결과 확인 중"), false);
    if (LocalAction == Action && ClientContextMatches()) ServerInvestigate(Target, Action);
    return true;
}
bool URPGNarrativeInvestigationComponent::RetryPendingInvestigation()
{
    const double Now = FPlatformTime::Seconds();
    if (!LocalAction.IsValid() || !ClientContextMatches() || Now < NextClientRetry) return false;
    NextClientRetry = Now + 1;
    // The server retains the approved target and observation even when the world actor has disappeared.
    ServerInvestigate(LocalTarget.Get(), LocalAction); return true;
}
bool URPGNarrativeInvestigationComponent::ServerContextMatches() const
{
    const auto* Command = Attempt.GetCommand(); const auto* State = ApprovedState.Get();
    const auto* Controller = Cast<APlayerController>(GetOwner());
    const auto* Game = Controller ? Controller->GetGameInstance() : nullptr;
    const auto* Web = Game ? Game->GetSubsystem<UHttpWebManager>() : nullptr;
    FGuid Character, Session;
    return GetOwner() && GetOwner()->HasAuthority() && Command && State && Controller
        && Controller->GetPlayerState<ARPGPlayerState>() == State && State->HasAuthenticatedCharacter()
        && State->GetAuthenticatedSteamId() == ApprovedSteam && State->GetBackendConnectionGeneration() == Command->ConnectionGeneration
        && FGuid::Parse(State->GetBackendCharacterId(), Character) && Character == Command->CharacterId
        && FGuid::Parse(State->GetBackendDungeonSessionId(), Session) && Session == Command->SessionId
        && Web && Web->IsConfiguredForDungeonServer() && Web->GetGameServerId() == ApprovedServer && Web->GetApiUrl() == ApprovedEndpoint
        && FGuid::Parse(Web->GetConfiguredDungeonSessionId(), Session) && Session == Command->SessionId;
}
void URPGNarrativeInvestigationComponent::ServerInvestigate_Implementation(ARPGNarrativeInvestigation* Target, const FGuid ActionId)
{
    auto* Controller = Cast<APlayerController>(GetOwner());
    auto* State = Controller ? Controller->GetPlayerState<ARPGPlayerState>() : nullptr;
    if (!GetOwner() || !GetOwner()->HasAuthority() || !ActionId.IsValid() || !State) return;
    FGuid Character, Session;
    FGuid::Parse(State->GetBackendCharacterId(), Character); FGuid::Parse(State->GetBackendDungeonSessionId(), Session);
    for (const auto& Completed : CompletedActions)
        if (Completed.Action == ActionId)
        {
            if (State->HasAuthenticatedCharacter() && Completed.Character == Character && Completed.Session == Session)
                ClientInvestigationResult(ActionId, Character, Session, Completed.bSuccess, false);
            return;
        }
    if (Attempt.IsPending())
    {
        if (ServerAction == ActionId && ServerContextMatches())
        {
            // Repeated client input can only retry the existing server event, subject to the same backoff.
            if (!ActiveRequest && FPlatformTime::Seconds() >= NextRetry) { Attempt.AllowManualRetry(); Dispatch(); }
        }
        else ClientInvestigationResult(ActionId, Character, Session, false, true);
        return;
    }
    auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ARPGGameModeBase>() : nullptr;
    const auto* Pawn = Cast<ARPGBaseCharacter>(Controller->GetPawn());
    const auto* Attributes = Pawn ? Pawn->GetRPGAttributeSet() : nullptr;
    auto* Game = Controller->GetGameInstance(); auto* Web = Game ? Game->GetSubsystem<UHttpWebManager>() : nullptr;
    const double Now = FPlatformTime::Seconds();
    if (Now < NextApproval) { ClientInvestigationResult(ActionId, Character, Session, false, false); return; }
    NextApproval = Now + 0.75;
    if (!State->HasAuthenticatedCharacter() || !Mode || !Mode->HasConfirmedDungeonAdmission(State)
        || !Pawn || Pawn->IsActorBeingDestroyed() || Pawn->GetPlayerState() != State || !Attributes
        || !FMath::IsFinite(Attributes->GetCurrentHealth()) || Attributes->GetCurrentHealth() <= 0
        || !IsValid(Target) || !Target->HasAuthority() || !Target->CanObserve(Controller) || !Web)
    { ClientInvestigationResult(ActionId, Character, Session, false, false); return; }
    FRPGNarrativeInteractionCommand Command;
    Command.EventId = FGuid::NewGuid(); Command.CharacterId = Character; Command.SessionId = Session;
    Command.ConnectionGeneration = State->GetBackendConnectionGeneration(); Command.TargetId = Target->GetTargetId();
    Command.ObservedAt = FDateTime::UtcNow();
    if (!Attempt.Begin(Command)) { ClientInvestigationResult(ActionId, Character, Session, false, false); return; }
    ApprovedState = State; ApprovedSteam = State->GetAuthenticatedSteamId(); ApprovedServer = Web->GetGameServerId(); ApprovedEndpoint = Web->GetApiUrl();
    ServerAction = ActionId; ServerTarget = Target;
    if (!ServerContextMatches()) { Attempt.Reset(); ClientInvestigationResult(ActionId, Character, Session, false, false); return; }
    SetComponentTickEnabled(true); Dispatch();
}
void URPGNarrativeInvestigationComponent::Dispatch()
{
    if (ActiveRequest || !ServerContextMatches() || !Attempt.StartRequest()) return;
    const uint64 Generation = ++RequestGeneration;
    auto* Web = GetOwner()->GetGameInstance()->GetSubsystem<UHttpWebManager>();
    const TWeakObjectPtr<URPGNarrativeInvestigationComponent> WeakThis(this);
    ActiveRequest = Web->CreateServerNarrativeInteractionRequest(*Attempt.GetCommand(), Attempt.GetBody(),
        [WeakThis, Generation](FRPGNarrativeInteractionHttpResult Result)
        { if (auto* Self = WeakThis.Get()) Self->Receive(Generation, MoveTemp(Result)); });
    if (!ActiveRequest || !ActiveRequest->ProcessRequest()) Receive(Generation, {});
}
void URPGNarrativeInvestigationComponent::Receive(const uint64 Generation, FRPGNarrativeInteractionHttpResult Result)
{
    if (Generation != RequestGeneration || !Attempt.IsPending()) return;
    ActiveRequest.Reset();
    const auto Command = *Attempt.GetCommand(); const FGuid Action = ServerAction;
    Result.bCurrentServerContext &= ServerContextMatches();
    const auto Outcome = Attempt.Receive(Result);
    const bool bSuccess = Outcome == ERPGNarrativeInteractionOutcome::Succeeded;
    NextRetry = FPlatformTime::Seconds() + Attempt.RetryDelay();
    if (Outcome != ERPGNarrativeInteractionOutcome::Unknown)
    {
        if (CompletedActions.Num() >= 16) CompletedActions.RemoveAt(0);
        CompletedActions.Add({Action, Command.CharacterId, Command.SessionId, bSuccess});
        ApprovedState.Reset(); ServerTarget.Reset(); ServerAction.Invalidate();
    }
    ClientInvestigationResult(Action, Command.CharacterId, Command.SessionId, bSuccess, Outcome == ERPGNarrativeInteractionOutcome::Unknown);
}
void URPGNarrativeInvestigationComponent::ClientInvestigationResult_Implementation(const FGuid ActionId, const FGuid CharacterId,
    const FGuid SessionId, const bool bSuccess, const bool bRetryable)
{
    if (LocalAction != ActionId || LocalCharacter != CharacterId || LocalSession != SessionId || !ClientContextMatches()) return;
    if (bSuccess)
    {
        LocalAction.Invalidate(); bRefreshNeeded = true;
        Status(NSLOCTEXT("RPGNarrative", "InvestigationComplete", "조사 완료"), false);
    }
    else if (bRetryable) Status(NSLOCTEXT("RPGNarrative", "InvestigationUnknown", "진행 결과를 다시 확인해 주세요"), true);
    else
    {
        LocalAction.Invalidate(); Status(NSLOCTEXT("RPGNarrative", "InvestigationRejected", "지금은 조사할 수 없습니다"), false);
    }
}
void URPGNarrativeInvestigationComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime, TickType, Function);
    if (Attempt.IsPending() && !ServerContextMatches())
    {
        // A new identity/generation never inherits the old observation. Its persisted outcome, if
        // any, is recovered by the original character's GET; cancellation cannot undo that write.
        ++RequestGeneration;
        if (ActiveRequest) { ActiveRequest->OnProcessRequestComplete().Unbind(); ActiveRequest->CancelRequest(); ActiveRequest.Reset(); }
        const auto Command = *Attempt.GetCommand();
        if (CompletedActions.Num() >= 16) CompletedActions.RemoveAt(0);
        CompletedActions.Add({ServerAction, Command.CharacterId, Command.SessionId, false});
        Attempt.Reset(); ApprovedState.Reset(); ServerTarget.Reset(); ServerAction.Invalidate();
    }
    if (LocalAction.IsValid() && !ClientContextMatches())
    {
        LocalAction.Invalidate();
        if (auto* Target = LocalTarget.Get()) Target->SetLocalStatus(FText::GetEmpty(), false);
        LocalTarget.Reset();
    }
    if (Attempt.CanAutoRetry() && !ActiveRequest && FPlatformTime::Seconds() >= NextRetry && ServerContextMatches()) Dispatch();
    if (bRefreshNeeded)
    {
        if (!ClientContextMatches()) bRefreshNeeded = false;
        else if (auto* Controller = Cast<APlayerController>(GetOwner()))
            if (auto* Player = Controller->GetLocalPlayer())
                if (auto* Flow = Player->GetSubsystem<URPGNarrativeCoordinator>(); Flow && !Flow->IsBusy() && !Flow->HasPendingChoice())
                    if ((Flow->GetSelectedCharacterId() == LocalCharacter || Flow->SelectCharacter(LocalCharacter)) && Flow->Refresh()) bRefreshNeeded = false;
    }
    SetComponentTickEnabled(Attempt.IsPending() || LocalAction.IsValid() || bRefreshNeeded);
}
void URPGNarrativeInvestigationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ++RequestGeneration;
    if (ActiveRequest) { ActiveRequest->OnProcessRequestComplete().Unbind(); ActiveRequest->CancelRequest(); ActiveRequest.Reset(); }
    Attempt.Reset();
    if (auto* Target = LocalTarget.Get()) Target->SetLocalStatus(FText::GetEmpty(), false);
    Super::EndPlay(Reason);
}
