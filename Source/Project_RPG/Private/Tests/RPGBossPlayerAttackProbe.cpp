#include "Tests/RPGBossMapE2EProbeComponent.h"

#include "AbilitySystemComponent.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBossCharacter.h"
#include "Character/RPGPlayer.h"
#include "Camera/PlayerCameraManager.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Component/Equipment/RPGPlayerWeaponLoadoutComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameInstance/RPGGameInstance.h"
#include "DataTable/SkillData.h"
#include "GameMode/RPGBossBattleGameMode.h"
#include "GameState/RPGBossBattleGameState.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "Manager/DataManager.h"
#include "RPGInputTags.h"
#include "RPGGameplayTags.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Tests/RPGBossAttackScenario.h"
#include "Skill/QuickSlot/RPGQuickSlotInputBindings.h"

namespace
{
	const FRPGBossAttackScenario& AttackScenario() { return RPGBossAttackScenarios::Current(); }
	bool IsChargedAttack() { return AttackScenario().IsCharged(); }
	bool IsChannelingAttack() { return AttackScenario().IsChanneling(); }
	bool IsSkillAttack() { return AttackScenario().IsSkill(); }
	const TCHAR* AttackSkillName() { return AttackScenario().SkillName; }
	const TCHAR* AttackPath() { return AttackScenario().AbilityPath; }
	const TCHAR* AttackMontage() { return AttackScenario().MontageName; }
	FAnimMontageInstance* ObserveChannelMontage(UAnimInstance* Anim, UAnimMontage* Current, int32& InstanceID)
	{
		if (!Anim) return nullptr;
		if (Current && Current->GetName() == AttackMontage())
			if (const auto* Active = Anim->GetActiveInstanceForMontage(Current)) InstanceID = Active->GetInstanceID();
		// GAS clears CurrentMontage on blend-out before the End section completes.
		// Follow the same activation's native instance, never another active montage.
		return InstanceID == INDEX_NONE ? nullptr : Anim->GetMontageInstanceForID(InstanceID);
	}
	FGameplayTag AttackDamageEvent() { return AttackScenario().DamageEvent; }
	FGameplayTag AttackInputTag(const ARPGPlayerWeapon* Weapon)
	{
		return RPGBossAttackScenarios::ResolveInputTag(Weapon, AttackScenario());
	}
	double AttackInputHoldSeconds()
	{
		return FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESingleSwing")) ? .15 : AttackScenario().InputHoldSeconds;
	}
	FGameplayAbilitySpec* FindDefaultAttack(UAbilitySystemComponent* ASC)
	{
		return RPGBossAttackScenarios::FindAbility(ASC, AttackScenario());
	}
	bool HasAttackBinding(const UEnhancedInputComponent* Input, const UInputAction* Action, ETriggerEvent Event)
	{
		if (!Input || !Action) { return false; }
		for (const auto& Binding : Input->GetActionEventBindings())
		{
			if (Binding && Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event) { return true; }
		}
		return false;
	}
	float BossHP(const ARPGBossCharacter* Boss)
	{
		return Boss->GetAbilitySystemComponent()->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	}
}

