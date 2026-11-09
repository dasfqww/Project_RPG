#include "Component/RPGPlayerMovementComponent.h"

#include "GameFramework/Character.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#include "Riding/RPGRidingComponent.h"
#include "Component/RPGSecurityValidationComponent.h"

URPGPlayerMovementComponent::URPGPlayerMovementComponent()
{
    SetNetworkMoveDataContainer(RidingMoves);
    SetMoveResponseDataContainer(RidingResponse);
}

bool URPGPlayerMovementComponent::MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& NewRotation,
    bool bSweep, FHitResult* OutHit, ETeleportType Teleport)
{
    if (IsRevokedOnServer()) { if (OutHit) *OutHit = FHitResult(); return false; }
    const FVector OriginalStart = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
    auto* OriginalRiding = CharacterOwner ? CharacterOwner->FindComponentByClass<URPGRidingComponent>() : nullptr;
    const FGuid OriginalExecution = OriginalRiding ? OriginalRiding->GetRidingState().ExecutionId : FGuid();
    FVector AllowedDelta = Delta;
    if (CharacterOwner && CharacterOwner->HasAuthority() && UpdatedComponent)
        if (auto* Riding = CharacterOwner->FindComponentByClass<URPGRidingComponent>())
        {
            const TWeakObjectPtr<USceneComponent> OriginalComponent = UpdatedComponent;
            const auto* OriginalController = CharacterOwner->GetController();
            const FGuid OriginalEpoch = Riding->GetRidingState().Key.RiderEpoch;
            const FVector Start = UpdatedComponent->GetComponentLocation();
            const double Fraction = Riding->ConstrainServerMovement(Start, Start + Delta);
            // Region dismount publishes callbacks: do not apply old input/rotation to a revoked or replaced context.
            if (IsRevokedOnServer() || !IsValid(CharacterOwner) || !IsValid(Riding) || !OriginalComponent.IsValid()
                || UpdatedComponent != OriginalComponent.Get() || CharacterOwner->GetController() != OriginalController
                || Riding->GetRidingState().Key.RiderEpoch != OriginalEpoch
                || !UpdatedComponent->GetComponentLocation().Equals(Start, 0.001))
            { if (OutHit) *OutHit = FHitResult(); return false; }
            AllowedDelta = Delta * Fraction;
            if (bEvaluatingRidingMove && (!bHasRidingStamp || !(EvaluatedStamp == FRPGRidingMoveStamp::FromState(Riding->GetRidingState()))))
                GetPredictionData_Server_Character()->bForceClientUpdate = true;
        }
    const bool Moved = Super::MoveUpdatedComponentImpl(AllowedDelta, NewRotation, bSweep, OutHit, Teleport);
    if (IsValid(OriginalRiding) && UpdatedComponent) OriginalRiding->RecordServerDashMovement(OriginalExecution, OriginalStart, UpdatedComponent->GetComponentLocation());
    return Moved;
}

bool URPGPlayerMovementComponent::CanOpenRidingDashWindow() const
{
    auto* S = CharacterOwner ? CharacterOwner->FindComponentByClass<URPGSecurityValidationComponent>() : nullptr;
    FString Rejection;
    return CharacterOwner && CharacterOwner->HasAuthority() && !IsRevokedOnServer() && S
        && S->CanPerformProtectedAction(TEXT("RidingDash"), false, Rejection) && !S->IsMovementAuthorizationActive();
}
FRPGRidingDashWindow URPGPlayerMovementComponent::OpenRidingDashWindow(float Duration, float Distance)
{
    if (!CanOpenRidingDashWindow()) return {};
    auto* S = CharacterOwner->FindComponentByClass<URPGSecurityValidationComponent>();
    const auto Handle = S->BeginMovementAuthorization(Duration, Distance, TEXT("RPG.RidingDash"));
    return {S, Handle, [Weak = TWeakObjectPtr<URPGSecurityValidationComponent>(S), Handle]()
        { if (auto* Original = Weak.Get()) Original->EndMovementAuthorization(Handle, true); }};
}
FVector URPGPlayerMovementComponent::ConstrainInputAcceleration(const FVector& Input) const
{
    const auto* R = CharacterOwner ? CharacterOwner->FindComponentByClass<URPGRidingComponent>() : nullptr;
    return R && R->IsServerDashActive() ? FVector::ZeroVector : Super::ConstrainInputAcceleration(Input);
}

bool URPGPlayerMovementComponent::TryGetRidingProfile(FRPGRidingMovementProfile& Out) const
{
    Out = {};
    if (IsRevokedOnServer()) return false;
    const auto* Riding = CharacterOwner ? CharacterOwner->FindComponentByClass<URPGRidingComponent>() : nullptr;
    return Riding && Riding->TryBuildMovementProfile(Out);
}

FNetworkPredictionData_Client* URPGPlayerMovementComponent::GetPredictionData_Client() const
{
    if (!ClientPredictionData)
        const_cast<URPGPlayerMovementComponent*>(this)->ClientPredictionData = new FPredictionData_Client_RPGRiding(*this);
    return ClientPredictionData;
}

bool URPGPlayerMovementComponent::CanCrouchInCurrentState() const
{
    FRPGRidingMovementProfile Profile;
    return !TryGetRidingProfile(Profile) && Super::CanCrouchInCurrentState();
}

