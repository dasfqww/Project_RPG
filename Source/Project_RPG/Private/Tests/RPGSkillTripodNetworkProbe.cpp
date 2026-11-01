#include "Tests/RPGSkillTripodNetworkProbe.h"
#include "Tests/RPGSkillAssetCooldownProbe.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Animation/AnimInstance.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RPGAbilityTags.h"
#include "RPGInputTags.h"
#include "Skill/RPGSkillCatalogSubsystem.h"
#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillExecutionPolicy.h"
#include "Skill/RPGSkillExecutionTypes.h"
#include "UI/MVVM/RPGSkillViewModel.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"
#include "UI/Skill/RPGTripodBoard.h"
#include "UI/Skill/RPGTripodButton.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWidget.h"

namespace
{
	constexpr int32 FixtureTotalSP = 5;
	constexpr int32 SelectionRejectionCount = 7;
	// Test-only public checkpoint: private selections stay non-default until
	// every owner has inspected its remote pawn replicas. Score is restored.
	constexpr float PrivacyReadyScore = 4101.0f;
	constexpr float PrivacyCheckedScore = 4102.0f;
	constexpr float PrivacyReleasedScore = 4103.0f;
	constexpr float InstantActiveScore = 4104.0f;
	constexpr float ChargeActiveScore = 4105.0f;
	constexpr float ChargeCooldownReadyScore = 4106.0f;
	// Slow only the disposable test pawns, keeping montage sections observable
	// across delayed RPCs without changing any animation asset or live player.
	constexpr float FixtureAttackSpeed = 0.25f;

	URPGAbilitySystemComponent* GetASC(APlayerController& Controller)
	{
		return Cast<URPGAbilitySystemComponent>(
			UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Controller.GetPawn()));
	}

	FGameplayAbilitySpec* FindChargeSpec(URPGAbilitySystemComponent& ASC)
	{
		for (FGameplayAbilitySpec& Spec : ASC.GetActivatableAbilities())
			if (Cast<URPGGameplayAbility_SkillContainer>(Spec.Ability) &&
				Spec.Ability->GetClass()->GetName().Contains(TEXT("ChargeSlash"))) return &Spec;
		return nullptr;
	}

	FName ReadSection(const APlayerController& Controller, const FRPGSkillRuntimeSpec& Runtime)
	{
		const auto* Character = Cast<ACharacter>(Controller.GetPawn());
		const auto* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
		return Anim && Runtime.Montage ? Anim->Montage_GetCurrentSection(Runtime.Montage) : NAME_None;
	}

	bool SnapshotMatches(const FRPGSkillRuntimeSpec& Runtime, const bool bCharge)
	{
		return Runtime.SkillTag == RPGGameplayTags::Player_Ability_Skill_Charge && Runtime.SkillLevel == 4 &&
			Runtime.SelectedTripodIndices.IsValidIndex(0) && Runtime.SelectedTripodIndices[0] == (bCharge ? 1 : 0) &&
			Runtime.GetStatScalar(FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.Damage"))) == (bCharge ? 3.0f : 2.0f) &&
			Runtime.ExecutionPolicyClass == (bCharge ? URPGSkillExecutionPolicy_Charge::StaticClass() :
				URPGSkillExecutionPolicy_Instant::StaticClass());
	}

	uint32 ReadRevision(const URPGPlayerSkillComponent& Component)
	{
		const auto* Property = FindFProperty<FUInt32Property>(
			URPGPlayerSkillComponent::StaticClass(), TEXT("ReplicatedSkillDataRevision"));
		check(Property);
		return Property->GetPropertyValue_InContainer(&Component);
	}

	const TArray<FRPGReplicatedSkillSaveEntry>& ReadReplicatedEntries(const URPGPlayerSkillComponent& Component)
	{
		const auto* Property = FindFProperty<FArrayProperty>(
			URPGPlayerSkillComponent::StaticClass(), TEXT("ReplicatedSkillData"));
		check(Property);
		return *Property->ContainerPtrToValuePtr<TArray<FRPGReplicatedSkillSaveEntry>>(&Component);
	}

	const FRPGReplicatedSkillSaveEntry* ReadReplicatedSelection(const URPGPlayerSkillComponent& Component,
		const FGameplayTag SkillTag = RPGGameplayTags::Player_Ability_Skill_Charge)
	{
		return ReadReplicatedEntries(Component).FindByPredicate([SkillTag](const FRPGReplicatedSkillSaveEntry& Entry)
		{
			return Entry.SkillTag == SkillTag;
		});
	}

	int32 ReadUsedSP(const URPGPlayerSkillComponent& Component)
	{
		const auto* Property = FindFProperty<FIntProperty>(
			URPGPlayerSkillComponent::StaticClass(), TEXT("UsedSP"));
		check(Property);
		return Property->GetPropertyValue_InContainer(&Component);
	}

	void SetCheckpoint(APlayerState& PlayerState, const float Score)
	{
		if (PlayerState.GetScore() != Score)
		{
			PlayerState.SetScore(Score);
			PlayerState.ForceNetUpdate();
		}
	}
}

FRPGSkillTripodNetworkProbe::~FRPGSkillTripodNetworkProbe()
{
	if (auto* ASC = InputAbilitySystem.Get()) ASC->OnAbilityEnded.Remove(AbilityEndedHandle);
	ReleaseInput();
	if (Board.IsValid()) Board->InitializeBoard(nullptr);
}

