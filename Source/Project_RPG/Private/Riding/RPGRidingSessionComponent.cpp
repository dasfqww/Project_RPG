#include "Riding/RPGRidingSessionComponent.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSession.h"
#include "Riding/RPGRidingSnapshot.h"
#include "Online/RPGDungeonConnectionRecovery.h"
#include "Interfaces/IHttpRequest.h"
#include "Containers/Ticker.h"
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
    CancelSnapshotWork(); SnapshotDispatches = 0; SnapshotExpiresAt = 0;
    if (bRevoke) Runtime->Revoke(); else Runtime->ClearIdentity();
    PendingDamageScopes.Reset();
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
        Runtime->ConfirmAdmission({AuthorityContext, CharacterGuid, SessionGuid, Generation}, Policy->Values);
        RequestAdmissionSnapshot();
    }
    PublishOwnerState();
}

void URPGRidingSessionComponent::CancelSnapshotWork()
{
    SnapshotNonce.Invalidate();
    if (SnapshotHttp) { SnapshotHttp->OnProcessRequestComplete().Unbind(); SnapshotHttp->CancelRequest(); SnapshotHttp.Reset(); }
    FTSTicker::RemoveTicker(SnapshotRetryTicker); SnapshotRetryTicker.Reset();
    FTSTicker::RemoveTicker(SnapshotExpiryTicker); SnapshotExpiryTicker.Reset();
}

void URPGRidingSessionComponent::RequestAdmissionSnapshot(bool bUserRequested)
{
    if (!IsContextCurrent()) return;
    if (SnapshotExpiresAt > 0 && FPlatformTime::Seconds() >= SnapshotExpiresAt)
    { ExpireAdmissionSnapshot(AuthorityContext); if (!IsContextCurrent()) return; }
    FRPGRidingPolicyValues Approved; if (!Runtime->CopyAdmissionPolicy(Approved)) return;
    auto* Host = RPGRidingSnapshot::ResolveHost(GetWorld());
    bool bTestFactory = false;
#if WITH_DEV_AUTOMATION_TESTS
    bTestFactory = static_cast<bool>(SnapshotFactoryForTests);
#endif
    if (!bTestFactory && (!Host || !Host->IsRecoveryServerRuntime() || !Host->IsRecoveryServerConfigured() || !Host->HasValidRecoveryServiceCredential())) return;
    const auto Ticket = Runtime->BeginSnapshot(FPlatformTime::Seconds(), bUserRequested);
    if (!Ticket.RequestId.IsValid()) return;
    FTSTicker::RemoveTicker(SnapshotRetryTicker); SnapshotRetryTicker.Reset();
    FRPGRidingSnapshotContext Context; Context.Ticket = Ticket; Context.Policy = Approved; Context.World = GetWorld();
    Context.ServerId = Host ? Host->GetRecoveryServerId() : FString();
#if WITH_DEV_AUTOMATION_TESTS
    if (bTestFactory) Context.ServerId = SnapshotServerForTests;
#endif
    Context.SentUtc = FDateTime::UtcNow(); Context.SentMonotonic = FPlatformTime::Seconds();
    const FGuid Nonce = SnapshotNonce = FGuid::NewGuid(); SnapshotDispatches = FMath::Min(SnapshotDispatches + 1, 4);
    auto Completion = [WeakThis = TWeakObjectPtr<URPGRidingSessionComponent>(this), Context, Nonce](const FRPGRidingSnapshotResponse& Response)
    { if (auto* Self = WeakThis.Get()) Self->CompleteAdmissionSnapshot(Context, Nonce, Response); };
#if WITH_DEV_AUTOMATION_TESTS
    if (bTestFactory) SnapshotHttp = SnapshotFactoryForTests(Context, MoveTemp(Completion));
    else
#endif
    SnapshotHttp = RPGRidingSnapshot::Create(*Host, Context, MoveTemp(Completion));
    if (SnapshotNonce != Nonce) return; // A synchronous test completion or teardown must not dispatch stale work.
    if (!SnapshotHttp || !SnapshotHttp->ProcessRequest())
    {
        if (SnapshotHttp) SnapshotHttp->OnProcessRequestComplete().Unbind();
        CompleteAdmissionSnapshot(Context, Nonce, {});
    }
}

void URPGRidingSessionComponent::CompleteAdmissionSnapshot(const FRPGRidingSnapshotContext& Context, const FGuid& Nonce, const FRPGRidingSnapshotResponse& Response)
{
    if (!IsInGameThread() || Nonce != SnapshotNonce || !Nonce.IsValid()) return;
    SnapshotNonce.Invalidate(); SnapshotHttp.Reset();
    if (GetWorld() != Context.World.Get() || AuthorityContext != Context.Ticket.Identity.AuthorityContext) return;
    if (!IsContextCurrent())
    { Runtime->FailSnapshot(Context.Ticket, FPlatformTime::Seconds()); PublishOwnerState(); return; }
    FRPGRidingDecodedSnapshot Decoded; const double Now = FPlatformTime::Seconds();
    if (Response.bScopeCurrent && Response.bTransportSuccessful && Response.StatusCode == 200
        && RPGRidingSnapshot::Decode(Response.Body, Context, FDateTime::UtcNow(), Now, Decoded)
        && Runtime->ApplySnapshot(Context.Ticket, Decoded.Snapshot, Now))
    {
        SnapshotExpiresAt = Now + Decoded.ValidForSeconds;
        const FGuid Expected = AuthorityContext;
        SnapshotExpiryTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
            [WeakThis = TWeakObjectPtr<URPGRidingSessionComponent>(this), Expected](float)
            {
                auto* Self = WeakThis.Get(); if (!Self || Expected != Self->AuthorityContext) return false;
                if (FPlatformTime::Seconds() < Self->SnapshotExpiresAt) return true;
                Self->SnapshotExpiryTicker.Reset(); Self->ExpireAdmissionSnapshot(Expected); return false;
            })); // Core ticks only check a monotonic deadline; world pause/dilation cannot extend ownership.
    }
    else if (Runtime->FailSnapshot(Context.Ticket, Now) && SnapshotDispatches < 4)
    {
        const FGuid Expected = AuthorityContext; const double RetryAt = Now + (1 << (SnapshotDispatches - 1));
        SnapshotRetryTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
            [WeakThis = TWeakObjectPtr<URPGRidingSessionComponent>(this), Expected, RetryAt](float)
            {
                auto* Self = WeakThis.Get(); if (!Self || Expected != Self->AuthorityContext) return false;
                if (FPlatformTime::Seconds() < RetryAt) return true;
                Self->SnapshotRetryTicker.Reset(); Self->RequestAdmissionSnapshot(); return false;
            }));
    }
    PublishOwnerState();
}

void URPGRidingSessionComponent::ExpireAdmissionSnapshot(FGuid ExpectedContext)
{
    if (!IsContextCurrent() || ExpectedContext != AuthorityContext) return;
    if (Runtime->ExpireSnapshot(FPlatformTime::Seconds()))
    {
        FTSTicker::RemoveTicker(SnapshotExpiryTicker); SnapshotExpiryTicker.Reset();
        SnapshotExpiresAt = 0; SnapshotDispatches = 4;
        if (auto* Pawn = BoundPawn.Get()) Pawn->ForceDismount(ERPGRidingEndReason::ContextLost);
        PublishOwnerState();
    }
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