bool URPGPlayerMovementComponent::IsRevokedOnServer() const
{
	return CharacterOwner && CharacterOwner->HasAuthority() && IsDungeonGameplayRevoked(CharacterOwner);
}

void URPGPlayerMovementComponent::StopRevokedMovement()
{
	if (!IsRevokedOnServer()) return;
	StopMovementImmediately();
	ClearAccumulatedForces();
	CurrentRootMotion.Clear();
	RootMotionParams.Clear();
	CharacterOwner->ConsumeMovementInputVector();
	Super::SetMovementMode(MOVE_None);
}

void URPGPlayerMovementComponent::SetMovementMode(EMovementMode NewMovementMode, uint8 NewCustomMode)
{
	Super::SetMovementMode(IsRevokedOnServer() ? MOVE_None : NewMovementMode,
		IsRevokedOnServer() ? 0 : NewCustomMode);
    if (CharacterOwner && CharacterOwner->HasAuthority())
        if (auto* Riding = CharacterOwner->FindComponentByClass<URPGRidingComponent>())
        {
            Riding->ValidateServerMovementContext();
            if (bEvaluatingRidingMove && (!bHasRidingStamp
                || !(EvaluatedStamp == FRPGRidingMoveStamp::FromState(Riding->GetRidingState()))))
                GetPredictionData_Server_Character()->bForceClientUpdate = true;
        }
}

void URPGPlayerMovementComponent::PerformMovement(float DeltaTime)
{
	if (IsRevokedOnServer()) { StopRevokedMovement(); return; }
    if (auto* Riding = CharacterOwner ? CharacterOwner->FindComponentByClass<URPGRidingComponent>() : nullptr) Riding->ValidateServerMovementContext();
    if (auto* Riding = CharacterOwner ? CharacterOwner->FindComponentByClass<URPGRidingComponent>() : nullptr) Riding->PrepareServerDashMovement();
	Super::PerformMovement(DeltaTime);
    if (auto* Riding = CharacterOwner ? CharacterOwner->FindComponentByClass<URPGRidingComponent>() : nullptr) Riding->FinalizeServerDashMovement();
}

void URPGPlayerMovementComponent::ServerMove_HandleMoveData(const FCharacterNetworkMoveDataContainer& MoveDataContainer)
{
	if (IsRevokedOnServer()) { StopRevokedMovement(); return; }
	Super::ServerMove_HandleMoveData(MoveDataContainer);
}

void URPGPlayerMovementComponent::ServerMove_PerformMovement(const FCharacterNetworkMoveData& Data)
{
    if (IsRevokedOnServer()) { StopRevokedMovement(); return; }
    if (auto* Riding = CharacterOwner ? CharacterOwner->FindComponentByClass<URPGRidingComponent>() : nullptr) Riding->ValidateServerMovementContext();
    if (auto* Riding = CharacterOwner ? CharacterOwner->FindComponentByClass<URPGRidingComponent>() : nullptr) Riding->PrepareServerDashMovement();
    TGuardValue<bool> Evaluating(bEvaluatingRidingMove, true);
    const bool Known = &Data == &RidingMoves.Moves[0] || &Data == &RidingMoves.Moves[1] || &Data == &RidingMoves.Moves[2];
    TGuardValue<bool> HasStamp(bHasRidingStamp, Known);
    TGuardValue<FRPGRidingMoveStamp> Stamp(EvaluatedStamp, Known ? static_cast<const FRPGRidingNetworkMoveData&>(Data).Riding : FRPGRidingMoveStamp{});
    const auto* Riding = CharacterOwner ? CharacterOwner->FindComponentByClass<URPGRidingComponent>() : nullptr;
    const auto Current = Riding ? FRPGRidingMoveStamp::FromState(Riding->GetRidingState()) : FRPGRidingMoveStamp{};
    // This also bypasses the base's falling-error grace path for a known state mismatch.
    if (!Known || !(EvaluatedStamp == Current)) GetPredictionData_Server_Character()->bForceClientUpdate = true;
    // No state/profile is ever assigned from the received identifiers.
    Super::ServerMove_PerformMovement(Data);
}

bool URPGPlayerMovementComponent::ServerCheckClientError(float Stamp, float Delta, const FVector& Accel, const FVector& Location,
    const FVector& Relative, FMovementBaseInterfaceData* Base, FName Bone, uint8 Mode)
{
    const bool BaseError = Super::ServerCheckClientError(Stamp, Delta, Accel, Location, Relative, Base, Bone, Mode);
    const auto* Riding = CharacterOwner ? CharacterOwner->FindComponentByClass<URPGRidingComponent>() : nullptr;
    const auto Current = Riding ? FRPGRidingMoveStamp::FromState(Riding->GetRidingState()) : FRPGRidingMoveStamp{};
    return BaseError || (bEvaluatingRidingMove && (!bHasRidingStamp || !(EvaluatedStamp == Current)));
}

void URPGPlayerMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& Response)
{
    if (&Response == &RidingResponse && RidingResponse.bHasRidingState)
        if (auto* Riding = CharacterOwner ? CharacterOwner->FindComponentByClass<URPGRidingComponent>() : nullptr)
            Riding->ApplyMovementCorrection(RidingResponse.State);
    // Install current same-epoch authority before the base schedules replay of unacknowledged input.
    Super::ClientHandleMoveResponse(Response);
}