void FRPGSkillTripodNetworkProbe::ObserveAbilityEnd(APlayerController& Controller, URPGAbilitySystemComponent& ASC)
{
	if (InputAbilitySystem.Get() == &ASC && AbilityEndedHandle.IsValid()) return;
	if (auto* Previous = InputAbilitySystem.Get()) Previous->OnAbilityEnded.Remove(AbilityEndedHandle);
	InputAbilitySystem = &ASC;
	AbilityEndedHandle = ASC.OnAbilityEnded.AddLambda(
		[this, bAuthority = Controller.HasAuthority(), ControllerName = Controller.GetName()](const FAbilityEndedData& End)
	{
		if (End.AbilitySpecHandle != SkillHandle || End.bWasCancelled ||
			!(bClientSawCharge || bServerSawCharge)) return;
		bChargeEndedNormally = true;
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST %s_CHARGE_ENDED Cancelled=0 Controller=%s"),
			bAuthority ? TEXT("SERVER") : TEXT("CLIENT"), *ControllerName);
	});
}

void FRPGSkillTripodNetworkProbe::ReleaseInput()
{
	if (bInputHeld)
		if (auto* ASC = InputAbilitySystem.Get()) ASC->OnAbilitySpecInputReleased(SkillHandle);
	bInputHeld = false;
}

FRPGSkillTripodNetworkProbe::EResult FRPGSkillTripodNetworkProbe::Fail(const TCHAR* Reason)
{
	ReleaseInput();
	Failure = Reason;
	return EResult::Failed;
}

bool FRPGSkillTripodNetworkProbe::Initialize(APlayerController& Controller,
	URPGPlayerSkillComponent& Component, URPGSkillCatalogSubsystem& Catalog)
{
	FParse::Value(FCommandLine::Get(), TEXT("RPGExpectedClients="), ExpectedClients);
	if (ExpectedClients < 2 || !Controller.PlayerState)
	{
		Failure = TEXT("PrivacyFixtureRequiresTwoPlayersAndPlayerState");
		return false;
	}
	URPGSkillDefinition* Original = Catalog.FindSkillDefinition(RPGGameplayTags::Player_Ability_Skill_Charge);
	if (!Original)
	{
		Failure = TEXT("ChargeDefinitionMissing");
		return false;
	}
	// One transient replacement per game instance, shared by both server pawns.
	if (Original->GetOuter() != &Catalog)
	{
		Original = DuplicateObject<URPGSkillDefinition>(Original, &Catalog);
		Original->SetFlags(RF_Transient);
		const bool bAuthority = Controller.HasAuthority();
		Original->MaxSkillLevel = bAuthority ? 4 : 10;
		Original->ModeOverrides.Reset();
		Original->SkillVFX = nullptr;
		Original->BaseCooldown = 1.0f;
		Original->TripodTiers.Reset();
		FRPGSkillTripodTier& FirstTier = Original->TripodTiers.AddDefaulted_GetRef();
		FirstTier.RequiredSkillLevel = 4;
		for (const float Scalar : {2.0f, 3.0f})
		{
			FRPGSkillModifier Modifier;
			Modifier.StatTag = FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.Damage"));
			Modifier.ScalarValue = Scalar;
			FirstTier.Options.AddDefaulted_GetRef().StatModifiers.Add(Modifier);
		}
		FRPGSkillInstantExecutionConfig Instant;
		Instant.StartSection = TEXT("Attack");
		FirstTier.Options[0].OverrideExecutionPolicyClass = URPGSkillExecutionPolicy_Instant::StaticClass();
		FirstTier.Options[0].OverrideExecutionConfig.InitializeAs<FRPGSkillInstantExecutionConfig>(Instant);
		FRPGSkillChargeExecutionConfig Charge;
		Charge.ChargeSection = TEXT("Prepare");
		Charge.ReleaseSection = TEXT("Attack");
		Charge.MaxChargeHoldTime = 10.0f;
		FirstTier.Options[1].OverrideExecutionPolicyClass = URPGSkillExecutionPolicy_Charge::StaticClass();
		FirstTier.Options[1].OverrideExecutionConfig.InitializeAs<FRPGSkillChargeExecutionConfig>(Charge);
		FRPGSkillTripodTier& LockedTier = Original->TripodTiers.AddDefaulted_GetRef();
		// Deliberately simulate a stale client catalog so UI prediction succeeds
		// but the authority must reject and restore its locked-tier selection.
		LockedTier.RequiredSkillLevel = bAuthority ? 7 : 4;
		LockedTier.Options.AddDefaulted();
		const auto* Property = FindFProperty<FArrayProperty>(
			URPGSkillCatalogSubsystem::StaticClass(), TEXT("CachedDefinitions"));
		check(Property);
		auto& Definitions = *Property->ContainerPtrToValuePtr<TArray<TObjectPtr<URPGSkillDefinition>>>(&Catalog);
		for (auto& Entry : Definitions)
		{
			if (Entry && Entry->SkillTag == Original->SkillTag)
			{
				Entry = Original;
				break;
			}
		}
	}
	Definition.Reset(Original);
	// A separate, equal-cap definition ensures the SP rejection cannot be
	// confused with the deliberately different charge level caps above.
	URPGSkillDefinition* Budget = Catalog.FindSkillDefinition(RPGGameplayTags::Player_Ability_Rage);
	if (!Budget || Budget->GetOuter() != &Catalog)
	{
		Budget = DuplicateObject<URPGSkillDefinition>(Original, &Catalog,
			MakeUniqueObjectName(&Catalog, Original->GetClass(), TEXT("SkillBudgetFixture")));
		Budget->SetFlags(RF_Transient);
		Budget->SkillTag = RPGGameplayTags::Player_Ability_Rage;
		Budget->MaxSkillLevel = 10;
		Budget->TripodTiers.Reset();
		const auto* Property = FindFProperty<FArrayProperty>(
			URPGSkillCatalogSubsystem::StaticClass(), TEXT("CachedDefinitions"));
		check(Property);
		auto& Definitions = *Property->ContainerPtrToValuePtr<TArray<TObjectPtr<URPGSkillDefinition>>>(&Catalog);
		auto* Existing = Definitions.FindByPredicate([](const auto& Entry)
		{
			return Entry && Entry->SkillTag == RPGGameplayTags::Player_Ability_Rage;
		});
		if (Existing) *Existing = Budget;
		else Definitions.Add(Budget);
	}
	BudgetDefinition.Reset(Budget);
	if (Controller.HasAuthority())
	{
		OriginalPlayerScore = Controller.PlayerState->GetScore();
		const auto* Property = FindFProperty<FIntProperty>(
			URPGPlayerSkillComponent::StaticClass(), TEXT("TotalSP"));
		check(Property);
		Property->SetPropertyValue_InContainer(&Component, FixtureTotalSP);
		URPGAbilitySystemComponent* ASC = GetASC(Controller);
		if (!ASC) { Failure = TEXT("FixtureAbilitySystemMissing"); return false; }
		ASC->SetNumericAttributeBase(URPGAttributeSet::GetAttackSpeedAttribute(), FixtureAttackSpeed);
		if (!ASC->HasMatchingGameplayTag(RPGGameplayTags::Player_Status_Rage_Active))
			ASC->AddLooseGameplayTag(RPGGameplayTags::Player_Status_Rage_Active, 1,
				EGameplayTagReplicationState::CountToOwner);
		if (!FindChargeSpec(*ASC))
		{
			UClass* AbilityClass = LoadClass<UGameplayAbility>(nullptr,
				TEXT("/Game/Blueprints/Character/Player/Abilities/Skill/GA_Player_ChargeSlash.GA_Player_ChargeSlash_C"));
			if (!AbilityClass) { Failure = TEXT("ChargeAbilityClassMissing"); return false; }
			FGameplayAbilitySpec Granted(AbilityClass, 1);
			Granted.SourceObject = Controller.GetPawn();
			Granted.GetDynamicSpecSourceTags().AddTag(RPGGameplayTags::InputTag_MustBeHeld_Charge_IdentitySkill);
			ASC->GiveAbility(Granted);
		}
		Component.GetOwner()->ForceNetUpdate();
	}
	else
	{
		ViewModel.Reset(NewObject<URPGSkillViewModel>(&Controller));
		ViewModel->InitializeSkillData(&Component, {Original, Budget});
		UClass* BoardClass = LoadClass<URPGTripodBoard>(nullptr,
			TEXT("/Game/Blueprints/UI/Skill/WBP_TripodBoard.WBP_TripodBoard_C"));
		Board.Reset(BoardClass ? CreateWidget<URPGTripodBoard>(&Controller, BoardClass) : nullptr);
		if (!Board.IsValid()) { Failure = TEXT("AuthoredTripodBoardMissing"); return false; }
		SlateBoard = Board->TakeWidget();
		Board->InitializeBoard(ViewModel->SkillSlots[0]);
	}
	Phase = EPhase::Level;
	return true;
}

