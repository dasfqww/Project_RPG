#include "AnimNotifyState/RPGAnimNotifyState_PerformTrace.h"

#include "Abilities/GameplayAbilityTargetTypes.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Component/Combat/PawnCombatComponent.h"
#include "Component/Equipment/RPGEquipComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Item/Weapon/RPGWeaponBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGAnimNotifyState_PerformTrace)

DEFINE_LOG_CATEGORY_STATIC(LogRPGWeaponTrace, Log, All);

namespace RPGWeaponTrace
{
	FTransform Interpolate(const FTransform& Start, const FTransform& End, const float Alpha)
	{
		FTransform Result;
		Result.Blend(Start, End, Alpha);
		return Result;
	}
}

URPGAnimNotifyState_PerformTrace::URPGAnimNotifyState_PerformTrace(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	bShouldFireInEditor = false;
#endif
}

void URPGAnimNotifyState_PerformTrace::NotifyBegin(
	USkeletalMeshComponent* MeshComponent,
	UAnimSequenceBase* Animation,
	const float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComponent, Animation, TotalDuration, EventReference);
	RemoveStaleStates();

	if (!ShouldExecute(MeshComponent))
	{
		RuntimeStates.Remove(MeshComponent);
		return;
	}

	ARPGWeaponBase* WeaponActor = ResolveWeapon(MeshComponent);
	UBoxComponent* CollisionBox = WeaponActor ? WeaponActor->GetWeaponCollisionBox() : nullptr;
	if (!WeaponActor || !CollisionBox)
	{
		UE_LOG(LogRPGWeaponTrace, Verbose,
			TEXT("Skipping trace for %s: no compatible weapon collision box."),
			*GetNameSafe(MeshComponent ? MeshComponent->GetOwner() : nullptr));
		RuntimeStates.Remove(MeshComponent);
		return;
	}

	FRuntimeState& State = RuntimeStates.FindOrAdd(MeshComponent);
	State.WeaponActor = WeaponActor;
	State.HitActors.Reset();
	State.PreviousCollisionTransform = CollisionBox->GetComponentTransform();
	State.PreviousSampleLocation = ResolveSampleLocation(WeaponActor);
}

void URPGAnimNotifyState_PerformTrace::NotifyTick(
	USkeletalMeshComponent* MeshComponent,
	UAnimSequenceBase* Animation,
	const float FrameDeltaTime,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComponent, Animation, FrameDeltaTime, EventReference);
	if (!ShouldExecute(MeshComponent))
	{
		return;
	}

	if (FRuntimeState* State = RuntimeStates.Find(MeshComponent))
	{
		PerformTrace(MeshComponent, *State);
	}
}

void URPGAnimNotifyState_PerformTrace::NotifyEnd(
	USkeletalMeshComponent* MeshComponent,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComponent, Animation, EventReference);
	if (ShouldExecute(MeshComponent))
	{
		if (FRuntimeState* State = RuntimeStates.Find(MeshComponent))
		{
			PerformTrace(MeshComponent, *State);
		}
	}
	RuntimeStates.Remove(MeshComponent);
}

bool URPGAnimNotifyState_PerformTrace::ShouldExecute(
	const USkeletalMeshComponent* MeshComponent) const
{
	const AActor* Owner = MeshComponent ? MeshComponent->GetOwner() : nullptr;
	return Owner && Owner->GetLocalRole() == ExecuteNetRole;
}

ARPGWeaponBase* URPGAnimNotifyState_PerformTrace::ResolveWeapon(
	const USkeletalMeshComponent* MeshComponent) const
{
	AActor* Owner = MeshComponent ? MeshComponent->GetOwner() : nullptr;
	if (!Owner)
	{
		return nullptr;
	}

	if (const URPGEquipComponent* EquipComponent =
		Owner->FindComponentByClass<URPGEquipComponent>())
	{
		URPGItemBase* Item = nullptr;
		AActor* SpawnedActor = nullptr;
		EEquipmentSlotType SlotType = EEquipmentSlotType::None;
		if (EquipComponent->FindEquippedWeapon(
			WeaponHandType, Item, SpawnedActor, SlotType))
		{
			if (ARPGWeaponBase* Weapon = Cast<ARPGWeaponBase>(SpawnedActor))
			{
				return Weapon;
			}
		}
	}

	if (const UPawnCombatComponent* CombatComponent =
		Owner->FindComponentByClass<UPawnCombatComponent>())
	{
		ARPGWeaponBase* Weapon = CombatComponent->GetCharacterCurrentEquippedWeapon();
		if (Weapon &&
			(WeaponHandType == EWeaponHandType::TwoHand ||
			 Weapon->GetWeaponHandType() == EWeaponHandType::Count ||
			 Weapon->GetWeaponHandType() == WeaponHandType))
		{
			return Weapon;
		}
	}

	return nullptr;
}

