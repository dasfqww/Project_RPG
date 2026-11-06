#include "Riding/RPGRidingSessionComponent.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSession.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

void FRPGRidingSessionDeleter::operator()(FRPGRidingSession* Session) const { delete Session; }

URPGRidingSessionComponent::URPGRidingSessionComponent()
{
    SetIsReplicatedByDefault(true);
    Runtime.Reset(new FRPGRidingSession());
}
URPGRidingSessionComponent::~URPGRidingSessionComponent() = default;

void URPGRidingSessionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(ThisClass, OwnerState, COND_OwnerOnly);
}

bool URPGRidingSessionComponent::IsContextCurrent() const
{
    const auto* PlayerState = Cast<APlayerState>(GetOwner());
    const auto* PC = Cast<APlayerController>(BoundController.Get());
    return IsInGameThread() && IsValid(PlayerState) && PlayerState->HasAuthority()
        && !PlayerState->IsActorBeingDestroyed() && IsValid(GetWorld()) && !GetWorld()->bIsTearingDown
        && IsValid(PC) && !PC->IsActorBeingDestroyed() && PC->HasAuthority() && PC->GetWorld() == GetWorld()
        && PlayerState->GetOwner() == PC && PC->PlayerState == PlayerState && BoundGeneration > 0
        && ValidateAdmission && ValidateAdmission();
}

void URPGRidingSessionComponent::Invalidate(bool bRevoke)
{
    if (GetOwner() && !GetOwner()->HasAuthority()) return;
    if (bRevoke) Runtime->Revoke(); else Runtime->ClearIdentity();
    if (auto* Pawn = BoundPawn.Get()) Pawn->ReleaseBinding();
    BoundPawn.Reset(); BoundController.Reset(); BoundEpoch.Invalidate(); AuthorityContext.Invalidate();
    BoundCharacter.Reset(); BoundSteam.Reset(); BoundSession.Reset(); BoundGeneration = 0;
    PublishOwnerState();
}

void URPGRidingSessionComponent::UpdateAuthenticatedContext(const FString& CharacterId, const FString& SteamId,
    const FString& SessionId, int64 Generation, bool bRevoked, bool bAdmissionConfirmed)
{
    if (!IsInGameThread() || !GetOwner() || !GetOwner()->HasAuthority()) return;
    if (bRevoked) { Invalidate(true); return; }
    auto* PS = Cast<APlayerState>(GetOwner());
    auto* PC = PS ? Cast<APlayerController>(PS->GetOwner()) : nullptr;
    FGuid CharacterGuid, SessionGuid;
    if (!PC || PC->PlayerState != PS || PC->GetWorld() != GetWorld() || Generation <= 0 || SteamId.IsEmpty()
        || !FGuid::Parse(CharacterId, CharacterGuid) || !FGuid::Parse(SessionId, SessionGuid))
    { Invalidate(false); return; }
    if (PC != BoundController || CharacterId != BoundCharacter || SteamId != BoundSteam
        || SessionId != BoundSession || Generation != BoundGeneration)
    {
        Invalidate(false);
        AuthorityContext = FGuid::NewGuid();
        if (!Runtime->SetIdentity({AuthorityContext, CharacterGuid, SessionGuid, Generation}, FPlatformTime::Seconds())) return;
        BoundController = PC; BoundCharacter = CharacterId; BoundSteam = SteamId;
        BoundSession = SessionId; BoundGeneration = Generation;
    }
    if (bAdmissionConfirmed && IsContextCurrent() && Policy)
    {
        // Snapshot transport is deliberately absent until the authenticated Riding backend exists.
        // Admission alone cannot set Ready or fabricate ownership.
        Runtime->ConfirmAdmission({AuthorityContext, CharacterGuid, SessionGuid, Generation}, Policy->Values);
    }
    PublishOwnerState();
}

FGuid URPGRidingSessionComponent::Bind(URPGRidingComponent& PawnComponent)
{
    auto* Pawn = Cast<APawn>(PawnComponent.GetOwner());
    if (!IsContextCurrent() || !Pawn || Pawn->GetController() != BoundController || Pawn->GetPlayerState() != GetOwner()
        || Pawn->GetWorld() != GetWorld() || BoundController->GetPawn() != Pawn) return {};
    if (BoundPawn == &PawnComponent && BoundEpoch.IsValid()) return BoundEpoch;
    if (auto* Previous = BoundPawn.Get()) Previous->ReleaseBinding();
    BoundPawn = &PawnComponent;
    BoundEpoch = Runtime->BindPawn();
    return BoundEpoch;
}

void URPGRidingSessionComponent::Unbind(URPGRidingComponent& PawnComponent, const FGuid& Epoch)
{
    if (BoundPawn != &PawnComponent || Epoch != BoundEpoch) return;
    Runtime->UnbindPawn(Epoch);
    BoundPawn.Reset(); BoundEpoch.Invalidate();
}

void URPGRidingSessionComponent::PublishOwnerState()
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    const auto Timing = Runtime->GetTiming(FPlatformTime::Seconds());
    const double WorldNow = GetWorld() ? GetWorld()->GetTimeSeconds() : 0;
    OwnerState = {};
    OwnerState.bReady = IsContextCurrent() && Runtime->GetStatus() == ERPGRidingSessionStatus::Ready;
    if (Timing.bValid)
    {
        OwnerState.DashEndServerSeconds = WorldNow + Timing.DashRemaining;
        OwnerState.EmoteEndServerSeconds = WorldNow + Timing.EmoteRemaining;
        OwnerState.CombatEndServerSeconds = WorldNow + Timing.CombatRemaining;
    }
    MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, OwnerState, this);
    GetOwner()->ForceNetUpdate();
}

void URPGRidingSessionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    Invalidate(true);
    Super::EndPlay(Reason);
}