void FRPGSkillTripodNetworkProbe::BeginReplyWait(const URPGPlayerSkillComponent& Component, const uint32 Replies)
{
	RevisionBefore = ReadRevision(Component);
	ExpectedReplies = Replies;
}

bool FRPGSkillTripodNetworkProbe::RepliesArrived(const URPGPlayerSkillComponent& Component) const
{
	return static_cast<uint32>(ReadRevision(Component) - RevisionBefore) >= ExpectedReplies;
}

bool FRPGSkillTripodNetworkProbe::CheckState(const URPGPlayerSkillComponent& Component,
	const int32 Level, const int32 Option, const int32 RemainingSP) const
{
	const auto* Replicated = ReadReplicatedSelection(Component);
	const FRPGSkillSaveData Local = Component.GetSkillSaveData(Definition->SkillTag);
	const auto Matches = [=](const FRPGSkillSaveData& Data)
	{
		return Data.SkillLevel == Level && Data.SelectedTripodIndices.Num() == 2 &&
			Data.SelectedTripodIndices[0] == Option && Data.SelectedTripodIndices[1] == INDEX_NONE;
	};
	return Replicated && Matches(Replicated->SaveData) && Matches(Local) &&
		Component.GetRemainingSP() == RemainingSP && ViewModel->RemainingSP == RemainingSP &&
		ViewModel->SkillSlots.Num() == 2 && ViewModel->SkillSlots[0]->SkillLevel == Level &&
		ViewModel->SkillSlots[0]->CurrentTripodIndices == Local.SelectedTripodIndices && CheckButtons(Level, Option);
}

bool FRPGSkillTripodNetworkProbe::ClickTripod(const int32 Tier, const int32 Option)
{
	const FName Name(*FString::Printf(TEXT("Btn_%d_%d"), Tier + 1, Option + 1));
	auto* Button = Board.IsValid() ? Cast<URPGTripodButton>(Board->GetWidgetFromName(Name)) : nullptr;
	if (!Button || !Button->GetIsEnabled() || Button->GetVisibility() != ESlateVisibility::Visible) return false;
	// Real CommonUI handler and ViewModel command, not a delegate-only shortcut.
	// No rendered mouse routing is asserted by this native network fixture.
	Button->ProcessEvent(Button->FindFunctionChecked(TEXT("HandleButtonClicked")), nullptr);
	return ViewModel->SkillSlots[0]->IsTripodSelected(Tier, Option) && Button->GetSelected();
}

