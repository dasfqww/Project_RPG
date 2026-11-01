// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Combat/PawnCombatComponent.h"
#include "Ability/Skill/RPGGreatSwordResolveAbility.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Item/Weapon/RPGWeaponBase.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "FunctionLibrary/RPGCombatFunctionLibrary.h"
#include "Security/RPGDungeonGameplayAuthority.h"

#include "RPGDebugHelper.h"

void UPawnCombatComponent::OnRegister()
{
	Super::OnRegister();
	BindEquipmentNotifications();
}

void UPawnCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	BindEquipmentNotifications();
}

void UPawnCombatComponent::OnUnregister()
{
	if (URPGAuthoritativeEquipmentComponent* Equipment = ObservedEquipment.Get())
	{
		Equipment->OnEquipmentSnapshotsChanged.Remove(EquipmentSnapshotsChangedHandle);
		Equipment->OnActiveEquipStateChanged.Remove(ActiveEquipStateChangedHandle);
	}
	ObservedEquipment.Reset();
	EquipmentSnapshotsChangedHandle.Reset();
	ActiveEquipStateChangedHandle.Reset();
	StopWeaponCollision();
	for (const auto& Pair : CharacterHasWeaponMap)
	{
		UnbindWeaponEvents(Pair.Value.Get());
	}
	Super::OnUnregister();
}

void UPawnCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopWeaponCollision();
	Super::EndPlay(EndPlayReason);
}

void UPawnCombatComponent::BindEquipmentNotifications()
{
	URPGAuthoritativeEquipmentComponent* Equipment = GetOwner()
		? GetOwner()->FindComponentByClass<URPGAuthoritativeEquipmentComponent>() : nullptr;
	if (Equipment == ObservedEquipment.Get()) return;
	if (URPGAuthoritativeEquipmentComponent* Previous = ObservedEquipment.Get())
	{
		Previous->OnEquipmentSnapshotsChanged.Remove(EquipmentSnapshotsChangedHandle);
		Previous->OnActiveEquipStateChanged.Remove(ActiveEquipStateChangedHandle);
	}
	ObservedEquipment = Equipment;
	EquipmentSnapshotsChangedHandle.Reset();
	ActiveEquipStateChangedHandle.Reset();
	if (Equipment)
	{
		EquipmentSnapshotsChangedHandle = Equipment->OnEquipmentSnapshotsChanged.AddUObject(
			this, &ThisClass::HandleEquipmentChanged);
		ActiveEquipStateChangedHandle = Equipment->OnActiveEquipStateChanged.AddWeakLambda(
			this, [this](EEquipState, EEquipState) { HandleEquipmentChanged(); });
	}
	HandleEquipmentChanged();
}

void UPawnCombatComponent::HandleEquipmentChanged()
{
	if (GetCharacterCurrentEquippedWeaponType() != ERPGGladiatorWeaponType::GreatSword)
		URPGGreatSwordResolveAbility::RevokeFor(GetOwner());
	if (CollisionWeapon.Get() != GetCharacterCurrentEquippedWeapon())
	{
		StopWeaponCollision();
	}
}

bool UPawnCombatComponent::CanDispatchWeaponEvent(const ARPGWeaponBase* Weapon) const
{
	return IsValid(Weapon) && GetOwner() && GetOwner()->HasAuthority()
		&& !IsDungeonGameplayRevoked(GetOwner())
		&& CollisionWeapon.Get() == Weapon && GetCharacterCurrentEquippedWeapon() == Weapon
		&& Weapon->GetWeaponCollisionBox()
		&& Weapon->GetWeaponCollisionBox()->GetCollisionEnabled() == ECollisionEnabled::QueryOnly;
}

void UPawnCombatComponent::BindWeaponEvents(ARPGWeaponBase* Weapon)
{
	if (!IsValid(Weapon)) return;
	const TWeakObjectPtr<ARPGWeaponBase> Source = Weapon;
	Weapon->OnWeaponHitTarget.BindWeakLambda(this, [this, Source](AActor* Target)
	{
		if (IsValid(Target) && CanDispatchWeaponEvent(Source.Get())) OnHitTargetActor(Target);
	});
	Weapon->OnWeaponPulledFromTarget.BindWeakLambda(this, [this, Source](AActor* Target)
	{
		if (IsValid(Target) && CanDispatchWeaponEvent(Source.Get())) OnWeaponPulledFromTargetActor(Target);
	});
}

void UPawnCombatComponent::UnbindWeaponEvents(ARPGWeaponBase* Weapon)
{
	if (!IsValid(Weapon)) return;
	if (Weapon->OnWeaponHitTarget.IsBoundToObject(this)) Weapon->OnWeaponHitTarget.Unbind();
	if (Weapon->OnWeaponPulledFromTarget.IsBoundToObject(this)) Weapon->OnWeaponPulledFromTarget.Unbind();
}

