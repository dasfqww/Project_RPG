#include "Component/Equipment/RPGPlayerWeaponLoadoutComponent.h"
#include "Combat/RPGCombatControlTypes.h"

#include "Ability/RPGGameplayAbility.h"
#include "Animation/AnimInstance.h"
#include "AnimInstance/Player/RPGPlayerLinkedAnimLayer.h"
#include "Component/Combat/PawnCombatComponent.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "InputMappingContext.h"
#include "Net/UnrealNetwork.h"
#include "RPGGameplayTags.h"
#include "PlayerClass/RPGPlayerClassComponent.h"

URPGPlayerWeaponLoadoutComponent::URPGPlayerWeaponLoadoutComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	AbilityLevelSourceInputTag = RPGGameplayTags::InputTag_EquipWeapon;
}

void URPGPlayerWeaponLoadoutComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, Presentation);
}

void URPGPlayerWeaponLoadoutComponent::OnRegister()
{
	Super::OnRegister();
	BindEquipment();
	RefreshLoadout();
}

void URPGPlayerWeaponLoadoutComponent::BeginPlay()
{
	Super::BeginPlay();
	BindEquipment();
	RefreshLoadout();
}

void URPGPlayerWeaponLoadoutComponent::OnUnregister()
{
	UnbindAnimation();
	if (auto* Equipment = ObservedEquipment.Get())
	{
		Equipment->OnEquipmentSnapshotsChanged.Remove(SnapshotChangedHandle);
		Equipment->OnActiveEquipStateChanged.Remove(StateChangedHandle);
	}
	ObservedEquipment.Reset();
	SnapshotChangedHandle.Reset();
	StateChangedHandle.Reset();
	ReleaseAbilities();
	ReleasePresentation();
	RestoreStartupVisibility();
	Super::OnUnregister();
}

void URPGPlayerWeaponLoadoutComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	UnbindAnimation();
	ReleaseAbilities();
	ReleasePresentation();
	Super::EndPlay(Reason);
}

void URPGPlayerWeaponLoadoutComponent::BindEquipment()
{
	auto* Equipment = GetOwner() ? GetOwner()->FindComponentByClass<URPGAuthoritativeEquipmentComponent>() : nullptr;
	if (Equipment == ObservedEquipment.Get()) return;
	if (auto* Previous = ObservedEquipment.Get())
	{
		Previous->OnEquipmentSnapshotsChanged.Remove(SnapshotChangedHandle);
		Previous->OnActiveEquipStateChanged.Remove(StateChangedHandle);
	}
	ObservedEquipment = Equipment;
	SnapshotChangedHandle.Reset();
	StateChangedHandle.Reset();
	if (Equipment)
	{
		SnapshotChangedHandle = Equipment->OnEquipmentSnapshotsChanged.AddUObject(this, &ThisClass::RefreshLoadout);
		StateChangedHandle = Equipment->OnActiveEquipStateChanged.AddWeakLambda(
			this, [this](EEquipState, EEquipState) { RefreshLoadout(); });
	}
}

void URPGPlayerWeaponLoadoutComponent::BindAnimation()
{
	const auto* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (ObservedMesh.Get() == Mesh) return;
	UnbindAnimation();
	ObservedMesh = Mesh;
	if (Mesh) Mesh->OnAnimInitialized.AddUniqueDynamic(this, &ThisClass::RefreshPresentation);
}

void URPGPlayerWeaponLoadoutComponent::UnbindAnimation()
{
	if (auto* Mesh = ObservedMesh.Get()) Mesh->OnAnimInitialized.RemoveDynamic(this, &ThisClass::RefreshPresentation);
	ObservedMesh.Reset();
}

bool URPGPlayerWeaponLoadoutComponent::ResolveGrantLevel(const URPGAbilitySystemComponent& ASC, int32& OutLevel) const
{
	OutLevel = 0;
	for (const FGameplayAbilitySpec& Spec : ASC.GetActivatableAbilities())
	{
		if (!Spec.GetDynamicSpecSourceTags().HasTagExact(AbilityLevelSourceInputTag)) continue;
		if (OutLevel != 0 || Spec.Level < 1 || Spec.SourceObject.Get() != GetOwner()) return false;
		OutLevel = Spec.Level;
	}
	return OutLevel > 0;
}