bool FRPGSkillTripodNetworkProbe::CheckButtons(const int32 Level, const int32 Option) const
{
	if (!Board.IsValid()) return false;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		auto* Button = Cast<URPGTripodButton>(Board->GetWidgetFromName(Index == 0 ? TEXT("Btn_1_1") : TEXT("Btn_1_2")));
		if (!Button || Button->GetSelected() != (Option == Index) || Button->GetIsEnabled() != (Level >= 4)) return false;
	}
	const auto* Locked = Cast<URPGTripodButton>(Board->GetWidgetFromName(TEXT("Btn_2_1")));
	return Locked && !Locked->GetSelected();
}

FRPGSkillTripodNetworkProbe::EResult FRPGSkillTripodNetworkProbe::TickClientActivation(
	APlayerController& Controller, const bool bCharge)
{
	auto* ASC = GetASC(Controller);
	auto* Spec = ASC ? FindChargeSpec(*ASC) : nullptr;
	if (!Spec || !ASC->HasMatchingGameplayTag(RPGGameplayTags::Player_Status_Rage_Active) ||
		!FMath::IsNearlyEqual(ASC->GetNumericAttribute(URPGAttributeSet::GetAttackSpeedAttribute()), FixtureAttackSpeed))
		return EResult::Waiting;
	if (!bInputHeld)
	{
		// These process-local Definition clones have no network-stable asset
		// identity. After prediction catch-up, their replicated effect source
		// cannot drive the client's source-object query. Confirm the authority's
		// real cooldown readiness rather than racing it or sleeping a fixed time.
		if (bCharge && Controller.PlayerState->GetScore() != ChargeCooldownReadyScore)
			return EResult::Waiting;
		if (Spec->IsActive()) return EResult::Waiting;
		const auto* ReadyAbility = Cast<URPGGameplayAbility_SkillContainer>(Spec->GetPrimaryInstance());
		if (!ReadyAbility) return EResult::Waiting;
		// Loading stalls advance wall time differently from gameplay timers.
		// Do not bypass an active local guard even after the authority is ready.
		if (!ReadyAbility->CheckCooldown(Spec->Handle, ASC->AbilityActorInfo.Get()))
		{
			if (!bLoggedCooldownWait)
			{
				bLoggedCooldownWait = true;
				UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST CLIENT_WAIT_COOLDOWN Remaining=%f"),
					ReadyAbility->GetCooldownTimeRemaining(ASC->AbilityActorInfo.Get()));
			}
			return EResult::Waiting;
		}
		InputAbilitySystem = ASC;
		SkillHandle = Spec->Handle;
		bInputHeld = true;
		ASC->OnAbilitySpecInputPressed(SkillHandle);
		if (!Spec->IsActive()) return Fail(TEXT("FixtureInputDidNotActivateAbility"));
	}
	const auto* Ability = Cast<URPGGameplayAbility_SkillContainer>(Spec->GetPrimaryInstance());
	if (!Spec->IsActive() || !Ability || !SnapshotMatches(Ability->GetActiveSkillSpec(), bCharge))
		return Fail(TEXT("ClientSelectedTripodActivationSnapshotMismatch"));
	bool& bSawActivation = bCharge ? bClientSawCharge : bClientSawInstant;
	if (!bSawActivation)
	{
		const FName Section = bCharge ? FName(TEXT("Prepare")) : FName(TEXT("Attack"));
		if (ReadSection(Controller, Ability->GetActiveSkillSpec()) != Section) return EResult::Waiting;
		bSawActivation = true;
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST CLIENT_ACTIVATION Option=%d Policy=%s Level=4 Scalar=%d Section=%s"),
			bCharge ? 1 : 0, bCharge ? TEXT("Charge") : TEXT("Instant"), bCharge ? 3 : 2, *Section.ToString());
	}
	if (Controller.PlayerState->GetScore() != (bCharge ? ChargeActiveScore : InstantActiveScore)) return EResult::Waiting;
	ReleaseInput();
	if (bCharge)
	{
		Phase = EPhase::ChargeRelease;
	}
	else
	{
		// Explicitly cancel the fixture's instant attack only after the authority
		// has acknowledged its real montage/snapshot; normal completion is not claimed.
		ASC->CancelAbilityHandle(SkillHandle);
		auto* Component = Controller.GetPawn()->FindComponentByClass<URPGPlayerSkillComponent>();
		if (!RequestRejection(*Component)) return Fail(TEXT("RejectionRequestDidNotPredict"));
		Phase = EPhase::Reject;
	}
	return EResult::Waiting;
}

bool FRPGSkillTripodNetworkProbe::ObserveServerActivation(APlayerController& Controller)
{
	auto* ASC = GetASC(Controller);
	auto* Spec = ASC ? FindChargeSpec(*ASC) : nullptr;
	const auto* Ability = Spec ? Cast<URPGGameplayAbility_SkillContainer>(Spec->GetPrimaryInstance()) : nullptr;
	if (!Spec || !Spec->IsActive()) return true;
	if (!Ability) return false;
	const FRPGSkillRuntimeSpec& Runtime = Ability->GetActiveSkillSpec();
	const bool bCharge = Runtime.ExecutionPolicyClass == URPGSkillExecutionPolicy_Charge::StaticClass();
	if (!SnapshotMatches(Runtime, bCharge)) return false;
	const FName Section = ReadSection(Controller, Runtime);
	bool& bSawActivation = bCharge ? bServerSawCharge : bServerSawInstant;
	if (!bSawActivation && Section == (bCharge ? FName(TEXT("Prepare")) : FName(TEXT("Attack"))))
	{
		bSawActivation = true;
		SetCheckpoint(*Controller.PlayerState, bCharge ? ChargeActiveScore : InstantActiveScore);
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST SERVER_ACTIVATION Option=%d Policy=%s Level=4 Scalar=%d Section=%s Controller=%s"),
			bCharge ? 1 : 0, bCharge ? TEXT("Charge") : TEXT("Instant"), bCharge ? 3 : 2, *Section.ToString(), *Controller.GetName());
	}
	if (bCharge && bServerSawCharge && Section == TEXT("Attack") && !bServerSawRelease)
	{
		bServerSawRelease = true;
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST SERVER_RELEASE Section=Attack Controller=%s"), *Controller.GetName());
	}
	return true;
}