void UPawnCombatComponent::StopWeaponCollision()
{
	ARPGWeaponBase* Previous = CollisionWeapon.Get();
	++CollisionWindowSerial;
	DispatchSweepHit = nullptr;
	if (UsesContinuousWeaponTrace()) SetComponentTickEnabled(false);
	CollisionWeapon.Reset(); // Ignore overlap callbacks generated while disabling the box.
	UnbindWeaponEvents(Previous);
	if (IsValid(Previous) && Previous->GetWeaponCollisionBox())
	{
		Previous->GetWeaponCollisionBox()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	OverlappedActors.Reset();
}

void UPawnCombatComponent::RegisterSpawnedWeapon(FGameplayTag InWeaponTagToRegister, ARPGWeaponBase* InWeaponToRegister, bool bRegisterAsEquippedWeapon)
{
	checkf(!CharacterHasWeaponMap.Contains(InWeaponTagToRegister),
		TEXT("A named named %s has already been added as carried weapon"), *InWeaponTagToRegister.ToString());
	check(InWeaponToRegister);

	CharacterHasWeaponMap.Emplace(InWeaponTagToRegister, InWeaponToRegister);

	BindWeaponEvents(InWeaponToRegister);

	if (bRegisterAsEquippedWeapon)
	{
		CurrentEquippedWeaponTag = InWeaponTagToRegister;
		HandleEquipmentChanged();
	}

	const FString WeaponString = FString::Printf(TEXT("A weapon named: %s has been registered using the tag %s"),
		*InWeaponToRegister->GetName(), *InWeaponTagToRegister.ToString());
	Debug::Print(WeaponString);
}

ARPGWeaponBase* UPawnCombatComponent::GetCharacterHasWeaponByTag(FGameplayTag InWeaponTagToGet) const
{
	if (const TWeakObjectPtr<ARPGWeaponBase>* FoundWeapon = CharacterHasWeaponMap.Find(InWeaponTagToGet))
	{
		return FoundWeapon->Get();
	}

	return nullptr;
}

ARPGWeaponBase* UPawnCombatComponent::GetCharacterCurrentEquippedWeapon() const
{
	const URPGAuthoritativeEquipmentComponent* Equipment = GetOwner()
		? GetOwner()->FindComponentByClass<URPGAuthoritativeEquipmentComponent>() : nullptr;
	if (Equipment && Equipment->HasWeaponSnapshots())
	{
		FRPGEquipmentSnapshot Snapshot;
		return Equipment->FindWeaponSnapshot(EWeaponHandType::Count, Snapshot)
			&& IsValid(Snapshot.EquippedActor) ? Cast<ARPGWeaponBase>(Snapshot.EquippedActor) : nullptr;
	}
	// NPC/startup loadouts remain actor-only compatibility, not a second item system.
	if (!CurrentEquippedWeaponTag.IsValid())
	{
		return nullptr;
	}

	return GetCharacterHasWeaponByTag(CurrentEquippedWeaponTag);
}

ERPGGladiatorWeaponType UPawnCombatComponent::GetCharacterCurrentEquippedWeaponType() const
{
	const ARPGWeaponBase* Weapon = GetCharacterCurrentEquippedWeapon();
	if (!IsValid(Weapon)) return ERPGGladiatorWeaponType::Count;
	const auto* Equipment = GetOwner() ? GetOwner()->FindComponentByClass<URPGAuthoritativeEquipmentComponent>() : nullptr;
	if (Equipment && Equipment->HasWeaponSnapshots())
	{
		FRPGEquipmentSnapshot Snapshot;
		return Equipment->FindWeaponSnapshot(EWeaponHandType::Count, Snapshot) && Snapshot.EquippedActor == Weapon
			? Snapshot.WeaponType : ERPGGladiatorWeaponType::Count;
	}
	return Weapon->GetWeaponType();
}

void UPawnCombatComponent::ToggleWeaponCollision(bool bShouldEnable, EToggleDamageType ToggleDamageType)
{
	if (ToggleDamageType==EToggleDamageType::CurrentEquippedWeapon)
	{
		ToggleCurrentEquippedWeaponCollision(bShouldEnable);
	}

	else
	{
		ToggleBodyCollsionBoxCollision(bShouldEnable, ToggleDamageType);
	}
}

void UPawnCombatComponent::OnHitTargetActor(AActor* HitActor)
{
}

void UPawnCombatComponent::OnWeaponPulledFromTargetActor(AActor* InteractedActor)
{
}

void UPawnCombatComponent::ToggleCurrentEquippedWeaponCollision(bool bShouldEnable)
{
	BindEquipmentNotifications();
	if (!bShouldEnable || !GetOwner() || !GetOwner()->HasAuthority() || IsDungeonGameplayRevoked(GetOwner()))
	{
		StopWeaponCollision();
		return;
	}
	ARPGWeaponBase* WeaponToToggle = GetCharacterCurrentEquippedWeapon();
	if (!IsValid(WeaponToToggle) || !WeaponToToggle->GetWeaponCollisionBox())
	{
		StopWeaponCollision();
		return;
	}
	if (!CanDispatchWeaponEvent(WeaponToToggle))
	{
		StopWeaponCollision();
		CollisionWeapon = WeaponToToggle;
		BindWeaponEvents(WeaponToToggle);
		PreviousWeaponTransform = WeaponToToggle->GetWeaponCollisionBox()->GetComponentTransform();
	}
	WeaponToToggle->GetWeaponCollisionBox()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	if (UsesContinuousWeaponTrace() && CanDispatchWeaponEvent(WeaponToToggle))
	{
		SetComponentTickInterval(0.f);
		SetComponentTickEnabled(true);
	}
}

bool UPawnCombatComponent::TryGetCurrentServerWeaponHit(const AActor* Target, FHitResult& OutHit) const
{
	if (!IsValid(Target) || !DispatchSweepHit || DispatchSweepHit->GetActor() != Target ||
		DispatchWindowSerial != CollisionWindowSerial || !CanDispatchWeaponEvent(CollisionWeapon.Get())) return false;
	OutHit = *DispatchSweepHit;
	return true;
}

void UPawnCombatComponent::SweepCurrentWeapon()
{
	ARPGWeaponBase* Weapon = CollisionWeapon.Get();
	if (!UsesContinuousWeaponTrace() || bSweepingWeapon || !CanDispatchWeaponEvent(Weapon) || !GetWorld()) return;
	TGuardValue<bool> SweepGuard(bSweepingWeapon, true);
	UBoxComponent* Box = Weapon->GetWeaponCollisionBox();
	const FTransform Previous = PreviousWeaponTransform;
	const FTransform Current = Box->GetComponentTransform();
	PreviousWeaponTransform = Current;
	const FVector Extent = Box->GetScaledBoxExtent();
	// A teleport, invalid geometry or scale discontinuity must not sweep through an arena.
	if (Previous.ContainsNaN() || Current.ContainsNaN() || Extent.ContainsNaN() || Extent.GetMin() <= 0.0 ||
		!Previous.GetScale3D().Equals(Current.GetScale3D(), KINDA_SMALL_NUMBER)) return;
	const double Angle = Previous.GetRotation().AngularDistance(Current.GetRotation());
	const double Travel = FVector::Distance(Previous.GetLocation(), Current.GetLocation()) + Angle * Extent.Size();
	const double RequiredSteps = FMath::Max(Travel / 4.0, Angle / FMath::DegreesToRadians(2.0));
	constexpr int32 MaxSteps = 128;
	if (!FMath::IsFinite(RequiredSteps) || RequiredSteps > MaxSteps) return;
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(RequiredSteps));
	FCollisionQueryParams Query(SCENE_QUERY_STAT(RPGPlayerWeaponSweep), false, GetOwner());
	Query.AddIgnoredActor(Weapon);
	Query.bFindInitialOverlaps = true;
	const FCollisionResponseParams Responses(Box->GetCollisionResponseToChannels());
	const uint64 Window = CollisionWindowSerial;
	APawn* Source = Cast<APawn>(GetOwner());
	for (int32 Index = 0; Index < Steps; ++Index)
	{
		FTransform Start, End, Middle;
		Start.Blend(Previous, Current, static_cast<float>(Index) / Steps);
		End.Blend(Previous, Current, static_cast<float>(Index + 1) / Steps);
		Middle.Blend(Start, End, .5f);
		TArray<FHitResult> Hits;
		GetWorld()->SweepMultiByChannel(Hits, Start.GetLocation(), End.GetLocation(), Middle.GetRotation(),
			Box->GetCollisionObjectType(), FCollisionShape::MakeBox(Extent), Query, Responses);
		for (const FHitResult& Hit : Hits)
		{
			if (Window != CollisionWindowSerial || !CanDispatchWeaponEvent(Weapon)) return;
			APawn* Target = Cast<APawn>(Hit.GetActor());
			if (!Target || OverlappedActors.Contains(Target) || !Hit.GetComponent() ||
				!Box->GetGenerateOverlapEvents() || !Hit.GetComponent()->GetGenerateOverlapEvents() ||
				!URPGCombatFunctionLibrary::IsTargetPawnHostile(Source, Target)) continue;
			TGuardValue<const FHitResult*> HitGuard(DispatchSweepHit, &Hit);
			TGuardValue<uint64> WindowGuard(DispatchWindowSerial, Window);
			OnHitTargetActor(Target);
		}
	}
}

TArray<ARPGWeaponBase*> UPawnCombatComponent::GetRegisteredStartupWeapons() const
{
	TArray<ARPGWeaponBase*> Actors;
	for (const auto& Pair : CharacterHasWeaponMap)
		if (ARPGWeaponBase* Actor = Pair.Value.Get()) Actors.AddUnique(Actor);
	return Actors;
}

void UPawnCombatComponent::ToggleBodyCollsionBoxCollision(bool bShouldEnable, EToggleDamageType ToggleDamageType)
{
}
