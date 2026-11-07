#include "Component/RPGCharacterMovementComponent.h"
#include "Riding/RPGRidingTypes.h"
#include "Combat/RPGCombatControlTypes.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "GameFramework/Character.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Controller.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

URPGCharacterMovementComponent::URPGCharacterMovementComponent()
{
	SetIsReplicatedByDefault(true);
}

void URPGCharacterMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	FDoRepLifetimeParams Params;
	Params.Condition = COND_SimulatedOnly;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(URPGCharacterMovementComponent, bReplicatedSkillMovementActive, Params);
}

FGuid URPGCharacterMovementComponent::BeginSkillMovementConstraint(UObject* Source, float SpeedScalar)
{
	if (!IsValid(Source) || !IsValid(CharacterOwner) || !FMath::IsFinite(SpeedScalar)
		|| SpeedScalar < 0.f || SpeedScalar > 1.f) return {};
	const FGuid Handle = FGuid::NewGuid();
	SkillMovementConstraints.Add(Handle, {Source, SpeedScalar});
	if (SpeedScalar == 0.f)
	{
		Acceleration = FVector::ZeroVector;
		Velocity.X = Velocity.Y = 0.f;
		CharacterOwner->ConsumeMovementInputVector();
	}
	UpdateSkillMovementRootMotion();
	return Handle;
}

void URPGCharacterMovementComponent::EndSkillMovementConstraint(FGuid Handle)
{
	SkillMovementConstraints.Remove(Handle);
	UpdateSkillMovementRootMotion();
}

float URPGCharacterMovementComponent::GetSkillMovementScalar() const
{
	float Scalar = 1.f;
	for (const auto& Entry : SkillMovementConstraints)
		if (Entry.Value.Source.IsValid()) Scalar = FMath::Min(Scalar, Entry.Value.SpeedScalar);
	return Scalar;
}

bool URPGCharacterMovementComponent::IsVoluntaryMovementBlocked() const
{
	const auto* ASC = CharacterOwner ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(CharacterOwner) : nullptr;
	if (ASC && ASC->GetAvatarActor() == CharacterOwner)
	{
		const auto* Stats = ASC->GetSet<URPGAttributeSet>();
		const auto Death = FGameplayTag::RequestGameplayTag(TEXT("Shared.Status.Death"), false);
		if ((Stats && (!FMath::IsFinite(Stats->GetCurrentHealth()) || Stats->GetCurrentHealth() <= 0.f))
			|| (Death.IsValid() && ASC->HasMatchingGameplayTag(Death))) return true;
	}
	return RPGCombatControl::IsActionBlocked(CharacterOwner) || GetSkillMovementScalar() == 0.f;
}

void URPGCharacterMovementComponent::RestoreSkillRootMotionMode()
{
	if (UAnimInstance* Instance = SkillRootMotionInstance.Get())
	{
		// A later explicit writer keeps ownership of a different mode.
		if (Instance->RootMotionMode == ERootMotionMode::IgnoreRootMotion)
			Instance->SetRootMotionMode(static_cast<ERootMotionMode::Type>(SavedSkillRootMotionMode));
	}
	SkillRootMotionInstance.Reset();
}

void URPGCharacterMovementComponent::UpdateSkillMovementRootMotion()
{
	for (auto It = SkillMovementConstraints.CreateIterator(); It; ++It)
		if (!It.Value().Source.IsValid()) It.RemoveCurrent();
	const bool bLocalActive = !SkillMovementConstraints.IsEmpty();
	if (CharacterOwner && CharacterOwner->HasAuthority() && bReplicatedSkillMovementActive != bLocalActive)
	{
		bReplicatedSkillMovementActive = bLocalActive;
		MARK_PROPERTY_DIRTY_FROM_NAME(URPGCharacterMovementComponent, bReplicatedSkillMovementActive, this);
		CharacterOwner->ForceNetUpdate();
	}
	const bool bActive = bLocalActive || (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy
		&& bReplicatedSkillMovementActive);
	UAnimInstance* Instance = CharacterOwner && CharacterOwner->GetMesh()
		? CharacterOwner->GetMesh()->GetAnimInstance() : nullptr;
	if (!bActive || SkillRootMotionInstance.Get() != Instance) RestoreSkillRootMotionMode();
	if (bActive && IsValid(Instance) && !SkillRootMotionInstance.IsValid())
	{
		SkillRootMotionInstance = Instance;
		SavedSkillRootMotionMode = Instance->RootMotionMode;
		// Preserve movement-component root-motion sources (knockback/airborne).
		// Ignoring animation root motion lets normal input drive the mobile cast.
		Instance->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
		RootMotionParams.Clear();
	}
}