bool FRPGSkillTripodNetworkProbe::CheckBudgetState(const URPGPlayerSkillComponent& Component,
	const int32 Level) const
{
	const auto* Replicated = ReadReplicatedSelection(Component, BudgetDefinition->SkillTag);
	const auto Local = Component.GetSkillSaveData(BudgetDefinition->SkillTag);
	return Replicated && Replicated->SaveData.SkillLevel == Level &&
		Replicated->SaveData.SelectedTripodIndices.IsEmpty() && Local.SkillLevel == Level &&
		Local.SelectedTripodIndices.IsEmpty() && ViewModel->SkillSlots[1]->SkillLevel == Level &&
		ViewModel->SkillSlots[1]->CurrentTripodIndices.IsEmpty();
}

bool FRPGSkillTripodNetworkProbe::CheckRemotePrivacy(APlayerController& Controller,
	int32& VisiblePeers, bool& AllPeersReady) const
{
	VisiblePeers = 0;
	AllPeersReady = true;
	const FRPGSkillSaveData DefaultSaveData;
	for (TActorIterator<APawn> It(Controller.GetWorld(), Controller.GetPawn()->GetClass()); It; ++It)
	{
		if (*It == Controller.GetPawn() || It->IsLocallyControlled()) continue;
		const auto* PeerState = It->GetPlayerState();
		const auto* Peer = It->FindComponentByClass<URPGPlayerSkillComponent>();
		if (!PeerState || !Peer) continue;
		const auto* Defaults = Cast<URPGPlayerSkillComponent>(Peer->GetArchetype());
		if (!Defaults || Defaults->GetTotalSP() == FixtureTotalSP)
		{
			UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST PRIVACY_FIXTURE_MISMATCH Peer=%s Defaults=%s TotalSP=%d"),
				*It->GetName(), *GetNameSafe(Defaults), Defaults ? Defaults->GetTotalSP() : INDEX_NONE);
			return false;
		}
		++VisiblePeers;
		const float Score = PeerState->GetScore();
		AllPeersReady &= Score == PrivacyReadyScore || Score == PrivacyCheckedScore ||
			Score == PrivacyReleasedScore;
		// Check both wire backing state and public local views. TotalSP uses a
		// server fixture value distinct from the remote class-default baseline.
		const auto ChargeData = Peer->GetSkillSaveData(Definition->SkillTag);
		const auto BudgetData = Peer->GetSkillSaveData(BudgetDefinition->SkillTag);
		if (!ReadReplicatedEntries(*Peer).IsEmpty() || ReadRevision(*Peer) != 0 ||
			Peer->GetTotalSP() != Defaults->GetTotalSP() || ReadUsedSP(*Peer) != 0 ||
			Peer->GetRemainingSP() != Defaults->GetTotalSP() ||
			ChargeData.SkillLevel != DefaultSaveData.SkillLevel ||
			ChargeData.SelectedTripodIndices != DefaultSaveData.SelectedTripodIndices ||
			BudgetData.SkillLevel != DefaultSaveData.SkillLevel ||
			BudgetData.SelectedTripodIndices != DefaultSaveData.SelectedTripodIndices)
		{
			UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST PRIVACY_MISMATCH Peer=%s Entries=%d Revision=%u TotalSP=%d DefaultSP=%d UsedSP=%d RemainingSP=%d ChargeLevel=%d ChargeIndices=%d BudgetLevel=%d BudgetIndices=%d"),
				*It->GetName(), ReadReplicatedEntries(*Peer).Num(), ReadRevision(*Peer),
				Peer->GetTotalSP(), Defaults->GetTotalSP(), ReadUsedSP(*Peer), Peer->GetRemainingSP(),
				ChargeData.SkillLevel, ChargeData.SelectedTripodIndices.Num(),
				BudgetData.SkillLevel, BudgetData.SelectedTripodIndices.Num());
			return false;
		}
	}
	AllPeersReady &= VisiblePeers == ExpectedClients - 1;
	return true;
}

bool FRPGSkillTripodNetworkProbe::RequestRejection(URPGPlayerSkillComponent& Component)
{
	BeginReplyWait(Component);
	if (RejectionIndex == 4)
	{
		return ClickTripod(1, 0);
	}
	if (RejectionIndex == 6)
	{
		const int32 AuthorityBaseline = ReadUsedSP(Component);
		ViewModel->RequestSkillLevelUp(Definition->SkillTag);
		return ViewModel->SkillSlots[0]->SkillLevel == 5 && ViewModel->RemainingSP == 0 &&
			ReadUsedSP(Component) == AuthorityBaseline;
	}
	// Bypass local validation only to send malformed payloads through the real
	// owned skill component RPC; this is not a direct _Implementation call.
	struct FParameters { FGameplayTag SkillTag; int32 TierIndex; int32 OptionIndex; };
	FParameters Parameters{Definition->SkillTag, 0, 0};
	switch (RejectionIndex)
	{
	case 0: Parameters.TierIndex = INDEX_NONE; break;
	case 1: Parameters.TierIndex = 99; break;
	case 2: Parameters.OptionIndex = 99; break;
	case 3: Parameters.OptionIndex = INDEX_NONE - 1; break;
	case 5: Parameters.SkillTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Skill1")); break;
	default: return false;
	}
	UFunction* Function = Component.FindFunctionChecked(TEXT("ServerSelectTripod"));
	check(Function->ParmsSize == sizeof(Parameters));
	Component.ProcessEvent(Function, &Parameters);
	return true;
}