FVector URPGAnimNotifyState_PerformTrace::ResolveSampleLocation(
	const ARPGWeaponBase* WeaponActor) const
{
	if (!WeaponActor)
	{
		return FVector::ZeroVector;
	}

	if (const UStaticMeshComponent* WeaponMesh = WeaponActor->GetWeaponMesh())
	{
		if (!TraceParams.TraceSocketName.IsNone() &&
			WeaponMesh->DoesSocketExist(TraceParams.TraceSocketName))
		{
			return WeaponMesh->GetSocketLocation(TraceParams.TraceSocketName);
		}
	}

	const UBoxComponent* CollisionBox = WeaponActor->GetWeaponCollisionBox();
	return CollisionBox ? CollisionBox->GetComponentLocation() : WeaponActor->GetActorLocation();
}

void URPGAnimNotifyState_PerformTrace::PerformTrace(
	USkeletalMeshComponent* MeshComponent,
	FRuntimeState& State)
{
	ARPGWeaponBase* WeaponActor = State.WeaponActor.Get();
	UBoxComponent* CollisionBox = WeaponActor ? WeaponActor->GetWeaponCollisionBox() : nullptr;
	UWorld* World = MeshComponent ? MeshComponent->GetWorld() : nullptr;
	AActor* Owner = MeshComponent ? MeshComponent->GetOwner() : nullptr;
	if (!WeaponActor || !CollisionBox || !World || !Owner || !EventTag.IsValid())
	{
		return;
	}

	const FTransform CurrentTransform = CollisionBox->GetComponentTransform();
	const FVector CurrentSampleLocation = ResolveSampleLocation(WeaponActor);
	const FVector BoxExtent = CollisionBox->GetScaledBoxExtent().ComponentMax(FVector(1.0f));
	const float LinearTravel = FVector::Distance(State.PreviousSampleLocation, CurrentSampleLocation);
	const float AngularTravel = State.PreviousCollisionTransform.GetRotation().AngularDistance(
		CurrentTransform.GetRotation()) * BoxExtent.Size();
	const float MaxStepDistance = FMath::Max(TraceParams.TargetDistance, 1.0f);
	const int32 SubStepCount = FMath::Max(
		1, FMath::CeilToInt(FMath::Max(LinearTravel, AngularTravel) / MaxStepDistance));

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RPGWeaponTrace), false);
	QueryParams.bReturnPhysicalMaterial = true;
	QueryParams.AddIgnoredActor(Owner);
	QueryParams.AddIgnoredActor(WeaponActor);

	TArray<FHitResult> NewHitResults;
	for (int32 StepIndex = 0; StepIndex < SubStepCount; ++StepIndex)
	{
		const float StartAlpha = static_cast<float>(StepIndex) / SubStepCount;
		const float EndAlpha = static_cast<float>(StepIndex + 1) / SubStepCount;
		const FTransform StartTransform = RPGWeaponTrace::Interpolate(
			State.PreviousCollisionTransform, CurrentTransform, StartAlpha);
		const FTransform EndTransform = RPGWeaponTrace::Interpolate(
			State.PreviousCollisionTransform, CurrentTransform, EndAlpha);
		const FTransform AverageTransform = RPGWeaponTrace::Interpolate(
			StartTransform, EndTransform, 0.5f);

		TArray<FHitResult> StepHits;
		World->SweepMultiByObjectType(
			StepHits,
			StartTransform.GetLocation(),
			EndTransform.GetLocation(),
			AverageTransform.GetRotation(),
			ObjectQueryParams,
			FCollisionShape::MakeBox(BoxExtent),
			QueryParams);

		for (const FHitResult& HitResult : StepHits)
		{
			AActor* HitActor = HitResult.GetActor();
			const TWeakObjectPtr<AActor> WeakHitActor(HitActor);
			if (!HitActor || State.HitActors.Contains(WeakHitActor))
			{
				continue;
			}

			State.HitActors.Add(WeakHitActor);
			NewHitResults.Add(HitResult);
		}

#if WITH_EDITOR
		if (TraceDebugParams.bDrawDebugShape)
		{
			const FColor Color = StepHits.IsEmpty()
				? TraceDebugParams.TraceColor
				: TraceDebugParams.HitColor;
			DrawDebugBox(World, StartTransform.GetLocation(), BoxExtent,
				AverageTransform.GetRotation(), Color, false, 2.0f);
			DrawDebugBox(World, EndTransform.GetLocation(), BoxExtent,
				AverageTransform.GetRotation(), Color, false, 2.0f);
			DrawDebugLine(World, StartTransform.GetLocation(), EndTransform.GetLocation(),
				Color, false, 2.0f);
		}
#endif
	}

	State.PreviousCollisionTransform = CurrentTransform;
	State.PreviousSampleLocation = CurrentSampleLocation;

	if (NewHitResults.IsEmpty())
	{
		return;
	}

	FGameplayAbilityTargetDataHandle TargetDataHandle;
	for (const FHitResult& HitResult : NewHitResults)
	{
		FGameplayAbilityTargetData_SingleTargetHit* TargetData =
			new FGameplayAbilityTargetData_SingleTargetHit();
		TargetData->HitResult = HitResult;
		TargetDataHandle.Add(TargetData);
	}

	FGameplayEventData EventData;
	EventData.EventTag = EventTag;
	EventData.Instigator = WeaponActor;
	EventData.TargetData = MoveTemp(TargetDataHandle);
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, EventTag, EventData);
}

void URPGAnimNotifyState_PerformTrace::RemoveStaleStates()
{
	for (auto It = RuntimeStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Value().WeaponActor.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}
