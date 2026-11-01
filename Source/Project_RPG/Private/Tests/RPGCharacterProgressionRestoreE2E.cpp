#include "Tests/RPGCharacterProgressionRestoreE2E.h"

#include "Ability/RPGGameplayAbility.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Manager/HttpWebManager.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "Player/RPGPlayerState.h"
#include "PlayerClass/RPGPlayerClassComponent.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "Progression/RPGCharacterProgressionComponent.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/QuickSlot/RPGSkillQuickSlotServices.h"
#include "Skill/RPGClassSkillEligibility.h"
#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGSkillExecutionTypes.h"
#include "Tests/RPGCharacterProgressionCombatProbe.h"

bool URPGCharacterProgressionRestoreE2E::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	return Super::ShouldCreateSubsystem(Outer)
		&& FParse::Param(FCommandLine::Get(), TEXT("RPGProgressionRestoreE2E"))
		&& FParse::Param(FCommandLine::Get(), TEXT("nosteam"));
#else
	return false;
#endif
}

void URPGCharacterProgressionRestoreE2E::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UHttpWebManager>();
	Collection.InitializeDependency<URPGDungeonSessionSubsystem>();
	Collection.InitializeDependency<URPGSteamAuthSubsystem>();
	FParse::Value(FCommandLine::Get(), TEXT("RPGRestoreCharacter="), CharacterId);
	FParse::Value(FCommandLine::Get(), TEXT("RPGRestoreSteam="), SteamId);
	FParse::Value(FCommandLine::Get(), TEXT("RPGRestoreSession="), SessionId);
	FParse::Value(FCommandLine::Get(), TEXT("RPGRestoreGates="), GateRoot);
	bCombat = FParse::Param(FCommandLine::Get(), TEXT("RPGProgressionCombatE2E"));
	FParse::Value(FCommandLine::Get(), TEXT("RPGRestoreChargeChoice="), ChargeChoice);
	UE_LOG(LogTemp, Display, TEXT("PROGRESSION_RESTORE_E2E START DevelopmentOnly=1"));
	Deadline = FPlatformTime::Seconds() + (bCombat ? 300.0 : 150.0);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickProbe), 0.025f);
}

void URPGCharacterProgressionRestoreE2E::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	CombatProbe.Reset();
	Super::Deinitialize();
}

bool URPGCharacterProgressionRestoreE2E::Fail(const TCHAR* Reason)
{
	UE_LOG(LogTemp, Error, TEXT("PROGRESSION_RESTORE_E2E FAIL Reason=%s Stage=%d"), Reason, Stage);
	FPlatformMisc::RequestExitWithStatus(false, 1);
	return false;
}