FRPGSkillTripodNetworkProbe::EResult FRPGSkillTripodNetworkProbe::TickClient(
	APlayerController& Controller, URPGPlayerSkillComponent& Component)
{
	int32 VisiblePeers = 0;
	bool AllPeersReady = false;
	if (!CheckRemotePrivacy(Controller, VisiblePeers, AllPeersReady))
		return Fail(TEXT("OtherOwnerSkillSelectionOrSPWasReplicated"));
	if (Phase == EPhase::InstantActivate || Phase == EPhase::ChargeActivate)
		return TickClientActivation(Controller, Phase == EPhase::ChargeActivate);
	if (Phase == EPhase::ChargeRelease)
	{
		auto* ASC = GetASC(Controller);
		auto* Spec = ASC ? ASC->FindAbilitySpecFromHandle(SkillHandle) : nullptr;
		const auto* Ability = Spec ? Cast<URPGGameplayAbility_SkillContainer>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Spec || !Ability || (Spec->IsActive() && !SnapshotMatches(Ability->GetActiveSkillSpec(), true)))
			return Fail(TEXT("ChargeReleaseSnapshotMismatch"));
		if (Spec->IsActive() && ReadSection(Controller, Ability->GetActiveSkillSpec()) == TEXT("Attack") && !bClientSawRelease)
		{
			bClientSawRelease = true;
			UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST CLIENT_RELEASE Section=Attack"));
		}
		if (!Spec->IsActive() && Controller.PlayerState->GetScore() == PrivacyReadyScore)
		{
			if (!bClientSawRelease || !bChargeEndedNormally) return Fail(TEXT("ChargeDidNotFinishReleaseNormally"));
			Phase = EPhase::Privacy;
		}
		return EResult::Waiting;
	}
	if (Phase == EPhase::Level && ExpectedReplies == 1)
	{
		if (Component.GetTotalSP() != FixtureTotalSP)
		{
			return EResult::Waiting;
		}
		ViewModel->RefreshSkillData();
		BeginReplyWait(Component, 3);
		for (int32 Level = 1; Level < 4; ++Level)
		{
			ViewModel->RequestSkillLevelUp(Definition->SkillTag);
		}
	}
	if (!RepliesArrived(Component))
	{
		return EResult::Waiting;
	}
	switch (Phase)
	{
	case EPhase::Level:
		if (!CheckState(Component, 4, INDEX_NONE, 2)) return Fail(TEXT("LevelConfirmationMismatch"));
		BeginReplyWait(Component);
		if (!ClickTripod(0, 0)) return Fail(TEXT("FirstNativeTripodButtonDidNotSelect"));
		Phase = EPhase::Select;
		break;
	case EPhase::Select:
		if (!CheckState(Component, 4, 0, 2)) return Fail(TEXT("TripodSelectionMismatch"));
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST CLIENT_SELECTION_CONFIRMED Option=0"));
		Phase = EPhase::InstantActivate;
		break;
	case EPhase::Reject:
		if (!CheckState(Component, 4, 0, 2))
		{
			UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST ROLLBACK_MISMATCH Case=%d Level=%d SP=%d ViewModelSP=%d"),
				RejectionIndex, Component.GetSkillSaveData(Definition->SkillTag).SkillLevel,
				Component.GetRemainingSP(), ViewModel->RemainingSP);
			return Fail(TEXT("RejectedRequestDidNotRestoreSelectionAndSP"));
		}
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST CLIENT_REJECTED Case=%d Rollback=1 SP=2"), RejectionIndex);
		if (++RejectionIndex < SelectionRejectionCount)
		{
			if (!RequestRejection(Component)) return Fail(TEXT("RejectionRequestDidNotPredict"));
		}
		else
		{
			BeginReplyWait(Component);
			if (!ClickTripod(0, 1)) return Fail(TEXT("SecondNativeTripodButtonDidNotSelect"));
			Phase = EPhase::Switch;
		}
		break;
	case EPhase::Switch:
	{
		if (!CheckState(Component, 4, 1, 2)) return Fail(TEXT("TripodSwitchMismatch"));
		FRPGSkillRuntimeSpec Spec;
		Definition->BuildRuntimeSpec(Controller.GetPawn(), Component.GetSkillSaveData(Definition->SkillTag), Spec);
		if (Spec.GetStatScalar(FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.Damage"))) != 3.0f)
			return Fail(TEXT("ResolvedTripodScalarMismatch"));
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST CLIENT_SWITCH_CONFIRMED Option=1 Scalar=3"));
		Phase = EPhase::ChargeActivate;
		break;
	}
	case EPhase::Privacy:
		if (!AllPeersReady || !Controller.PlayerState ||
			Controller.PlayerState->GetScore() != PrivacyReadyScore)
		{
			PrivacyReadySince = 0.0;
			break;
		}
		if (PrivacyReadySince == 0.0) PrivacyReadySince = FPlatformTime::Seconds();
		if (FPlatformTime::Seconds() - PrivacyReadySince < 1.0) break;
		if (!CheckState(Component, 4, 1, 2)) return Fail(TEXT("PrivacyOwnerPositiveControlMismatch"));
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST CLIENT_OWNER_ONLY_CONFIRMED Peers=%d Entries=0 Revision=0 SPBaseline=1"), VisiblePeers);
		BeginReplyWait(Component);
		ViewModel->RequestSkillLevelMax(BudgetDefinition->SkillTag);
		Phase = EPhase::Budget;
		break;
	case EPhase::Budget:
		if (!CheckState(Component, 4, 1, 0) || !CheckBudgetState(Component, 3) || ReadUsedSP(Component) != 5)
			return Fail(TEXT("BudgetWasNotExhaustedBelowLevelCap"));
		if (!Controller.PlayerState || Controller.PlayerState->GetScore() != PrivacyReleasedScore) break;
		if (Component.TryLevelUpSkill(BudgetDefinition->SkillTag) || !CheckBudgetState(Component, 3))
			return Fail(TEXT("LocalUIAcceptedInsufficientSP"));
		BeginReplyWait(Component);
		{
			struct FParameters { FGameplayTag SkillTag; } Parameters{BudgetDefinition->SkillTag};
			UFunction* Function = Component.FindFunctionChecked(TEXT("ServerTryLevelUpSkill"));
			check(Function->ParmsSize == sizeof(Parameters));
			Component.ProcessEvent(Function, &Parameters);
		}
		Phase = EPhase::BudgetReject;
		break;
	case EPhase::BudgetReject:
		if (!CheckState(Component, 4, 1, 0) || !CheckBudgetState(Component, 3) || ReadUsedSP(Component) != 5)
			return Fail(TEXT("AuthorityAcceptedInsufficientSP"));
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST CLIENT_REJECTED Case=7 Reason=InsufficientSP Level=3 Max=10 Cost=1 SP=0"));
		BeginReplyWait(Component);
		ViewModel->RequestSkillLevelMin(BudgetDefinition->SkillTag);
		Phase = EPhase::BudgetReset;
		break;
	case EPhase::BudgetReset:
		if (!CheckState(Component, 4, 1, 2) || !CheckBudgetState(Component, 1))
			return Fail(TEXT("BudgetSkillRefundMismatch"));
		BeginReplyWait(Component);
		ViewModel->RequestSkillLevelDown(Definition->SkillTag);
		Phase = EPhase::LevelDown;
		break;
	case EPhase::LevelDown:
		if (!CheckState(Component, 3, INDEX_NONE, 3)) return Fail(TEXT("LevelDownDidNotClearTripodAndRefund"));
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST CLIENT_LOCKED_TIER_CLEARED Level=3 SP=3"));
		BeginReplyWait(Component);
		ViewModel->RequestSkillLevelMin(Definition->SkillTag);
		Phase = EPhase::Reset;
		break;
	case EPhase::Reset:
		if (!CheckState(Component, 1, INDEX_NONE, FixtureTotalSP) || !CheckBudgetState(Component, 1))
			return Fail(TEXT("FullRefundMismatch"));
		if (!bClientSawInstant || !bClientSawCharge || !bClientSawRelease || !bChargeEndedNormally)
			return Fail(TEXT("ClientActivationCoverageMissing"));
		if (!AssetCooldownProbe) AssetCooldownProbe = MakeShared<FRPGSkillAssetCooldownProbe>();
		{
			const auto Result = AssetCooldownProbe->Tick(Controller);
			if (Result == FRPGSkillAssetCooldownProbe::EResult::Failed) return Fail(*AssetCooldownProbe->GetFailure());
			if (Result != FRPGSkillAssetCooldownProbe::EResult::Passed) break;
		}
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST CLIENT_COMPLETE Rejections=8 FullRefund=1 OwnerOnly=1 InsufficientSP=1 NativeButtons=1 Activations=2 ChargeRelease=1 AssetCooldown=1"));
		Phase = EPhase::Finished;
		return EResult::Passed;
	default: break;
	}
	return EResult::Waiting;
}