void URPGCharacterMovementComponent::OnRep_SkillMovementActive()
{
	UpdateSkillMovementRootMotion();
}

void URPGCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	UpdateSkillMovementRootMotion();
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void URPGCharacterMovementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SkillMovementConstraints.Reset();
	RestoreSkillRootMotionMode();
	Super::EndPlay(EndPlayReason);
}

FGuid URPGCharacterMovementComponent::BeginWalkSpeedLimit(float BaseSpeed)
{
	if (!FMath::IsFinite(BaseSpeed) || BaseSpeed <= 0.f) return {};
	const auto Handle = FGuid::NewGuid(); WalkSpeedLimits.Add(Handle, BaseSpeed); return Handle;
}
void URPGCharacterMovementComponent::EndWalkSpeedLimit(FGuid Handle) { WalkSpeedLimits.Remove(Handle); }

float URPGCharacterMovementComponent::GetMaxSpeed() const
{
	if (IsVoluntaryMovementBlocked()) return 0.f;
    FRPGRidingMovementProfile Riding;
    if (MovementMode == MOVE_Walking && TryGetRidingProfile(Riding))
    {
        float Speed = Riding.Speed;
        for (const auto& Limit : WalkSpeedLimits) Speed = FMath::Min(Speed, Limit.Value);
        return Speed * GetSkillMovementScalar(); // Absolute constraints precede Riding; combat stat multipliers do not.
    }
	float Base = Super::GetMaxSpeed();
	if (MovementMode != MOVE_Walking && MovementMode != MOVE_NavWalking && MovementMode != MOVE_Falling)
		return Base * GetSkillMovementScalar();
	for (const auto& Limit : WalkSpeedLimits) Base = FMath::Min(Base, Limit.Value);
	const auto* ASC = CharacterOwner ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(CharacterOwner) : nullptr;
	const auto* Stats = ASC && ASC->GetAvatarActor() == CharacterOwner ? ASC->GetSet<URPGAttributeSet>() : nullptr;
	if (!Stats) return Base * GetSkillMovementScalar();
	const float Scale = Stats->GetMoveSpeed(), Percent = Stats->GetMoveSpeedPercent();
	const float Result = Base * Scale * FMath::Max(0.f, 1.f + Percent / 100.f) * GetSkillMovementScalar();
	return FMath::IsFinite(Scale) && Scale >= 0.f && FMath::IsFinite(Percent) && FMath::IsFinite(Result) ? FMath::Max(0.f, Result) : 0.f;
}

float URPGCharacterMovementComponent::GetMaxAcceleration() const
{
    if (IsVoluntaryMovementBlocked()) return 0.f;
    FRPGRidingMovementProfile Riding;
    return MovementMode == MOVE_Walking && TryGetRidingProfile(Riding) ? Riding.Acceleration : Super::GetMaxAcceleration();
}

bool URPGCharacterMovementComponent::TryGetRidingProfile(FRPGRidingMovementProfile& Out) const { Out = {}; return false; }
float URPGCharacterMovementComponent::GetMaxBrakingDeceleration() const
{
    FRPGRidingMovementProfile Riding;
    return MovementMode == MOVE_Walking && TryGetRidingProfile(Riding) ? Riding.Braking : Super::GetMaxBrakingDeceleration();
}
FRotator URPGCharacterMovementComponent::GetDeltaRotation(float DeltaTime) const
{
    FRPGRidingMovementProfile Riding;
    if (MovementMode != MOVE_Walking || !TryGetRidingProfile(Riding)) return Super::GetDeltaRotation(DeltaTime);
    return FRotator(0, FMath::IsFinite(DeltaTime) ? FMath::Clamp(Riding.YawRate * DeltaTime, 0.f, 360.f) : 0.f, 0);
}