bool URPGBossMapE2EProbeComponent::PreparePlayerAttackLoadouts()
{
	const URPGGameInstance* GameInstance = Cast<URPGGameInstance>(GetWorld()->GetGameInstance());
	const FRPGSkillDataTable* AttackRow = GameInstance ? GameInstance->GetSkillData(AttackSkillName()) : nullptr;
	if (!AttackRow || !FMath::IsFinite(AttackRow->AttackPower) || AttackRow->AttackPower <= 0.f)
	{
		Fail(TEXT("PlayerAttackGameInstanceSkillDataMissing")); return false;
	}
	UDataManager* Catalog = GetWorld()->GetGameInstance()->GetSubsystem<UDataManager>();
	const URPGItemDefinition* Definition = LoadObject<URPGItemDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Item/Weapons/DA_Item_GreatSword.DA_Item_GreatSword"));
	if (!Catalog || !Definition || !Catalog->FindNativeItemDefinition(Definition->GetPrimaryAssetId()))
	{
		Fail(TEXT("PlayerAttackNativeWeaponDefinitionMissing")); return false;
	}
	bool bReady = true;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ARPGPlayer* Player = Cast<ARPGPlayer>(It->Get()->GetPawn());
		auto* Equipment = Player ? Player->FindComponentByClass<URPGAuthoritativeEquipmentComponent>() : nullptr;
		auto* Loadout = Player ? Player->FindComponentByClass<URPGPlayerWeaponLoadoutComponent>() : nullptr;
		if (!Equipment || !Loadout) { Fail(TEXT("PlayerAttackLoadoutMissing")); return false; }
		if (!Equipment->HasWeaponSnapshots())
		{
			// Explicit opt-in, in-memory equipment fixture. No inventory/backend writes,
			// startup edits, private snapshot injection or direct ability grants.
			if (Equipment->NumAppliedItems() != 0) { Fail(TEXT("PlayerAttackRefusesExistingEquipmentOverwrite")); return false; }
			FRPGItemOwnerRef Owner;
			Owner.Type = ERPGItemOwnerType::Character;
			Owner.OwnerId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
			FRPGItemLocation Location;
			Location.ContainerType = ERPGItemContainerType::Equipment;
			Location.ContainerId = Owner.OwnerId;
			Location.SlotIndex = int32(EEquipmentSlotType::Weapon_Primary_R);
			FRPGItemInstanceState Instance;
			FRPGItemRecord Record;
			FString Error;
			if (!FRPGItemInstanceState::TryRestore(FGuid::NewGuid(), 0, 1, {}, {}, Instance) ||
				!FRPGItemRecord::TryRestore(Definition->GetPrimaryAssetId(), Definition->GetDefinitionVersion(),
					Owner, Location, Instance, 1, ERPGItemLifecycleState::Active, {}, Record) ||
				!Equipment->Reconcile(Owner, {Record}, *Catalog, &Error))
			{
				UE_LOG(LogTemp, Error, TEXT("RAIDMAP_E2E_ATTACK_EQUIPMENT_ERROR %s"), *Error);
				Fail(TEXT("PlayerAttackEquipmentFixtureRejected")); return false;
			}
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_ATTACK_FIXTURE Pawn=%s NativeReconcile=1 BackendWrites=0 DirectAbilityGrant=0 GameInstance=%s AuthoredDamage=%g"),
				*Player->GetName(), *GameInstance->GetClass()->GetPathName(), AttackRow->AttackPower);
		}
		const FGameplayAbilitySpec* Spec = FindDefaultAttack(Player->GetAbilitySystemComponent());
		const FGameplayTag Prerequisite = AttackScenario().PrerequisiteTag;
		if (Prerequisite.IsValid() && !Player->GetAbilitySystemComponent()->HasMatchingGameplayTag(Prerequisite))
		{
			// Isolated prerequisite fixture, as in existing skill network probes.
			// Do not bypass CanActivateAbility or claim a real Rage acquisition flow.
			Player->GetAbilitySystemComponent()->AddLooseGameplayTag(Prerequisite, 1,
				EGameplayTagReplicationState::CountToOwner);
			PlayerAttackRageFixtureASCs.Add(Player->GetAbilitySystemComponent());
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CHARGE_RAGE_FIXTURE Server=1 ReplicatedToOwner=1 RealRageActivation=0 Pawn=%s"), *Player->GetName());
		}
		ARPGPlayerWeapon* Weapon = Player->GetPlayerCombatComponent()->GetPlayerCurrentEquippedWeapon();
		const FGameplayTag InputTag = AttackInputTag(Weapon);
		if (IsSkillAttack() && Weapon && !InputTag.IsValid()) { Fail(TEXT("SelectedSkillMissingFromAuthoredWeaponLoadout")); return false; }
		bReady &= Loadout->IsNativeManaged() && Weapon && Spec && Spec->SourceObject.Get() == Weapon &&
			InputTag.IsValid() && Spec->GetDynamicSpecSourceTags().HasTagExact(InputTag);
		if (bSummonPlayerKillScenario && Weapon && Spec)
		{
			UQuickSlotComponent* Slots = Player->GetQuickSlotComponent();
			const FGameplayTag SkillTag = RPGBossAttackScenarios::ResolveSkillTag(Player->GetAbilitySystemComponent(), AttackScenario());
			const UDataAsset_InputConfig* Config = LoadObject<UDataAsset_InputConfig>(nullptr,
				TEXT("/Game/Blueprints/Character/Player/Input/DA_InputConfig.DA_InputConfig"));
			if (!Slots || !SkillTag.IsValid() || !Config) { Fail(TEXT("SummonPlayerKillQuickSlotSetupMissing")); return false; }
			int32 SlotIndex = INDEX_NONE;
			for (const auto& Binding : FRPGQuickSlotInputBindings::Read(Config))
			{
				if (!Binding.bIsSkillSlot || !Binding.Action || Binding.Action->ValueType != EInputActionValueType::Boolean) { continue; }
				const FRPGQuickSlotContent* Content = Slots->GetSkillSlotContent(Binding.SlotIndex);
				if (!Content) { continue; }
				if (Content->AbilityTag == SkillTag) { SlotIndex = Binding.SlotIndex; break; }
				if (SlotIndex == INDEX_NONE && Content->IsEmpty()) { SlotIndex = Binding.SlotIndex; }
			}
			// Explicit development registration through the normal owned-skill API.
			// Reuse an existing registration or an empty slot; never replace another skill.
			if (SlotIndex == INDEX_NONE || !Slots->CanAssignSkillSlot(SlotIndex, SkillTag))
			{ Fail(TEXT("SummonPlayerKillNoAssignableQuickSlot")); return false; }
			Slots->SetSkillSlot(SlotIndex, SkillTag);
			const FRPGQuickSlotContent* Assigned = Slots->GetSkillSlotContent(SlotIndex);
			if (!Assigned || Assigned->AbilityTag != SkillTag) { Fail(TEXT("SummonPlayerKillQuickSlotRegistrationRejected")); return false; }
		}
	}
	return bReady;
}

void URPGBossMapE2EProbeComponent::CleanupPlayerAttackProbe()
{
	CleanupSummonPlayerTargetObservation();
	for (const auto& ASC : PlayerAttackRageFixtureASCs)
	{
		if (ASC.IsValid()) ASC->RemoveLooseGameplayTag(AttackScenario().PrerequisiteTag, 1,
			EGameplayTagReplicationState::CountToOwner);
	}
	PlayerAttackRageFixtureASCs.Reset();
	if (PlayerAttackInputSubsystem.IsValid())
	{
		if (PlayerAttackMoveAction.IsValid()) { PlayerAttackInputSubsystem->StopContinuousInputInjectionForAction(PlayerAttackMoveAction.Get()); }
		if (PlayerAttackAction.IsValid()) { PlayerAttackInputSubsystem->StopContinuousInputInjectionForAction(PlayerAttackAction.Get()); }
	}
	if (PlayerAttackASC.IsValid()) { PlayerAttackASC->OnAbilityEnded.Remove(PlayerAttackEndedHandle); }
	if (PlayerAttackASC.IsValid())
	{
		if (auto* Events = PlayerAttackASC->GenericGameplayEventCallbacks.Find(AttackDamageEvent()))
		{
			Events->Remove(PlayerAttackHitEventHandle);
		}
	}
	if (OriginalBoss.IsValid()) { OriginalBoss->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(PlayerAttackEffectHandle); }
	PlayerAttackEndedHandle.Reset();
	PlayerAttackEffectHandle.Reset();
	PlayerAttackHitEventHandle.Reset();
}

void URPGBossMapE2EProbeComponent::HandlePlayerAttackEnded(const FAbilityEndedData& Data)
{
	if (Data.AbilitySpecHandle == PlayerAttackHandle) { bPlayerAttackEndedNaturally = !Data.bWasCancelled; }
}

