#include "Component/Equipment/RPGPlayerWeaponLoadoutComponent.h"
#include "Combat/RPGCombatControlTypes.h"

#include "Ability/RPGGameplayAbility.h"
#include "Ability/RPGComboSkillAbility.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
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
#include "Security/RPGDungeonGameplayAuthority.h"
#include "TimerManager.h"

namespace RPGAutoStow
{
const FName HandSocket(TEXT("WeaponHandSocket"));
const FName BackSocket(TEXT("WeaponBackSocket"));
const FName Section(TEXT("Unequip"));
constexpr float IdleSeconds = 10.f;
FGameplayTag EventTag()
{
	return FGameplayTag::RequestGameplayTag(TEXT("Player.Event.Unequip.GreatSword"));
}
}

URPGPlayerWeaponLoadoutComponent::URPGPlayerWeaponLoadoutComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	AbilityLevelSourceInputTag = RPGGameplayTags::InputTag_EquipWeapon;
	StowMontage = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(
		TEXT("/Game/Blueprints/Character/Player/Anim/Montage/AM_GreatSword_Equip.AM_GreatSword_Equip")));
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
	ReleaseAutoStow();
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
	ReleaseAutoStow();
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
	// The authored startup sword also stays usable while carried on the back.
	if (!bNative)
	{
		if (auto* Combat = GetOwner()->FindComponentByClass<UPawnCombatComponent>())
		{
			Weapon = Cast<ARPGPlayerWeapon>(Combat->GetCharacterCurrentEquippedWeapon());
			if (!Weapon)
			{
				Weapon = Cast<ARPGPlayerWeapon>(Combat->GetCharacterHasWeaponByTag(RPGGameplayTags::Player_Weapon_GreatSword));
				if (Weapon) Combat->CurrentEquippedWeaponTag = RPGGameplayTags::Player_Weapon_GreatSword;
			}
		}
	}
	if (!IsValid(Weapon)) Weapon = nullptr;
	if (bNative) RetireStartupResources();
	else RestoreStartupVisibility();
	auto* ASC = GetOwner()->FindComponentByClass<URPGAbilitySystemComponent>();
	if (ASC != EventASC.Get())
	{
		ReleaseAutoStow();
		EventASC = ASC;
		if (ASC) StowEventHandle = ASC->GenericGameplayEventCallbacks.FindOrAdd(RPGAutoStow::EventTag())
			.AddUObject(this, &ThisClass::HandleStowEvent);
	}
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
		if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(AutoStowTimer);
		Presentation.CarryState = ERPGWeaponCarryState::Stowed;
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
	if (!Presentation.bNativeManaged && !Presentation.ActiveWeapon) { ReleasePresentation(); return; }
	if (Presentation.bNativeManaged) RetireStartupPresentation(); // Retry on possession/animation initialization.
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
	ApplyCarryPresentation();
}

bool URPGPlayerWeaponLoadoutComponent::IsAttackAbility(const UGameplayAbility* Ability)
{
	return Ability && (Ability->IsA<URPGComboSkillAbility>() || Ability->IsA<URPGGameplayAbility_SkillContainer>()
		|| Ability->GetAssetTags().HasTag(FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.Attack"))));
}

void URPGPlayerWeaponLoadoutComponent::NotifyAttackStarted(const UGameplayAbility* Ability)
{
	if (!IsAttackAbility(Ability) || !GetOwner() || IsDungeonGameplayRevoked(GetOwner())
		|| !Presentation.ActiveWeapon) return;
	const auto* Pawn = Cast<APawn>(GetOwner());
	if (!GetOwner()->HasAuthority() && (!Pawn || !Pawn->IsLocallyControlled())) return;
	Presentation.CarryState = ERPGWeaponCarryState::Drawn;
	ApplyCarryPresentation(); // Before the attack's Blueprint/montage can open weapon collision.
	if (GetOwner()->HasAuthority())
	{
		GetOwner()->ForceNetUpdate();
		ScheduleAutoStow(RPGAutoStow::IdleSeconds);
	}
}

void URPGPlayerWeaponLoadoutComponent::ScheduleAutoStow(const float Delay)
{
	if (GetWorld()) GetWorld()->GetTimerManager().SetTimer(AutoStowTimer, this, &ThisClass::TryAutoStow, Delay, false);
}

void URPGPlayerWeaponLoadoutComponent::TryAutoStow()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Presentation.ActiveWeapon
		|| Presentation.CarryState != ERPGWeaponCarryState::Drawn || IsDungeonGameplayRevoked(GetOwner())) return;
	const auto* ASC = EventASC.Get();
	if (!ASC || ASC->GetAvatarActor() != GetOwner() || ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death)) return;
	bool bBusy = RPGCombatControl::IsActionBlocked(GetOwner());
	for (const auto& Spec : ASC->GetActivatableAbilities()) bBusy |= Spec.IsActive() && IsAttackAbility(Spec.Ability);
	const auto* Character = Cast<ACharacter>(GetOwner());
	auto* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (bBusy || (Anim && Anim->IsAnyMontagePlaying()))
	{
		ScheduleAutoStow(0.25f); // A long hold/cast/reaction must finish before the idle animation.
		return;
	}
	Presentation.CarryState = ERPGWeaponCarryState::Sheathing;
	ApplyCarryPresentation();
	GetOwner()->ForceNetUpdate();
}