bool URPGCharacterMovementComponent::CanAttemptJump() const
{
    FRPGRidingMovementProfile Riding;
    return !TryGetRidingProfile(Riding) && !IsVoluntaryMovementBlocked() && Super::CanAttemptJump();
}

FVector URPGCharacterMovementComponent::ConstrainInputAcceleration(const FVector& Input) const
{
	// MoveAutonomous also calls this for server moves and client saved-move replay.
	return IsVoluntaryMovementBlocked() ? FVector::ZeroVector : Super::ConstrainInputAcceleration(Input);
}

void URPGCharacterMovementComponent::PerformMovement(float DeltaTime)
{
	if (IsVoluntaryMovementBlocked())
	{
		Acceleration = FVector::ZeroVector;
		Velocity.X = Velocity.Y = 0.f;
		bHasRequestedVelocity = false;
		CharacterOwner->ConsumeMovementInputVector();
	}
	Super::PerformMovement(DeltaTime); // Gravity/falling and death's MOVE_None remain owned by movement.
}

void URPGCharacterMovementComponent::ApplyRootMotionToVelocity(float DeltaTime)
{
	Super::ApplyRootMotionToVelocity(DeltaTime);
	const auto* Control = CharacterOwner ? CharacterOwner->FindComponentByClass<URPGCombatControlComponent>() : nullptr;
	if (IsVoluntaryMovementBlocked() && !(Control && (Control->HasKnockbackMotion() || Control->HasAirborneMotion()))) Velocity.X = Velocity.Y = 0.f;
}

void URPGCharacterMovementComponent::RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed)
{
	if (!IsVoluntaryMovementBlocked()) Super::RequestDirectMove(MoveVelocity, bForceMaxSpeed);
}

void URPGCharacterMovementComponent::RequestPathMove(const FVector& Input)
{
	if (!IsVoluntaryMovementBlocked()) Super::RequestPathMove(Input);
}

void URPGCharacterMovementComponent::PhysicsRotation(float DeltaTime)
{
	if (!RPGCombatControl::IsActionBlocked(CharacterOwner)) Super::PhysicsRotation(DeltaTime);
}

void URPGCharacterMovementComponent::UpdateBasedMovement(float DeltaSeconds)
{
	const auto* Base = CharacterOwner ? Cast<UPrimitiveComponent>(CharacterOwner->GetMovementBaseObject()) : nullptr;
	bool bMeasure = CharacterOwner && CharacterOwner->HasAuthority()
		&& IsMovingOnGround() && Base && Base->GetWorld() == GetWorld()
		&& MovementBaseUtility::IsDynamicBase(GetMovementBaseInterfaceData());
	// Server authority alone is not enough for a base owned by, or attached to,
	// a player/pawn. Keep the original world-space checks for those objects.
	for (const AActor* Attachment = Base ? Base->GetOwner() : nullptr; bMeasure && Attachment; Attachment = Attachment->GetAttachParentActor())
	{
		if (!Attachment->HasAuthority()) { bMeasure = false; break; }
		for (const AActor* Owner = Attachment; Owner; Owner = Owner->GetOwner())
			if (Owner->IsA<APawn>() || Owner->IsA<AController>()) { bMeasure = false; break; }
	}
	const TWeakObjectPtr<USceneComponent> BeforeComponent = UpdatedComponent;
	const FVector Before = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	Super::UpdateBasedMovement(DeltaSeconds);
	if (bMeasure && BeforeComponent.IsValid() && BeforeComponent.Get() == UpdatedComponent)
	{
		const FVector Total = ServerBasedMovementTotal + UpdatedComponent->GetComponentLocation() - Before;
		if (!Before.ContainsNaN() && !Total.ContainsNaN()) ServerBasedMovementTotal = Total;
	}
}

void URPGCharacterMovementComponent::HandleImpact(const FHitResult& Hit, float TimeSlice, const FVector& MoveDelta)
{
	Super::HandleImpact(Hit, TimeSlice, MoveDelta);
	if (auto* Control = CharacterOwner ? CharacterOwner->FindComponentByClass<URPGCombatControlComponent>() : nullptr)
	{
		Control->HandleKnockbackImpact(Hit);
		Control->HandleAirborneImpact(Hit);
	}
}