void URPGBossMapE2EProbeComponent::TickPlayerAttackLocalInput()
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController() || !PC->GetLocalPlayer()) { return; }
	ARPGPlayer* Player = Cast<ARPGPlayer>(PC->GetPawn());
	UAbilitySystemComponent* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
	FGameplayAbilitySpec* Spec = FindDefaultAttack(ASC);
	ARPGPlayerWeapon* Weapon = Player ? Player->GetPlayerCombatComponent()->GetPlayerCurrentEquippedWeapon() : nullptr;
	if (!Spec || !Weapon || Spec->SourceObject.Get() != Weapon || ASC->GetAvatarActor() != Player) { return; }
	if (AttackScenario().PrerequisiteTag.IsValid() && !bPlayerAttackInputComplete &&
		!ASC->HasMatchingGameplayTag(AttackScenario().PrerequisiteTag)) { return; }
	if (!PlayerAttackInputSubsystem.IsValid())
	{
		const UDataAsset_InputConfig* Config = LoadObject<UDataAsset_InputConfig>(nullptr,
			TEXT("/Game/Blueprints/Character/Player/Input/DA_InputConfig.DA_InputConfig"));
		auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
		const auto* Input = Cast<UEnhancedInputComponent>(PC->InputComponent);
		const UInputAction* Move = Config ? Config->FindNativeInputActionByTag(RPGGameplayTags::InputTag_Move) : nullptr;
		const UInputAction* Action = Config ? Config->FindAbilityInputActionByTag(AttackInputTag(Weapon)) : nullptr;
		if (bSummonPlayerKillScenario)
		{
			Action = nullptr;
			const UQuickSlotComponent* Slots = Player->GetQuickSlotComponent();
			const FGameplayTag SkillTag = RPGBossAttackScenarios::ResolveSkillTag(ASC, AttackScenario());
			if (!Slots || !SkillTag.IsValid()) { return; }
			for (const auto& Binding : FRPGQuickSlotInputBindings::Read(Config))
			{
				const FRPGQuickSlotContent* Content = Binding.bIsSkillSlot ? Slots->GetSkillSlotContent(Binding.SlotIndex) : nullptr;
				if (Content && Content->AbilityTag == SkillTag) { Action = Binding.Action; break; }
			}
			// Owner-only slot replication must arrive before acknowledging input readiness.
			if (!Action) { return; }
		}
		if (!Input || !Subsystem || !Subsystem->HasMappingContext(Weapon->PlayerWeaponData.WeaponInputMappingContext)) { return; }
		if (!Move || !Action || Action->ValueType != EInputActionValueType::Boolean ||
			!HasAttackBinding(Input, Move, ETriggerEvent::Triggered) || !HasAttackBinding(Input, Action, ETriggerEvent::Started) ||
			!HasAttackBinding(Input, Action, ETriggerEvent::Completed)) { Fail(TEXT("PlayerAttackAuthoredInputUnbound")); return; }
		PlayerAttackInputSubsystem = Subsystem;
		PlayerAttackMoveAction = Move;
		PlayerAttackAction = Action;
		if (bSummonPlayerKillScenario)
		{
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_PLAYER_SLOT_READY OwnedSkillRegistered=1 OwnerSlotReplicated=1 EnhancedInput=1"));
		}
	}
	if (!bPlayerAttackReadyReported)
	{
		bPlayerAttackReadyReported = true;
		ServerConfirmPlayerAttack(0);
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_ATTACK_INPUT_READY NativeWeapon=1 Mapping=1 BoundAttack=1"));
		if (IsChargedAttack()) { UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CHARGE_INPUT_READY RagePrerequisite=1")); }
	}
	if (bPlayerAttackInputComplete) { ServerConfirmPlayerAttack(2); return; }
	if (!bPlayerAttackRequested) { return; }
	if (Player != PlayerAttackTarget.Get() || !OriginalBoss.IsValid()) { Fail(TEXT("PlayerAttackLocalContextLost")); return; }
	const double Now = FPlatformTime::Seconds();
	if (PlayerAttackInputStart == 0.0)
	{
		PlayerAttackASC = ASC;
		PlayerAttackHandle = Spec->Handle;
		PlayerAttackEndedHandle = ASC->OnAbilityEnded.AddUObject(this, &ThisClass::HandlePlayerAttackEnded);
		PlayerAttackInputStart = Now;
	}
	if (!bPlayerAttackInputSent)
	{
		ACharacter* Victim = PlayerAttackVictim.Get();
		if (!IsValid(Victim)) { Fail(TEXT("PlayerAttackVictimLostBeforeInput")); return; }
		const FVector Toward = (Victim->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D();
		const FRotationMatrix Basis(FRotator(0.f, PC->GetControlRotation().Yaw, 0.f));
		const FVector2D Value(FVector::DotProduct(Toward, Basis.GetUnitAxis(EAxis::Y)), FVector::DotProduct(Toward, Basis.GetUnitAxis(EAxis::X)));
		PlayerAttackInputSubsystem->StartContinuousInputInjectionForAction(PlayerAttackMoveAction.Get(), FInputActionValue(Value), {}, {});
		const float Distance = FVector::Dist2D(Player->GetActorLocation(), Victim->GetActorLocation());
		const float Reach = Player->GetCapsuleComponent()->GetScaledCapsuleRadius() +
			Victim->GetCapsuleComponent()->GetScaledCapsuleRadius() + 10.f;
		const float Facing = FVector::DotProduct(Player->GetActorForwardVector().GetSafeNormal2D(), Toward);
		if (Distance > Reach || Facing < .95f || Now - PlayerAttackInputStart < .2)
		{
			if (bSummonPlayerKillScenario) PlayerAttackAimStart = 0.0;
			return;
		}
		if (IsSkillAttack())
		{
			if (bSummonPlayerKillScenario)
			{
				// Do not keep walking through a moving victim during aim settlement.
				// A new approach must earn a fresh stationary aim window.
				PlayerAttackInputSubsystem->StopContinuousInputInjectionForAction(PlayerAttackMoveAction.Get());
				if (Player->GetVelocity().SizeSquared2D() > FMath::Square(10.f)) { PlayerAttackAimStart = 0.0; return; }
			}
			// Use normal control aim: ground for JumpSmash, victim center for soft target.
			// Allow movement replication before authored target-data submission.
			FVector ViewOrigin; FRotator ViewRotation;
			PC->GetPlayerViewPoint(ViewOrigin, ViewRotation);
			FVector AimPoint = Victim->GetActorLocation();
			if (AttackScenario().bAimAtGround) AimPoint -= FVector::UpVector * Victim->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const FRotator Aim = (AimPoint - ViewOrigin).Rotation();
			PC->SetControlRotation(Aim);
			if (bSummonPlayerKillScenario)
			{
				APlayerCameraManager* Camera = PC->PlayerCameraManager;
				if (!IsValid(Camera) || !Camera->bUseClientSideCameraUpdates)
				{ Fail(TEXT("SummonPlayerKillClientCameraUpdatesUnavailable")); return; }
				// SetControlRotation alone does not dirty the replicated camera while
				// stationary. Let the normal camera tick send the real cached POV;
				// do not overwrite server aim or widen its security tolerances.
				Camera->bShouldSendClientSideCameraUpdate = true;
				FVector CurrentOrigin; FRotator CurrentView;
				PC->GetPlayerViewPoint(CurrentOrigin, CurrentView);
				if (!RPGBossAttackScenarios::HasLiveAimCandidate(ASC, AttackScenario(), Victim, CurrentView.Vector()))
				{
					PlayerAttackAimStart = 0.0;
					if (Now >= PlayerAttackDiagnosticAt)
					{
						PlayerAttackDiagnosticAt = Now + 2.0;
						UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_PLAYER_AIM_WAIT LiveVictimCandidate=0 InputNotSent=1"));
					}
					return;
				}
			}
			if (PlayerAttackAimStart == 0.0) PlayerAttackAimStart = Now;
			if (Now - PlayerAttackAimStart < .3) return;
		}
		bPlayerAttackInputSent = true;
		PlayerAttackPressStart = Now;
		PlayerAttackInputSubsystem->StopContinuousInputInjectionForAction(PlayerAttackMoveAction.Get());
		PlayerAttackInputSubsystem->StartContinuousInputInjectionForAction(PlayerAttackAction.Get(), FInputActionValue(true), {}, {});
		if (bSummonPlayerKillScenario)
		{
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_PLAYER_AIM_SYNC CameraUpdateRequested=1 StationaryAim=1 ServerAimUnmodified=1"));
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_PLAYER_AIM_CANDIDATE_READY LiveVictim=1 AuthoredFilter=1"));
		}
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_ATTACK_INPUT EnhancedInput=1 ManualActivation=0 ApproachInput=1 Distance=%g Reach=%g Facing=%g HoldSeconds=%g"), Distance, Reach, Facing, AttackInputHoldSeconds());
	}
	// Exercise the authored held-input combo, which stops at its final section.
	// A first swing may spatially miss; never synthesize a hit or enlarge its box.
	if (!IsChargedAttack() && !IsChannelingAttack() && Now - PlayerAttackPressStart >= AttackInputHoldSeconds()) { PlayerAttackInputSubsystem->StopContinuousInputInjectionForAction(PlayerAttackAction.Get()); }
	bPlayerAttackObservedActive |= Spec->IsActive();
	UAnimMontage* Montage = ASC->GetCurrentMontage();
	UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance();
	FAnimMontageInstance* ChannelInstance = IsChannelingAttack() ? ObserveChannelMontage(Anim, Montage, PlayerAttackChannelMontageInstanceID) : nullptr;
	bPlayerAttackObservedMontage |= Spec->IsActive() && Anim && Montage && Montage->GetName() == AttackMontage() && Anim->Montage_GetPosition(Montage) > .02f;
	if (IsChargedAttack() && Spec->IsActive())
	{
		FRPGBossAttackChargeSnapshot Config;
		if (!AttackScenario().ChargeObservation->ReadCharge(Spec->GetPrimaryInstance(), Config)) { Fail(TEXT("ChargeSlashFrozenPolicyMissing")); return; }
		const float TimePerLevel = Config.TimePerLevel;
		if (!FMath::IsFinite(TimePerLevel) || TimePerLevel <= 0.f) { Fail(TEXT("ChargeSlashTimingInvalid")); return; }
		const double WorldTime = GetWorld()->GetTimeSeconds();
		if (PlayerAttackChargeStart == 0.0) PlayerAttackChargeStart = WorldTime;
		const double HoldSeconds = TimePerLevel * (FMath::Max(1, Config.MinimumReleaseLevel) + .35);
		if (HoldSeconds >= TimePerLevel * Config.MaxChargeLevel + Config.MaxHoldTime) { Fail(TEXT("ChargeSlashNoManualReleaseWindow")); return; }
		const FName Section = Anim && Montage ? Anim->Montage_GetCurrentSection(Montage) : NAME_None;
		bPlayerAttackChargeHeld |= Montage && Montage->GetName() == AttackMontage() && !Section.IsNone() &&
			Section != Config.ReleaseSection && Config.bInputPressed;
		if (Section == Config.ReleaseSection)
		{
			if (!bPlayerAttackChargeInputReleased) { Fail(TEXT("ChargeSlashAutoReleaseInsteadOfInput")); return; }
			bPlayerAttackChargeReleased = true;
		}
		if (!bPlayerAttackChargeInputReleased && WorldTime - PlayerAttackChargeStart >= HoldSeconds)
		{
			if (!bPlayerAttackChargeHeld || BossHP(OriginalBoss.Get()) != PlayerAttackBossHealth) { Fail(TEXT("ChargeSlashMissingHoldOrPrematureDamage")); return; }
			bPlayerAttackChargeInputReleased = true;
			PlayerAttackInputSubsystem->StopContinuousInputInjectionForAction(PlayerAttackAction.Get());
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CHARGE_INPUT_RELEASE EnhancedInput=1 FrozenTiming=1 HeldSeconds=%g TimePerLevel=%g"), WorldTime - PlayerAttackChargeStart, TimePerLevel);
		}
	}
	if (IsChannelingAttack() && Spec->IsActive() && !bPlayerAttackEndedNaturally && !bPlayerAttackChannelStopped)
	{
		FRPGBossAttackChannelSnapshot Channel;
		if (!AttackScenario().ChannelObservation->ReadChannel(Spec->GetPrimaryInstance(), ChannelInstance, Channel)) { Fail(TEXT("ChannelFrozenPolicyOrTimingMissing")); return; }
		const double WorldTime = GetWorld()->GetTimeSeconds();
		if (PlayerAttackChannelStart == 0.0) PlayerAttackChannelStart = WorldTime;
		const FName Section = ChannelInstance->GetCurrentSection();
		bPlayerAttackChannelHeld |= AttackScenario().ObservedHoldSections.Contains(Section) && Channel.bInputPressed;
		if (Section == Channel.StopSection)
		{
			if (!bPlayerAttackChannelInputReleased) { Fail(TEXT("ChannelAutoStopInsteadOfInput")); return; }
			bPlayerAttackChannelStopped = true;
		}
		if (!bPlayerAttackChannelInputReleased && WorldTime - PlayerAttackChannelStart >= Channel.ManualHoldSeconds)
		{
			if (!bPlayerAttackChannelHeld || !Channel.bInputPressed) { Fail(TEXT("ChannelLocalHoldMissing")); return; }
			bPlayerAttackChannelInputReleased = true;
			PlayerAttackInputSubsystem->StopContinuousInputInjectionForAction(PlayerAttackAction.Get());
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CHANNEL_INPUT_RELEASE EnhancedInput=1 FrozenTiming=1 HeldSeconds=%g LoopSeconds=%g"), WorldTime - PlayerAttackChannelStart, Channel.LoopSeconds);
		}
	}
	if (bPlayerAttackObservedActive && !Spec->IsActive())
	{
		if (IsChargedAttack() && (!bPlayerAttackChargeHeld || !bPlayerAttackChargeReleased || !bPlayerAttackChargeInputReleased)) { Fail(TEXT("ChargeSlashLocalHoldReleaseMissing")); return; }
		if (IsChannelingAttack() && (!bPlayerAttackChannelHeld || !bPlayerAttackChannelStopped || !bPlayerAttackChannelInputReleased)) { Fail(TEXT("ChannelLocalManualStopMissing")); return; }
		if (!bPlayerAttackEndedNaturally || !bPlayerAttackObservedMontage) { Fail(TEXT("PlayerAttackLocalLifetimeFailed")); return; }
		bPlayerAttackInputComplete = true;
		CleanupPlayerAttackProbe();
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_ATTACK_INPUT_COMPLETE GAS=1 Montage=1 NaturalEnd=1"));
		ServerConfirmPlayerAttack(2);
	}
}

void URPGBossMapE2EProbeComponent::ClientBeginPlayerAttack_Implementation(APawn* Target, float BossHealth, APawn* Victim)
{
	if (!bEnabled || (!bPlayerAttackScenario && !bSummonPlayerKillScenario) || !Target ||
		!IsValid(Victim) || !FMath::IsFinite(BossHealth) || BossHealth <= 0.f) { return; }
	PlayerAttackTarget = Target;
	PlayerAttackBossHealth = BossHealth;
	PlayerAttackChannelClientHP = BossHealth;
	OriginalBoss = GetWorld()->GetGameState<ARPGBossBattleGameState>()->GetBattleSnapshot().Boss;
	PlayerAttackVictim = Cast<ACharacter>(Victim);
	if (!PlayerAttackVictim.IsValid() || !OriginalBoss.IsValid() ||
		(bSummonPlayerKillScenario ? Victim->GetOwner() != OriginalBoss.Get() : Victim != OriginalBoss.Get()))
	{ Fail(TEXT("PlayerAttackUnexpectedVictim")); return; }
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	bPlayerAttackRequested = PC && PC->GetPawn() == Target;
}

void URPGBossMapE2EProbeComponent::OnPlayerAttackEffectApplied(UAbilitySystemComponent* SourceASC,
	const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle)
{
	if (!bEnabled || bFinished || !bPlayerAttackScenario || !bPlayerAttackRequested || !OriginalBoss.IsValid()) { return; }
	// The normal hit also triggers the boss's self-applied hit-reaction GE. It
	// is not another damage source; only damage specs belong to this checkpoint.
	if (!Spec.SetByCallerTagMagnitudes.Contains(RPGGameplayTags::Shared_SetByCaller_BaseDamage)) { return; }
	const UGameplayAbility* Ability = Spec.GetContext().GetAbility();
	if (SourceASC != PlayerAttackASC.Get() || !Ability || Ability->GetClass()->GetPathName() != AttackPath() ||
		Spec.GetContext().GetInstigatorAbilitySystemComponent() != SourceASC) { Fail(TEXT("PlayerAttackUnexpectedDamageSource")); return; }
	ARPGPlayer* Player = Cast<ARPGPlayer>(PlayerAttackTarget.Get());
	if (IsChannelingAttack())
	{
		FRPGBossAttackChannelSnapshot Channel;
		if (!Player || bPlayerAttackChannelStopped || PlayerAttackChannelQuietStart > 0.0 ||
			!AttackScenario().ChannelObservation->ReadChannel(Spec.GetContext().GetAbilityInstance_NotReplicated(),
				ObserveChannelMontage(Player->GetMesh()->GetAnimInstance(), SourceASC->GetCurrentMontage(), PlayerAttackChannelMontageInstanceID), Channel) || !Channel.bInputPressed)
		{ Fail(TEXT("ChannelDamageAfterInputStop")); return; }
		const double WorldTime = GetWorld()->GetTimeSeconds();
		const int32 HitsInFrame = PlayerAttackChannelHitFrame == GFrameCounter ? PlayerAttackChannelHitsInFrame + 1 : 1;
		// Dispatch time is not notify time: a long animation update can queue multiple loops.
		if (!RPGBossAttackScenarios::AcceptsChannelCadence(PlayerAttackDamageCount > 0 ? WorldTime - PlayerAttackChannelLastHit : Channel.LoopSeconds,
			PlayerAttackChannelPreviousAdvance, Channel.FrameAdvanceSeconds, Channel.LoopSeconds, HitsInFrame))
		{
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CHANNEL_CADENCE_REJECT Delta=%g PreviousAdvance=%g FrameAdvance=%g LoopSeconds=%g HitsInFrame=%d"),
				WorldTime - PlayerAttackChannelLastHit, PlayerAttackChannelPreviousAdvance, Channel.FrameAdvanceSeconds, Channel.LoopSeconds, HitsInFrame);
			Fail(TEXT("ChannelDuplicateDamageWithinLoop")); return;
		}
		PlayerAttackChannelHitFrame = GFrameCounter;
		PlayerAttackChannelHitsInFrame = HitsInFrame;
		PlayerAttackChannelPreviousAdvance = Channel.FrameAdvanceSeconds;
		PlayerAttackChannelLastHit = WorldTime;
		PlayerAttackChannelLoopSeconds = Channel.LoopSeconds;
	}
	ARPGPlayerWeapon* Weapon = Player ? Player->GetPlayerCombatComponent()->GetPlayerCurrentEquippedWeapon() : nullptr;
	UBoxComponent* Box = Weapon ? Weapon->GetWeaponCollisionBox() : nullptr;
	const float After = BossHP(OriginalBoss.Get());
	const float Before = bPlayerAttackSawDamage ? PlayerAttackBossAfter : PlayerAttackBossHealth;
	FHitResult SweepHit;
	const bool bSweep = Player && Player->GetPlayerCombatComponent()->TryGetCurrentServerWeaponHit(OriginalBoss.Get(), SweepHit);
	const bool bOverlap = Box && Box->IsOverlappingActor(OriginalBoss.Get());
	bool bSkillQuery = false;
	if (IsSkillAttack())
	{
		bSkillQuery = AttackScenario().HitEvidence->ContainsServerHit(Spec, OriginalBoss.Get());
		if (IsChargedAttack())
		{
			FRPGBossAttackChargeSnapshot Charge;
			if (!bPlayerAttackChargeHeld || !bPlayerAttackChargeReleased ||
				!AttackScenario().ChargeObservation->ReadCharge(Spec.GetContext().GetAbilityInstance_NotReplicated(), Charge) || Charge.bInputPressed)
			{ Fail(TEXT("ChargeSlashDamageBeforeServerRelease")); return; }
		}
	}
	if ((IsSkillAttack() ? !bSkillQuery : (!Box || Box->GetCollisionEnabled() != ECollisionEnabled::QueryOnly || (!bOverlap && !bSweep))) ||
		!FMath::IsFinite(After) || After >= Before) { Fail(TEXT("PlayerAttackMissingServerWeaponHit")); return; }
	++PlayerAttackDamageCount;
	const int32 ExpectedDamageCount = FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESingleSwing")) ? 1 : AttackScenario().ExpectedDamageCount;
	if (ExpectedDamageCount > 0 && PlayerAttackDamageCount > ExpectedDamageCount)
	{
		Fail(TEXT("PlayerAttackSingleSwingDuplicateDamage")); return;
	}
	bPlayerAttackSawDamage = true;
	PlayerAttackBossAfter = After;
	if (IsSkillAttack())
	{
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SKILL_SERVER_HIT Skill=%s AuthoredAbilityContext=1 ServerHitQuery=1 Before=%g After=%g HitCount=%d"), AttackSkillName(), Before, After, PlayerAttackDamageCount);
	}
	else { UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_ATTACK_SERVER_HIT AuthoredAbilityContext=1 ServerWeaponOverlap=%d ServerWeaponSweep=%d Before=%g After=%g HitCount=%d"), bOverlap, bSweep, Before, After, PlayerAttackDamageCount); }
}

void URPGBossMapE2EProbeComponent::TickPlayerAttackServer()
{
	ARPGBossCharacter* Boss = OriginalBoss.Get();
	if (!Boss || !bSawCombatDamage || CombatClients.Num() != 2) { return; }
	if (!bPlayerAttackRequested)
	{
		ARPGPlayer* Player = Cast<ARPGPlayer>(Boss->BossThreatComponent->GetCurrentTarget());
		APlayerController* TargetPC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
		const FRPGBossTelegraphState Telegraph = Boss->BossAIComponent->GetTelegraphState();
		if (!TargetPC || Telegraph.bActive) { return; }
		FGameplayAbilitySpec* Spec = FindDefaultAttack(Player->GetAbilitySystemComponent());
		if (!Spec || Spec->IsActive()) { Fail(TEXT("PlayerAttackSpecNotReady")); return; }
		const auto* Security = Player->FindComponentByClass<URPGSecurityValidationComponent>();
		if (!Security) { Fail(TEXT("PlayerAttackSecurityMissing")); return; }
		PlayerAttackTarget = Player;
		PlayerAttackASC = Player->GetAbilitySystemComponent();
		PlayerAttackHandle = Spec->Handle;
		PlayerAttackBossHealth = BossHP(Boss);
		PlayerAttackThreatBefore = Boss->BossThreatComponent->GetThreatForTarget(Player);
		PlayerAttackViolationBaseline = Security->GetTotalViolationCount();
		PlayerAttackEndedHandle = PlayerAttackASC->OnAbilityEnded.AddUObject(this, &ThisClass::HandlePlayerAttackEnded);
		PlayerAttackHitEventHandle = PlayerAttackASC->GenericGameplayEventCallbacks.FindOrAdd(AttackDamageEvent()).AddWeakLambda(this,
			[this](const FGameplayEventData* Data)
			{
				UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_ATTACK_HIT_EVENT Target=%s Instigator=%s"),
					Data ? *GetNameSafe(Data->Target.Get()) : TEXT("None"), Data ? *GetNameSafe(Data->Instigator.Get()) : TEXT("None"));
			});
		PlayerAttackEffectHandle = Boss->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddUObject(this, &ThisClass::OnPlayerAttackEffectApplied);
		bPlayerAttackRequested = true;
		for (const auto& PC : ReadyClients) { PC->FindComponentByClass<URPGBossMapE2EProbeComponent>()->ClientBeginPlayerAttack(Player, PlayerAttackBossHealth, Boss); }
		if (TargetPC->IsLocalController()) { TargetPC->FindComponentByClass<URPGBossMapE2EProbeComponent>()->ClientBeginPlayerAttack(Player, PlayerAttackBossHealth, Boss); }
		Deadline = FPlatformTime::Seconds() + 15.0;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_ATTACK_SERVER_REQUEST Target=%s Host=%d ManualDamage=0 BrainRunning=1"), *Player->GetName(), int32(TargetPC->IsLocalController()));
	}
	ARPGPlayer* Player = Cast<ARPGPlayer>(PlayerAttackTarget.Get());
	const FGameplayAbilitySpec* Spec = PlayerAttackASC.IsValid() ? PlayerAttackASC->FindAbilitySpecFromHandle(PlayerAttackHandle) : nullptr;
	const URPGSecurityValidationComponent* Security = Player ? Player->FindComponentByClass<URPGSecurityValidationComponent>() : nullptr;
	if (!Spec || !Security || Security->GetTotalViolationCount() != PlayerAttackViolationBaseline) { Fail(TEXT("PlayerAttackSecurityOrSpecChanged")); return; }
	if (Spec->IsActive() && !bPlayerAttackObservedActive) { PlayerAttackDiagnosticAt = 0.0; }
	bPlayerAttackObservedActive |= Spec->IsActive();
	UAnimMontage* Montage = PlayerAttackASC->GetCurrentMontage();
	UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance();
	FAnimMontageInstance* ChannelInstance = IsChannelingAttack() ? ObserveChannelMontage(Anim, Montage, PlayerAttackChannelMontageInstanceID) : nullptr;
	bPlayerAttackObservedMontage |= Spec->IsActive() && Montage && Anim && Montage->GetName() == AttackMontage() && Anim->Montage_GetPosition(Montage) > .02f;
	if (IsChargedAttack() && Spec->IsActive() && Anim && Montage && Montage->GetName() == AttackMontage())
	{
		FRPGBossAttackChargeSnapshot Config;
		if (!AttackScenario().ChargeObservation->ReadCharge(Spec->GetPrimaryInstance(), Config)) { Fail(TEXT("ChargeSlashServerConfigMissing")); return; }
		const FName Section = Anim->Montage_GetCurrentSection(Montage);
		if (!bPlayerAttackChargeHeld && !Section.IsNone() && Section != Config.ReleaseSection && Config.bInputPressed)
		{
			if (BossHP(Boss) != PlayerAttackBossHealth) { Fail(TEXT("ChargeSlashPrematureServerDamage")); return; }
			bPlayerAttackChargeHeld = true;
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CHARGE_SERVER_HOLD InputPressed=1 BossHPUnchanged=1"));
		}
		if (!bPlayerAttackChargeReleased && Section == Config.ReleaseSection)
		{
			if (!bPlayerAttackChargeHeld || Config.bInputPressed) { Fail(TEXT("ChargeSlashServerReleaseInputMissing")); return; }
			bPlayerAttackChargeReleased = true;
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CHARGE_SERVER_RELEASE InputReleased=1 AttackSection=1"));
		}
	}
	if (IsChannelingAttack() && Spec->IsActive() && !bPlayerAttackEndedNaturally && !bPlayerAttackChannelStopped && ChannelInstance)
	{
		FRPGBossAttackChannelSnapshot Channel;
		if (!AttackScenario().ChannelObservation->ReadChannel(Spec->GetPrimaryInstance(), ChannelInstance, Channel)) { Fail(TEXT("ChannelServerConfigMissing")); return; }
		PlayerAttackChannelLoopSeconds = Channel.LoopSeconds;
		const FName Section = ChannelInstance->GetCurrentSection();
		if (!bPlayerAttackChannelHeld && AttackScenario().ObservedHoldSections.Contains(Section) && Channel.bInputPressed)
		{
			bPlayerAttackChannelHeld = true;
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CHANNEL_SERVER_HOLD InputPressed=1 LoopSection=1"));
		}
		if (!bPlayerAttackChannelStopped && Section == Channel.StopSection)
		{
			if (!bPlayerAttackChannelHeld || Channel.bInputPressed || PlayerAttackDamageCount < 3) { Fail(TEXT("ChannelServerManualStopOrRepeatedHitsMissing")); return; }
			bPlayerAttackChannelStopped = true;
			PlayerAttackChannelStopHP = BossHP(Boss);
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CHANNEL_SERVER_STOP InputReleased=1 EndSection=1 RepeatedHits=1 HitCount=%d"), PlayerAttackDamageCount);
		}
	}
	if (IsChannelingAttack() && bPlayerAttackChannelStopped)
	{
		if (BossHP(Boss) != PlayerAttackChannelStopHP) { Fail(TEXT("ChannelBossHPChangedAfterStop")); return; }
		if (!Spec->IsActive() && PlayerAttackChannelQuietStart == 0.0) PlayerAttackChannelQuietStart = GetWorld()->GetTimeSeconds();
	}
	if (FPlatformTime::Seconds() >= PlayerAttackDiagnosticAt)
	{
		PlayerAttackDiagnosticAt = FPlatformTime::Seconds() + (Spec->IsActive() ? .01 : 2.0);
		ARPGPlayerWeapon* Weapon = Player->GetPlayerCombatComponent()->GetPlayerCurrentEquippedWeapon();
		UBoxComponent* Box = Weapon ? Weapon->GetWeaponCollisionBox() : nullptr;
		bool bGeometryOverlap = false;
		if (Box && Box->GetCollisionEnabled() == ECollisionEnabled::QueryOnly)
		{
			TArray<FOverlapResult> Overlaps;
			FCollisionObjectQueryParams Objects;
			Objects.AddObjectTypesToQuery(Boss->GetCapsuleComponent()->GetCollisionObjectType());
			FCollisionQueryParams Query(SCENE_QUERY_STAT(RaidAttackDiagnostic), false, Player);
			Query.AddIgnoredActor(Weapon);
			GetWorld()->OverlapMultiByObjectType(Overlaps, Box->GetComponentLocation(), Box->GetComponentQuat(), Objects, Box->GetCollisionShape(), Query);
			bGeometryOverlap = Overlaps.ContainsByPredicate([Boss](const FOverlapResult& Result) { return Result.GetActor() == Boss; });
		}
		if (Box && Box->GetCollisionEnabled() == ECollisionEnabled::QueryOnly)
		{
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_ATTACK_COLLISION_DIAG CachedOverlap=%d PhysicsOverlap=%d WeaponResponse=%d BossObjectType=%d HitBound=%d"),
				int32(Box->IsOverlappingActor(Boss)), int32(bGeometryOverlap), int32(Box->GetCollisionResponseToChannel(Boss->GetCapsuleComponent()->GetCollisionObjectType())),
				int32(Boss->GetCapsuleComponent()->GetCollisionObjectType()), int32(Weapon->OnWeaponHitTarget.IsBound()));
		}
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_ATTACK_SERVER_DIAG Active=%d Montage=%s Position=%g Distance=%g BoxCollision=%d BoxLocation=%s BoxExtent=%s BossLocation=%s MeshTick=%d BossHP=%g"),
			int32(Spec->IsActive()), *GetNameSafe(Montage), Anim && Montage ? Anim->Montage_GetPosition(Montage) : -1.f,
			FVector::Dist2D(Player->GetActorLocation(), Boss->GetActorLocation()), Box ? int32(Box->GetCollisionEnabled()) : -1,
			Box ? *Box->GetComponentLocation().ToString() : TEXT("None"), Box ? *Box->GetScaledBoxExtent().ToString() : TEXT("None"),
			*Boss->GetActorLocation().ToString(), int32(Player->GetMesh()->VisibilityBasedAnimTickOption), BossHP(Boss));
	}
	if (!bPlayerAttackSawDamage || !bPlayerAttackOwnerConfirmed || PlayerAttackObserverClients.Num() != 2 || Spec->IsActive()) { return; }
	if (IsChannelingAttack())
	{
		if (!bPlayerAttackChannelHeld || !bPlayerAttackChannelStopped || PlayerAttackDamageCount < 3 || PlayerAttackChannelQuietStart == 0.0)
		{ Fail(TEXT("ChannelServerLifetimeMissing")); return; }
		const double QuietSeconds = GetWorld()->GetTimeSeconds() - PlayerAttackChannelQuietStart;
		if (QuietSeconds < FMath::Max(.5f, PlayerAttackChannelLoopSeconds * 2.f)) return;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CHANNEL_SERVER_QUIET NaturalEnd=1 NoPostStopDamage=1 BossHPUnchanged=1 QuietSeconds=%g HitCount=%d"), QuietSeconds, PlayerAttackDamageCount);
	}
	const int32 ExpectedDamageCount = FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESingleSwing")) ? 1 : AttackScenario().ExpectedDamageCount;
	if (ExpectedDamageCount > 0 && PlayerAttackDamageCount != ExpectedDamageCount) { Fail(TEXT("PlayerAttackDamageCountMismatch")); return; }
	const double Threat = Boss->BossThreatComponent->GetThreatForTarget(Player);
	if (!bPlayerAttackObservedActive || !bPlayerAttackObservedMontage || !bPlayerAttackEndedNaturally || Threat <= PlayerAttackThreatBefore)
	{
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_ATTACK_DIAG Active=%d Montage=%d NaturalEnd=%d ThreatBefore=%g ThreatAfter=%g"),
			int32(bPlayerAttackObservedActive), int32(bPlayerAttackObservedMontage), int32(bPlayerAttackEndedNaturally), PlayerAttackThreatBefore, Threat);
		Fail(TEXT("PlayerAttackLifetimeOrThreatMissing")); return;
	}
	if (IsSkillAttack())
	{
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SKILL_SERVER_COMPLETE Skill=%s Input=1 ServerHitQuery=1 GASDamage=1 Threat=1 Clients=2 HitCount=%d"), AttackSkillName(), PlayerAttackDamageCount);
	}
	else { UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_ATTACK_SERVER_COMPLETE Input=1 WeaponHit=1 GASDamage=1 Threat=1 Clients=2 Before=%g After=%g HitCount=%d"), PlayerAttackBossHealth, PlayerAttackBossAfter, PlayerAttackDamageCount); }
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_NATURAL_SERVER_COMPLETE Navigation=1 BTMove=1 GASDamage=1 Clients=2"));
	bFinished = true;
	CleanupPlayerAttackProbe();
	SetComponentTickEnabled(false);
}