void URPGPlayerWeaponLoadoutComponent::ApplyCarryPresentation()
{
	const auto* Character = Cast<ACharacter>(GetOwner());
	auto* Mesh = Character ? Character->GetMesh() : nullptr;
	auto* Weapon = Presentation.ActiveWeapon.Get();
	if (!Mesh || !IsValid(Weapon)) return;
	auto* Anim = Mesh->GetAnimInstance();
	if (Presentation.CarryState == ERPGWeaponCarryState::Drawn && PlayingStowMontage)
	{
		UAnimMontage* Previous = PlayingStowMontage;
		PlayingStowMontage = nullptr; // Ignore the interrupted end callback on an immediate re-attack.
		if (Anim) Anim->Montage_Stop(0.1f, Previous);
	}
	if (Presentation.CarryState == ERPGWeaponCarryState::Sheathing)
	{
		if (PlayingStowMontage) return;
		UAnimMontage* Montage = Anim ? StowMontage.LoadSynchronous() : nullptr;
		if (Anim && Montage && Montage->GetSectionIndex(RPGAutoStow::Section) != INDEX_NONE)
		{
			PlayingStowMontage = Montage;
			if (Anim->Montage_Play(Montage) > 0.f)
			{
				Anim->Montage_JumpToSection(RPGAutoStow::Section, Montage);
				FOnMontageEnded End;
				End.BindUObject(this, &ThisClass::HandleStowMontageEnded);
				Anim->Montage_SetEndDelegate(End, Montage);
				return; // Authored gameplay-event notify moves the sword during the animation.
			}
			PlayingStowMontage = nullptr;
		}
		if (!GetOwner()->HasAuthority()) return;
		Presentation.CarryState = ERPGWeaponCarryState::Stowed;
		GetOwner()->ForceNetUpdate();
	}
	const FName Socket = Presentation.CarryState == ERPGWeaponCarryState::Drawn ? RPGAutoStow::HandSocket : RPGAutoStow::BackSocket;
	if (Mesh->DoesSocketExist(Socket)) Weapon->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
}

void URPGPlayerWeaponLoadoutComponent::HandleStowEvent(const FGameplayEventData*)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Presentation.CarryState != ERPGWeaponCarryState::Sheathing
		|| !PlayingStowMontage) return;
	Presentation.CarryState = ERPGWeaponCarryState::Stowed;
	ApplyCarryPresentation();
	GetOwner()->ForceNetUpdate();
}

void URPGPlayerWeaponLoadoutComponent::HandleStowMontageEnded(UAnimMontage* Montage, const bool bInterrupted)
{
	if (PlayingStowMontage != Montage) return;
	PlayingStowMontage = nullptr;
	if (!GetOwner() || !GetOwner()->HasAuthority() || Presentation.CarryState != ERPGWeaponCarryState::Sheathing) return;
	Presentation.CarryState = bInterrupted ? ERPGWeaponCarryState::Drawn : ERPGWeaponCarryState::Stowed;
	ApplyCarryPresentation(); // Completion fallback for a montage with a missing/disabled notify.
	GetOwner()->ForceNetUpdate();
	if (bInterrupted) ScheduleAutoStow(0.25f);
}

void URPGPlayerWeaponLoadoutComponent::ReleaseAutoStow()
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(AutoStowTimer);
	if (auto* ASC = EventASC.Get())
		if (auto* Event = ASC->GenericGameplayEventCallbacks.Find(RPGAutoStow::EventTag())) Event->Remove(StowEventHandle);
	EventASC.Reset();
	StowEventHandle.Reset();
	if (auto* Character = Cast<ACharacter>(GetOwner()))
		if (auto* Anim = Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr)
		{
			auto* Previous = PlayingStowMontage.Get();
			PlayingStowMontage = nullptr;
			if (Previous) Anim->Montage_Stop(0.f, Previous);
		}
}