bool URPGCharacterProgressionRestoreE2E::TickProbe(float)
{
	if (FPlatformTime::Seconds() > Deadline) return Fail(TEXT("Timeout"));
	UWorld* World = GetGameInstance()->GetWorld();
	if (!World || !World->IsGameWorld()) return true;
	if (!GateRoot.IsEmpty() && IFileManager::Get().FileExists(*FPaths::Combine(GateRoot, TEXT("stop.ready"))))
	{
		FPlatformMisc::RequestExitWithStatus(false, 0);
		return false;
	}
	FGuid CharacterGuid, SessionGuid;
	if (!FGuid::Parse(CharacterId, CharacterGuid) || !FGuid::Parse(SessionId, SessionGuid) || GateRoot.IsEmpty())
		return Fail(TEXT("InvalidScope"));
	const bool bServer = World->GetNetMode() == NM_DedicatedServer;
	auto* Web = GetGameInstance()->GetSubsystem<UHttpWebManager>();
	auto* Flow = GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>();
	if (!Web || (!bServer && !Flow)) return Fail(TEXT("MissingSubsystem"));
	if (!bServer && Stage == 0)
	{
		if (!FParse::Param(FCommandLine::Get(), TEXT("RPGDevelopmentLogin"))) return Fail(TEXT("DevelopmentOnly"));
		Stage = 1;
		GetGameInstance()->GetSubsystem<URPGSteamAuthSubsystem>()->LoginToDevelopmentBackend(SteamId);
		return true;
	}
	if (!bServer && Stage == 1)
	{
		if (!Web->HasAccessToken()) return true;
		if (Web->GetAuthenticatedSteamId() != SteamId || !Flow->SelectCharacter(CharacterId) || !Flow->ResumeDungeonSession())
			return Fail(TEXT("SelectOrResume"));
		Stage = 2;
		return true;
	}
	if (!bServer && Stage == 2)
	{
		if (Flow->IsRequestInFlight()) return true;
		const auto Session = Flow->GetCurrentDungeonSession();
		UE_LOG(LogTemp, Display, TEXT("PROGRESSION_RESTORE_E2E CONNECT_STATE State=%s SessionMatched=%d"), *Session.State, Session.DungeonSessionId == SessionId);
		if (Session.DungeonSessionId != SessionId || Session.State != TEXT("InProgress")
			|| !Flow->ConnectToDungeon(World->GetFirstPlayerController(), Session.ServerAddress)) return Fail(TEXT("Connect"));
		Stage = 3;
		return true;
	}
	APlayerController* PC = World->GetFirstPlayerController();
	auto* Pawn = PC ? Cast<ARPGPlayer>(PC->GetPawn()) : nullptr;
	auto* State = PC ? PC->GetPlayerState<ARPGPlayerState>() : nullptr;
	if (!Pawn || !State || !State->HasAuthenticatedCharacter()) return true;
	if (State->GetBackendCharacterId() != CharacterId || State->GetBackendDungeonSessionId() != SessionId
		|| State->GetAuthenticatedSteamId() != SteamId) return Fail(TEXT("AdmissionIdentity"));
	auto* Restore = Pawn->FindComponentByClass<URPGCharacterProgressionComponent>();
	auto* Growth = Pawn->FindComponentByClass<URPGPlayerSkillComponent>();
	auto* Class = Pawn->FindComponentByClass<URPGPlayerClassComponent>();
	auto* Slots = Pawn->FindComponentByClass<UQuickSlotComponent>();
	auto* ASC = Cast<URPGAbilitySystemComponent>(Pawn->GetAbilitySystemComponent());
	if (!Restore || !Growth || !Class || !Slots || !ASC) return Fail(TEXT("MissingPawnOwner"));
	const auto View = Restore->GetProgressionView();
	if (View.State == ERPGProgressionRestoreState::Failed) return Fail(TEXT("RestoreFailed"));
	if (View.State != ERPGProgressionRestoreState::Ready || Growth->GetSkillLoadState() != ERPGPlayerSkillLoadState::Ready
		|| Class->GetApplyState() != ERPGPlayerClassApplyState::Applied || !Slots->HasInitializedSkillSlots()) return true;
	const auto* Definition = Growth->GetPlayerClassDefinition();
	if (!Definition) return true; // the owner-only state and primary asset may replicate separately
	const auto Assault = FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.Skill.AssultBlade"));
	const auto Charge = FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.Skill.Charge"));
	const auto Whirlwind = FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.Skill.WhirlWind"));
	const auto Jump = FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.Skill.JumpSmash"));
	int32 Tier = INDEX_NONE, Option = INDEX_NONE;
	const auto* ChargeBinding = Definition->FindSkill(Charge);
	if (!ChargeBinding || !ChargeBinding->Definition
		|| (ChargeChoice != TEXT("FullCharge") && ChargeChoice != TEXT("QuickCharge"))
		|| !ChargeBinding->Definition->ResolveSavedTripod(4, FName(*ChargeChoice), Tier, Option)) return Fail(TEXT("AuthoredTripod"));
	FRPGSkillSaveData ChargeSave;
	if (!Growth->TryGetSkillSaveData(Charge, ChargeSave)) return true;
	if (View.BackendRevision != 3 || View.Experience != 100 || View.Level != 2 || View.ProfileId != TEXT("FirstSlice.GreatSword.v1")
		|| Definition->ClassId != TEXT("GreatSwordWarrior") || Growth->GetTotalSP() != 3 || Growth->GetRemainingSP() != 0
		|| Growth->IsConfigurationMutable() || !Growth->IsClassIdentityUnlocked() || !Growth->IsSkillAcquired(Assault)
		|| !Growth->IsSkillAcquired(Whirlwind) || Growth->IsSkillAcquired(Jump) || ChargeSave.SkillLevel != 4
		|| !ChargeSave.SelectedTripodIndices.IsValidIndex(Tier) || ChargeSave.SelectedTripodIndices[Tier] != Option)
		return Fail(TEXT("SavedGrowthMismatch"));
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const auto* Slot = Slots->GetSkillSlotContent(Index);
		const FGameplayTag Expected = Index == 0 ? Charge : Index == 1 ? Assault : Index == 7 ? Whirlwind : FGameplayTag();
		if (!Slot || Slot->AbilityTag != Expected) return Fail(TEXT("SavedEightSlotsMismatch"));
	}
	FGameplayAbilitySpec* ChargeSpec = nullptr;
	int32 SkillGrants = 0, IdentityGrants = 0;
	for (auto& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Definition->IdentityAbility && Spec.Ability->GetClass() == Definition->IdentityAbility.Get()) ++IdentityGrants;
		const auto* Skill = Cast<URPGGameplayAbility_SkillContainer>(Spec.GetPrimaryInstance());
		if (!Skill || !Skill->GetSkillDefinition()) continue;
		const auto Tag = Skill->GetSkillDefinition()->SkillTag;
		if (Tag == Charge) ChargeSpec = &Spec;
		if (Growth->IsSkillInCurrentClass(Tag)) ++SkillGrants;
		if (Tag == Jump) return Fail(TEXT("UnlearnedGrant"));
	}
	if (!ChargeSpec || SkillGrants != 3 || IdentityGrants != 1) return true; // wait for initial spec replication
	const auto* Driver = World->GetNetDriver();
	if (!Driver || !Driver->IsUsingIrisReplication() || ASC->GetAvatarActor() != Pawn) return Fail(TEXT("NetworkContext"));
	if (bCombat)
	{
		if (!CombatProbe)
		{
			CombatProbe = MakeShared<FRPGCharacterProgressionCombatProbe>();
			UE_LOG(LogTemp, Display, TEXT("PROGRESSION_RESTORE_E2E RESTORED Role=%s Revision=3 SP=3 Used=3 Learned=3 SkillGrants=3 IdentityGrants=1 Slots=8 Empty=5 Iris=1"), bServer ? TEXT("Server") : TEXT("Client"));
		}
		CombatProbe->Tick(*PC, *ASC, ChargeSpec->Handle, *Slots, GateRoot, ChargeChoice);
		if (!CombatProbe->Failure().IsEmpty()) return Fail(*CombatProbe->Failure());
		return true;
	}
	if ((bServer && Stage == 0) || (!bServer && Stage == 3))
	{
		UE_LOG(LogTemp, Display, TEXT("PROGRESSION_RESTORE_E2E RESTORED Role=%s Revision=3 SP=3 Used=3 Learned=3 SkillGrants=3 IdentityGrants=1 Slots=8 Empty=5 Iris=1"), bServer ? TEXT("Server") : TEXT("Client"));
		if (bServer)
		{
			const auto Handle = ChargeSpec->Handle;
			const auto Generation = Growth->GetSkillRestoreGeneration();
			Restore->NotifyOwnerContextChanged();
			Class->RefreshAppliedClass();
			Growth->AddTotalSP(20);
			Slots->SetSkillSlot(2, Charge);
			if (Growth->GetTotalSP() != 3 || Growth->GetSkillRestoreGeneration() != Generation
				|| !ASC->FindAbilitySpecFromHandle(Handle) || Slots->GetSkillSlotContent(0)->AbilityTag != Charge
				|| Slots->GetSkillSlotContent(2)->AbilityTag.IsValid()) return Fail(TEXT("DuplicateOrMutableRestore"));
			auto* Equipment = Pawn->FindComponentByClass<URPGAuthoritativeEquipmentComponent>();
			// Inventory/actor restoration is independent of progression; observe it
			// immediately before input below rather than manufacturing equipment.
			if (!Equipment) return Fail(TEXT("MissingEquipmentOwner"));
			// Choose a clear ordinary aim direction in the authored arena. Keep
			// the production obstruction validation active; never submit synthetic target data.
			FVector Eye;
			FRotator EyeRotation;
			Pawn->GetActorEyesViewPoint(Eye, EyeRotation);
			FCollisionQueryParams Query(SCENE_QUERY_STAT(RPGProgressionRestoreAim), false, Pawn);
			float BestDistance = -1.f, BestYaw = 0.f;
			for (int32 Direction = 0; Direction < 8; ++Direction)
			{
				const float Yaw = Direction * 45.f;
				const FVector End = Eye + FRotator(0.f, Yaw, 0.f).Vector() * 3000.f;
				FHitResult Hit;
				const bool bHit = World->LineTraceSingleByChannel(Hit, Eye, End, ECC_Visibility, Query);
				const float Distance = bHit ? Hit.Distance : 3000.f;
				if (Distance > BestDistance) { BestDistance = Distance; BestYaw = Yaw; }
			}
			if (BestDistance < 500.f || !FFileHelper::SaveStringToFile(FString::SanitizeFloat(BestYaw),
				*FPaths::Combine(GateRoot, TEXT("aim-yaw.txt")))) return Fail(TEXT("NoClearFixtureAim"));
			Stage = 1;
		}
		else Stage = 4;
	}
	if (!bServer && Stage == 4 && IFileManager::Get().FileExists(*FPaths::Combine(GateRoot, TEXT("cast.ready"))))
	{
		FString YawText;
		float Yaw = 0.f;
		if (!FFileHelper::LoadFileToString(YawText, *FPaths::Combine(GateRoot, TEXT("aim-yaw.txt")))
			|| !LexTryParseString(Yaw, *YawText) || !FMath::IsFinite(Yaw)) return Fail(TEXT("MissingFixtureAim"));
		PC->SetControlRotation(FRotator(0.f, Yaw, 0.f));
		InputStartedAt = FPlatformTime::Seconds();
		Stage = 7;
		return true;
	}
	if (!bServer && Stage == 7 && FPlatformTime::Seconds() - InputStartedAt > 0.8)
	{
		Stage = 8;
		InputStartedAt = 0.0;
	}
	if (!bServer && Stage == 8)
	{
		const auto* Skill = Cast<URPGGameplayAbility_SkillContainer>(ChargeSpec->GetPrimaryInstance());
		FRPGGASSkillQuickSlotAccess Access(ASC);
		FGameplayTagContainer FailureTags;
		if (Access.Resolve(Charge) != ChargeSpec->Handle || !static_cast<const UGameplayAbility*>(Skill)->CanActivateAbility(ChargeSpec->Handle, ASC->AbilityActorInfo.Get(), nullptr, nullptr, &FailureTags))
		{
			if (InputStartedAt == 0.0)
			{
				InputStartedAt = FPlatformTime::Seconds();
				UE_LOG(LogTemp, Display, TEXT("PROGRESSION_RESTORE_E2E WAIT_INPUT Resolved=%d Eligible=%d Weapon=%d Local=%d FailureTags=%s"),
					Access.Resolve(Charge) == ChargeSpec->Handle,
					RPGClassSkillEligibility::CanActivate(Pawn, Charge, Skill->GetSkillDefinition(), Skill->GetClass(), ChargeSpec->Handle),
					Pawn->GetPlayerCombatComponent()->GetPlayerCurrentEquippedWeapon() != nullptr, Pawn->IsLocallyControlled(), *FailureTags.ToStringSimple());
				UE_LOG(LogTemp, Display, TEXT("PROGRESSION_RESTORE_E2E INPUT_CONTEXT Inhibited=%d ImmunityValid=%d ActorInfoLocal=%d Role=%d AssetTags=%s"),
					ASC->GetUserAbilityActivationInhibited(), Skill->GetSkillDefinition()->ActivationImmunities.IsValid(),
					ASC->AbilityActorInfo->IsLocallyControlled(), static_cast<int32>(Pawn->GetLocalRole()), *Skill->GetAssetTags().ToStringSimple());
			}
			if (FPlatformTime::Seconds() - InputStartedAt > 10.0) return Fail(TEXT("SavedSkillNotActivatable"));
			return true;
		}
		Slots->BeginUseSkillSlot(2); // deliberately empty; no grant or fallback
		Slots->EndUseSkillSlot(2);
		Slots->BeginUseSkillSlot(0); // existing input router, no test RPC or direct TryActivateAbility
		InputStartedAt = FPlatformTime::Seconds();
		Stage = 5;
		UE_LOG(LogTemp, Display, TEXT("PROGRESSION_RESTORE_E2E INPUT Slot=0 SavedCharge=1 EmptySlotIgnored=1"));
	}
	if (!bServer && Stage == 5 && FPlatformTime::Seconds() - InputStartedAt > 1.5)
	{
		Slots->EndUseSkillSlot(0);
		Stage = 6;
		UE_LOG(LogTemp, Display, TEXT("PROGRESSION_RESTORE_E2E RELEASE Slot=0"));
	}
	if (bServer && Stage == 1 && ChargeSpec->IsActive())
	{
		const auto* Skill = Cast<URPGGameplayAbility_SkillContainer>(ChargeSpec->GetPrimaryInstance());
		const auto& Runtime = Skill->GetActiveSkillSpec();
		const auto* Config = Runtime.ExecutionConfig.GetPtr<FRPGSkillChargeExecutionConfig>();
		if (!Config || Runtime.SkillTag != Charge || Runtime.SkillLevel != 4
			|| !Runtime.SelectedTripodIndices.IsValidIndex(Tier) || Runtime.SelectedTripodIndices[Tier] != Option
			|| !FMath::IsNearlyEqual(Config->ChargeTimePerLevel * Config->MaxChargeLevel, ChargeChoice == TEXT("FullCharge") ? 1.2f : 0.7f)
			|| Config->MinimumReleaseLevel != Config->MaxChargeLevel) return Fail(TEXT("FrozenRuntimeMismatch"));
		const auto Handle = ChargeSpec->Handle;
		Restore->NotifyOwnerContextChanged();
		Class->RefreshAppliedClass();
		if (ASC->FindAbilitySpecFromHandle(Handle) != ChargeSpec || !ChargeSpec->IsActive()) return Fail(TEXT("ActiveGrantReplaced"));
		CastSeenAt = FPlatformTime::Seconds();
		Stage = 2;
		UE_LOG(LogTemp, Display, TEXT("PROGRESSION_RESTORE_E2E CAST Server=1 SavedLevel=4 FullCharge=1 ChargeSeconds=1.2 ExistingInput=1 SameActiveHandle=1"));
	}
	if (bServer && Stage == 2 && !ChargeSpec->IsActive() && FPlatformTime::Seconds() - CastSeenAt > 1.0)
	{
		auto* Equipment = Pawn->FindComponentByClass<URPGAuthoritativeEquipmentComponent>();
		FRPGEquipmentSnapshot WeaponSnapshot;
		const auto* Weapon = Pawn->GetPlayerCombatComponent()->GetPlayerCurrentEquippedWeapon();
		if (!Equipment || !Weapon || !Equipment->FindWeaponSnapshot(EWeaponHandType::Count, WeaponSnapshot)
			|| WeaponSnapshot.EquippedActor != Weapon || !Definition->AllowedWeaponTypes.Contains(WeaponSnapshot.WeaponType))
			return Fail(TEXT("NativeWeaponIdentity"));
		const auto EquipState = Equipment->GetActiveEquipState();
		const auto ChargeHandle = ChargeSpec->Handle;
		Equipment->SetActiveEquipState(EEquipState::None);
		const bool bHolsteredAllowed = RPGClassSkillEligibility::CanActivate(Pawn, Charge, ChargeBinding->Definition,
			ChargeBinding->AbilityClass.Get(), ChargeHandle);
		Equipment->SetActiveEquipState(EquipState);
		if (bHolsteredAllowed) return Fail(TEXT("HolsteredWeaponAllowed"));
		UE_LOG(LogTemp, Display, TEXT("PROGRESSION_RESTORE_E2E WEAPON SnapshotType=%d ActorType=%d HolsteredRejected=1"),
			static_cast<int32>(WeaponSnapshot.WeaponType), static_cast<int32>(Weapon->GetWeaponType()));
		Stage = 3;
		UE_LOG(LogTemp, Display, TEXT("PROGRESSION_RESTORE_E2E PASS Server=1 Ended=1 SavedConfigurationRetained=1"));
	}
	return true;
}