void URPGBossMapE2EProbeComponent::TickPlayerAttackObserver()
{
	if (IsChannelingAttack() && OriginalBoss.IsValid() && PlayerAttackChannelClientHP > 0.f)
	{
		const float Health = BossHP(OriginalBoss.Get());
		if (Health < PlayerAttackChannelClientHP) { ++PlayerAttackChannelClientDrops; PlayerAttackChannelClientHP = Health; }
	}
	if (IsSkillAttack() && PlayerAttackTarget.IsValid())
	{
		const auto* Player = Cast<ARPGPlayer>(PlayerAttackTarget.Get());
		const UAbilitySystemComponent* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
		UAnimInstance* Anim = Player ? Player->GetMesh()->GetAnimInstance() : nullptr;
		UAnimMontage* Montage = ASC ? ASC->GetCurrentMontage() : nullptr;
		bPlayerAttackObservedMontage |= Anim && Montage && Montage->GetName() == AttackMontage() && Anim->Montage_GetPosition(Montage) > .02f;
		if (IsChargedAttack() && Anim && Montage && Montage->GetName() == AttackMontage())
		{
			const FName Section = Anim->Montage_GetCurrentSection(Montage);
			bPlayerAttackChargeHeld |= AttackScenario().ObservedHoldSections.Contains(Section);
			bPlayerAttackChargeReleased |= bPlayerAttackChargeHeld && Section == AttackScenario().ObservedReleaseSection;
		}
		if (IsChargedAttack() && (!bPlayerAttackChargeHeld || !bPlayerAttackChargeReleased)) return;
		if (IsChannelingAttack())
		{
			if (const auto* ChannelInstance = ObserveChannelMontage(Anim, Montage, PlayerAttackChannelMontageInstanceID))
			{
				const FName Section = ChannelInstance->GetCurrentSection();
				bPlayerAttackChannelHeld |= AttackScenario().ObservedHoldSections.Contains(Section);
				bPlayerAttackChannelStopped |= bPlayerAttackChannelHeld && Section == AttackScenario().ObservedReleaseSection;
			}
			if (!bPlayerAttackChannelHeld || !bPlayerAttackChannelStopped || PlayerAttackChannelClientDrops < 2) return;
		}
		if (!bPlayerAttackObservedMontage) return;
	}
	if (!PlayerAttackTarget.IsValid() || !OriginalBoss.IsValid() || BossHP(OriginalBoss.Get()) >= PlayerAttackBossHealth) { return; }
	bool bWeaponReplicated = false;
	for (TActorIterator<ARPGPlayerWeapon> It(GetWorld()); It; ++It)
	{
		bWeaponReplicated |= It->GetOwner() == PlayerAttackTarget.Get() && !It->IsHidden();
	}
	if (!bWeaponReplicated) { return; }
	if (!bPlayerAttackClientObserved)
	{
		bPlayerAttackClientObserved = true;
		if (IsChargedAttack()) { UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CHARGE_CLIENT_PHASES Hold=1 Release=1 MontageReplicated=1")); }
		if (IsChannelingAttack()) { UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CHANNEL_CLIENT_PHASES Loop=1 End=1 RepeatedHP=1 MontageReplicated=1 HealthDrops=%d"), PlayerAttackChannelClientDrops); }
		if (IsSkillAttack()) { UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SKILL_CLIENT_COMPLETE Skill=%s MontageReplicated=1 WeaponReplicated=1 BossDamageReplicated=1"), AttackSkillName()); }
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_ATTACK_CLIENT_COMPLETE WeaponReplicated=1 BossDamageReplicated=1 Before=%g After=%g"), PlayerAttackBossHealth, BossHP(OriginalBoss.Get()));
	}
	ServerConfirmPlayerAttack(1);
}

void URPGBossMapE2EProbeComponent::ServerConfirmPlayerAttack_Implementation(int32 Checkpoint)
{
	if (bEnabled && bSummonPlayerKillScenario) { ConfirmSummonPlayerAttack(Checkpoint); return; }
	if (!bEnabled || !bPlayerAttackScenario) { return; }
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	ARPGBossBattleGameMode* Mode = GetWorld()->GetAuthGameMode<ARPGBossBattleGameMode>();
	auto* Driver = Mode ? Mode->FindComponentByClass<URPGBossMapE2EProbeComponent>() : nullptr;
	if (!PC || !Driver || !Driver->bEnabled || Driver->bFinished || !Driver->bPlayerAttackScenario) { return; }
	ARPGPlayer* Player = Cast<ARPGPlayer>(PC->GetPawn());
	if (Checkpoint == 0 && Driver->ServerStage <= 1 && !PC->IsLocalController() && Player && FindDefaultAttack(Player->GetAbilitySystemComponent())) { Driver->PlayerAttackReadyClients.Add(PC); }
	else if (Driver->ServerStage == 2 && Driver->bPlayerAttackRequested)
	{
		if (Checkpoint == 1 && Driver->bPlayerAttackSawDamage && Driver->ReadyClients.Contains(PC)) { Driver->PlayerAttackObserverClients.Add(PC); }
		if (Checkpoint == 2 && PC->GetPawn() == Driver->PlayerAttackTarget.Get()) { Driver->bPlayerAttackOwnerConfirmed = true; }
	}
}