FRPGSkillTripodNetworkProbe::EResult FRPGSkillTripodNetworkProbe::TickServer(
	APlayerController& Controller, URPGPlayerSkillComponent& Component)
{
	const FRPGSkillSaveData Data = Component.GetSkillSaveData(Definition->SkillTag);
	const FRPGSkillSaveData Budget = Component.GetSkillSaveData(BudgetDefinition->SkillTag);
	const int32 Option = Data.SelectedTripodIndices.IsValidIndex(0) ? Data.SelectedTripodIndices[0] : INDEX_NONE;
	if (!AssetCooldownProbe && !ObserveServerActivation(Controller)) return Fail(TEXT("ServerSelectedTripodActivationSnapshotMismatch"));
	if (Data.SkillLevel > 4 || Budget.SkillLevel > 3 || Component.GetRemainingSP() < 0 ||
		(Data.SelectedTripodIndices.IsValidIndex(1) && Data.SelectedTripodIndices[1] != INDEX_NONE))
		return Fail(TEXT("AuthorityAcceptedLockedSelectionOrOverLevel"));
	bSawFirstSelection |= Data.SkillLevel == 4 && Option == 0;
	if (Data.SkillLevel == 4 && Option == 1)
	{
		bSawSecondSelection = true;
		auto* ASC = GetASC(Controller);
		const auto* AbilitySpec = ASC ? FindChargeSpec(*ASC) : nullptr;
		const auto* Ability = AbilitySpec
			? Cast<URPGGameplayAbility_SkillContainer>(AbilitySpec->GetPrimaryInstance()) : nullptr;
		if (bServerSawInstant && !bServerSawCharge && AbilitySpec && !AbilitySpec->IsActive() &&
			Ability && Controller.PlayerState && Controller.PlayerState->GetScore() != ChargeCooldownReadyScore &&
			Ability->CheckCooldown(AbilitySpec->Handle, ASC->AbilityActorInfo.Get()))
		{
			SetCheckpoint(*Controller.PlayerState, ChargeCooldownReadyScore);
			UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST SERVER_COOLDOWN_READY Remaining=%f Controller=%s"),
				Ability->GetCooldownTimeRemaining(ASC->AbilityActorInfo.Get()), *Controller.GetName());
		}
		if (bServerSawRelease && bChargeEndedNormally && AbilitySpec && !AbilitySpec->IsActive() && Controller.PlayerState &&
			Controller.PlayerState->GetScore() != PrivacyReleasedScore)
		{
			if (Budget.SkillLevel == 3 && Component.GetRemainingSP() == 0)
				SetCheckpoint(*Controller.PlayerState, PrivacyCheckedScore);
			else if (Budget.SkillLevel == 1)
				SetCheckpoint(*Controller.PlayerState, PrivacyReadyScore);
		}
		FRPGSkillRuntimeSpec Spec;
		Definition->BuildRuntimeSpec(Controller.GetPawn(), Data, Spec);
		if (Spec.GetStatScalar(FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.Damage"))) != 3.0f)
			return Fail(TEXT("ServerResolvedTripodScalarMismatch"));
	}
	// Release the stable-state hold only once every configured owner has sent
	// its budget request after observing private-data isolation on its peers.
	if (auto* GameState = Controller.GetWorld()->GetGameState())
	{
		int32 CheckedOwners = 0;
		for (const auto& PlayerState : GameState->PlayerArray)
			if (PlayerState && PlayerState->GetScore() == PrivacyCheckedScore) ++CheckedOwners;
		if (CheckedOwners == ExpectedClients)
			for (const auto& PlayerState : GameState->PlayerArray)
				if (PlayerState && PlayerState->GetScore() == PrivacyCheckedScore)
					SetCheckpoint(*PlayerState, PrivacyReleasedScore);
	}
	bSawBudgetLimit |= Budget.SkillLevel == 3 && Component.GetRemainingSP() == 0;
	bSawBudgetRefund |= bSawBudgetLimit && Budget.SkillLevel == 1 && Data.SkillLevel == 4 &&
		Component.GetRemainingSP() == 2;
	bSawLockedTierCleared |= bSawSecondSelection && Data.SkillLevel == 3 && Option == INDEX_NONE;
	if (bServerSawInstant && bServerSawCharge && bServerSawRelease && bChargeEndedNormally &&
		bSawFirstSelection && bSawSecondSelection && bSawLockedTierCleared && bSawBudgetLimit &&
		bSawBudgetRefund && Budget.SkillLevel == 1 && Data.SkillLevel == 1 &&
		Option == INDEX_NONE && Component.GetRemainingSP() == FixtureTotalSP)
	{
		if (!AssetCooldownProbe) AssetCooldownProbe = MakeShared<FRPGSkillAssetCooldownProbe>();
		const auto Result = AssetCooldownProbe->Tick(Controller);
		if (Result == FRPGSkillAssetCooldownProbe::EResult::Failed) return Fail(*AssetCooldownProbe->GetFailure());
		if (Result != FRPGSkillAssetCooldownProbe::EResult::Passed) return EResult::Waiting;
		if (Controller.PlayerState) SetCheckpoint(*Controller.PlayerState, OriginalPlayerScore);
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_TRIPOD_NETTEST SERVER_COMPLETE Selections=2 TierCleared=1 FullRefund=1 InsufficientSP=1 PrivacyBarrier=1 Activations=2 ChargeRelease=1 AssetCooldown=1 Controller=%s"), *Controller.GetName());
		return EResult::Passed;
	}
	return EResult::Waiting;
}

FRPGSkillTripodNetworkProbe::EResult FRPGSkillTripodNetworkProbe::Tick(APlayerController& Controller)
{
	APawn* Pawn = Controller.GetPawn();
	URPGPlayerSkillComponent* Component = Pawn ? Pawn->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	UGameInstance* GameInstance = Controller.GetGameInstance();
	URPGSkillCatalogSubsystem* Catalog = GameInstance ? GameInstance->GetSubsystem<URPGSkillCatalogSubsystem>() : nullptr;
	if (!Component || !Catalog || !Controller.PlayerState) return EResult::Waiting;
	if (!Catalog->IsCatalogReady())
	{
		Catalog->RequestSkillDefinitions();
		return EResult::Waiting;
	}
	if (Phase == EPhase::Initialize && !Initialize(Controller, *Component, *Catalog))
		return EResult::Failed;
	// The authored ability retains its authored Definition pointer. Bind only
	// this disposable, inactive ability instance to the same transient fixture
	// used by its catalog/ViewModel; do not mutate an ability CDO or asset.
	if (auto* ASC = GetASC(Controller))
		if (auto* Spec = FindChargeSpec(*ASC))
		{
			SkillHandle = Spec->Handle;
			ObserveAbilityEnd(Controller, *ASC);
			if (auto* Ability = Cast<URPGGameplayAbility_SkillContainer>(Spec->GetPrimaryInstance());
				!AssetCooldownProbe && !Spec->IsActive() && Ability)
			{
				const auto* Property = FindFProperty<FObjectPropertyBase>(
					URPGGameplayAbility_SkillContainer::StaticClass(), TEXT("SkillDefinition"));
				check(Property);
				Property->SetObjectPropertyValue_InContainer(Ability, Definition.Get());
			}
		}
	return Controller.HasAuthority() ? TickServer(Controller, *Component) : TickClient(Controller, *Component);
}