bool URPGPlayerWeaponLoadoutComponent::GrantWeaponAbilities(
	ARPGPlayerWeapon& Weapon, URPGAbilitySystemComponent& ASC, const int32 Level)
{
	TArray<FGameplayAbilitySpec> Specs;
	TSet<FGameplayTag> InputTags;
	const auto* ClassOwner = GetOwner()->FindComponentByClass<URPGPlayerClassComponent>();
	const auto Add = [&](const FRPGPlayerAbilitySet& Entry)
	{
		if (ClassOwner && ClassOwner->RoutesAbilityToClass(Entry.AbilityToGrant.Get(), Entry.InputTag)) return true;
		if (!RPGHitReaction::IsValidPolicy(Entry.HitReactionPolicy) || !RPGCombatControl::IsValidStunDuration(Entry.StunDuration) || !Entry.Knockback.IsValid()) return false;
		if (!Entry.IsValid()) return true; // Preserve the authored empty heavy-attack entry.
		if (InputTags.Contains(Entry.InputTag)) return false;
		for (const FGameplayAbilitySpec& Existing : ASC.GetActivatableAbilities())
		{
			if (Existing.GetDynamicSpecSourceTags().HasTagExact(Entry.InputTag)) return false;
		}
		InputTags.Add(Entry.InputTag);
		FGameplayAbilitySpec& Spec = Specs.AddDefaulted_GetRef();
		return Entry.BuildAbilitySpec(Level, &Weapon, Spec);
	};
	for (const auto& Entry : Weapon.PlayerWeaponData.DefaultWeaponAbilities) if (!Add(Entry)) return false;
	for (const auto& Entry : Weapon.PlayerWeaponData.SpecialWeaponAbilities) if (!Add(Entry)) return false;
	GrantedASC = &ASC;
	GrantedWeapon = &Weapon;
	GrantedLevel = Level;
	for (const FGameplayAbilitySpec& Spec : Specs) GrantedHandles.Add(ASC.GiveAbility(Spec));
	return true;
}

void URPGPlayerWeaponLoadoutComponent::ReleaseAbilities()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (auto* ASC = GrantedASC.Get())
		{
			for (const auto& Handle : GrantedHandles) ASC->ClearAbility(Handle);
		}
	}
	GrantedHandles.Reset();
	GrantedASC.Reset();
	GrantedWeapon.Reset();
	GrantedItemId.Invalidate();
	GrantedLevel = 0;
}

void URPGPlayerWeaponLoadoutComponent::RetireStartupResources()
{
	const auto* Combat = GetOwner()->FindComponentByClass<UPawnCombatComponent>();
	auto* ASC = GetOwner()->FindComponentByClass<URPGAbilitySystemComponent>();
	if (!Combat) return;
	for (ARPGWeaponBase* Actor : Combat->GetRegisteredStartupWeapons())
	{
		auto* Weapon = Cast<ARPGPlayerWeapon>(Actor);
		if (!IsValid(Weapon)) continue;
		if (!StartupVisibility.Contains(Weapon)) StartupVisibility.Add(Weapon, Weapon->IsHidden());
		Weapon->SetActorHiddenInGame(true);
		if (!ASC) continue;
		TArray<FGameplayAbilitySpecHandle> Preserved;
		for (const auto& Handle : Weapon->GetGrantedAbilitySpecHandles())
		{
			const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
			const auto* ClassOwner = GetOwner()->FindComponentByClass<URPGPlayerClassComponent>();
			if (Spec && Spec->Ability && ClassOwner && ClassOwner->OwnsAbilitySpec(Handle, Spec->Ability->GetClass()))
			{
				Preserved.Add(Handle);
				continue;
			}
			bool bAuthoredWeaponAbility = false;
			if (Spec && Spec->Ability && Spec->SourceObject.Get() == GetOwner())
			{
				for (const auto& Entry : Weapon->PlayerWeaponData.DefaultWeaponAbilities)
					bAuthoredWeaponAbility |= Entry.IsValid() && Spec->Ability->GetClass() == Entry.AbilityToGrant;
				for (const auto& Entry : Weapon->PlayerWeaponData.SpecialWeaponAbilities)
					bAuthoredWeaponAbility |= Entry.IsValid() && Spec->Ability->GetClass() == Entry.AbilityToGrant;
			}
			if (bAuthoredWeaponAbility) ASC->ClearAbility(Handle);
			else if (Spec) Preserved.Add(Handle);
		}
		Weapon->AssignGrantedAbilitySpecHandles(Preserved);
	}
}

void URPGPlayerWeaponLoadoutComponent::RestoreStartupVisibility()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		for (const auto& Pair : StartupVisibility)
			if (auto* Weapon = Pair.Key.Get()) Weapon->SetActorHiddenInGame(Pair.Value);
	}
	StartupVisibility.Reset();
}

void URPGPlayerWeaponLoadoutComponent::RefreshLoadout()
{
	if (bRefreshing || !GetOwner()) return;
	TGuardValue<bool> Guard(bRefreshing, true);
	BindEquipment();
	if (!GetOwner()->HasAuthority()) { RefreshPresentation(); return; }
	auto* Equipment = ObservedEquipment.Get();
	const bool bNative = Equipment && Equipment->HasWeaponSnapshots();
	FRPGEquipmentSnapshot Snapshot;
	auto* Weapon = bNative && Equipment->FindWeaponSnapshot(EWeaponHandType::Count, Snapshot)
		? Cast<ARPGPlayerWeapon>(Snapshot.EquippedActor) : nullptr;
	if (!IsValid(Weapon)) Weapon = nullptr;
	if (bNative) RetireStartupResources();
	else RestoreStartupVisibility();
	auto* ASC = GetOwner()->FindComponentByClass<URPGAbilitySystemComponent>();
	int32 Level = 0;
	const bool bReady = Weapon && ASC && ASC->IsOwnerActorAuthoritative()
		&& ASC->GetAvatarActor() == GetOwner() && ResolveGrantLevel(*ASC, Level);
	if (!bReady || GrantedASC.Get() != ASC || GrantedWeapon.Get() != Weapon
		|| GrantedItemId != Snapshot.ItemId || GrantedLevel != Level)
	{
		ReleaseAbilities();
		if (bReady && GrantWeaponAbilities(*Weapon, *ASC, Level)) GrantedItemId = Snapshot.ItemId;
	}
	// Do not publish usable input/animation for an unready or conflicting loadout.
	ARPGPlayerWeapon* Presented = bReady && GrantedItemId == Snapshot.ItemId ? Weapon : nullptr;
	if (Presentation.bNativeManaged != bNative || Presentation.ActiveWeapon != Presented)
	{
		Presentation.bNativeManaged = bNative;
		Presentation.ActiveWeapon = Presented;
		GetOwner()->ForceNetUpdate();
	}
	RefreshPresentation();
	if (auto* ClassOwner = GetOwner()->FindComponentByClass<URPGPlayerClassComponent>()) ClassOwner->RefreshAppliedClass();
}

void URPGPlayerWeaponLoadoutComponent::OnRep_Presentation()
{
	RefreshPresentation();
}

void URPGPlayerWeaponLoadoutComponent::ApplyInputContext(
	UEnhancedInputLocalPlayerSubsystem* Subsystem, UInputMappingContext* Context)
{
	UEnhancedPlayerInput* PlayerInput = Subsystem ? Subsystem->GetPlayerInput() : nullptr;
	if (InputSubsystem.Get() == Subsystem && InputContext.Get() == Context
		&& BoundPlayerInput.Get() == PlayerInput) return;
	ReleaseInputContext();
	if (!Subsystem || !Context || !PlayerInput) return;
	InputSubsystem = Subsystem;
	BoundPlayerInput = PlayerInput;
	InputContext = Context;
	bHadPreviousInputContext = Subsystem->HasMappingContext(Context, PreviousInputPriority);
	Subsystem->AddMappingContext(Context, 1); // Existing GA_Player_EquipGreatSword priority.
}

void URPGPlayerWeaponLoadoutComponent::ReleaseInputContext()
{
	if (auto* Subsystem = InputSubsystem.Get())
	{
		if (auto* Context = InputContext.Get(); Context && Subsystem->GetPlayerInput() == BoundPlayerInput.Get())
		{
			int32 CurrentPriority = 0;
			if (Subsystem->HasMappingContext(Context, CurrentPriority) && CurrentPriority == 1)
			{
				Subsystem->RemoveMappingContext(Context);
				if (bHadPreviousInputContext) Subsystem->AddMappingContext(Context, PreviousInputPriority);
			}
		}
	}
	InputSubsystem.Reset();
	BoundPlayerInput.Reset();
	InputContext.Reset();
	bHadPreviousInputContext = false;
	PreviousInputPriority = 0;
}

void URPGPlayerWeaponLoadoutComponent::ReleasePresentation()
{
	ReleaseInputContext();
	if (auto* Mesh = LinkedMesh.Get())
		if (LinkedLayer && Mesh->GetAnimInstance()) Mesh->UnlinkAnimClassLayers(LinkedLayer);
	LinkedMesh.Reset();
	LinkedAnimInstance.Reset();
	LinkedLayer = nullptr;
	RetiredStartupAnimInstance.Reset();
	RetiredStartupInputSubsystem.Reset();
}

void URPGPlayerWeaponLoadoutComponent::RetireStartupPresentation()
{
	const auto* Combat = GetOwner()->FindComponentByClass<UPawnCombatComponent>();
	const auto* Character = Cast<ACharacter>(GetOwner());
	const auto* Controller = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	ULocalPlayer* Local = Controller && Controller->IsLocalController() ? Controller->GetLocalPlayer() : nullptr;
	auto* Subsystem = Local ? Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	UAnimInstance* AnimInstance = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	const bool bRetireInput = Subsystem && RetiredStartupInputSubsystem.Get() != Subsystem;
	const bool bRetireAnimation = AnimInstance && RetiredStartupAnimInstance.Get() != AnimInstance;
	if (!Combat) return;
	for (ARPGWeaponBase* Actor : Combat->GetRegisteredStartupWeapons())
	{
		const auto* Weapon = Cast<ARPGPlayerWeapon>(Actor);
		if (!IsValid(Weapon)) continue;
		if (bRetireInput && Weapon->PlayerWeaponData.WeaponInputMappingContext)
			Subsystem->RemoveMappingContext(Weapon->PlayerWeaponData.WeaponInputMappingContext);
		if (bRetireAnimation && Weapon->PlayerWeaponData.WeaponAnimLayerToLink)
			Character->GetMesh()->UnlinkAnimClassLayers(Weapon->PlayerWeaponData.WeaponAnimLayerToLink);
	}
	RetiredStartupInputSubsystem = Subsystem;
	RetiredStartupAnimInstance = AnimInstance;
}

void URPGPlayerWeaponLoadoutComponent::RefreshPresentation()
{
	if (!GetOwner() || bRefreshingPresentation) return;
	TGuardValue<bool> Guard(bRefreshingPresentation, true);
	BindAnimation();
	if (!Presentation.bNativeManaged) { ReleasePresentation(); return; }
	RetireStartupPresentation(); // Retry when possession or the animation instance arrives later.
	const auto* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	const auto* Weapon = IsValid(Presentation.ActiveWeapon) ? Presentation.ActiveWeapon.Get() : nullptr;
	const TSubclassOf<URPGPlayerLinkedAnimLayer> Layer = Weapon ? Weapon->PlayerWeaponData.WeaponAnimLayerToLink : nullptr;
	if (LinkedMesh.Get() != Mesh || LinkedLayer != Layer || LinkedAnimInstance.Get() != AnimInstance)
	{
		if (auto* Previous = LinkedMesh.Get())
			if (LinkedLayer && Previous->GetAnimInstance()) Previous->UnlinkAnimClassLayers(LinkedLayer);
		LinkedMesh = Mesh;
		LinkedAnimInstance = AnimInstance;
		LinkedLayer = Layer;
		if (Mesh && Layer && AnimInstance) Mesh->LinkAnimClassLayers(Layer);
	}
	const auto* Controller = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	ULocalPlayer* Local = Controller && Controller->IsLocalController() ? Controller->GetLocalPlayer() : nullptr;
	ApplyInputContext(Local ? Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr,
		Weapon ? Weapon->PlayerWeaponData.WeaponInputMappingContext.Get() : nullptr);
}
