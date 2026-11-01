#include "Tests/RPGCharacterProgressionCombatProbe.h"
#include "Component/Combat/RPGCombatContributionComponent.h"

#include "AIController.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Controller/RPGPlayerController.h"
#include "DataTable/SkillData.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect/GE_CoolDown.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PlayerClass/RPGPlayerClassComponent.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "Progression/RPGCharacterProgressionComponent.h"
#include "RPGGameplayTags.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillDefinition.h"
#include "Tests/RPGSkillDamageNetworkProbe.h"
#include "UI/Content/RPGCombatContributionWidget.h"
#include "UI/HUD/RPGHUD.h"
#include "UObject/UObjectIterator.h"

namespace
{
constexpr float AuthoredChargeCooldown = 12.f;
const FVector FixtureLocation(0.f, 0.f, 10000.f);
const FVector Offsets[] = { FVector(300,0,0), FVector(-300,0,0), FVector(300,120,0), FVector(1500,0,0) };
FString Gate(const FString& Root, int32 Round, const TCHAR* Suffix)
{ return FPaths::Combine(Root, FString::Printf(TEXT("combat-%d.%s"), Round, Suffix)); }
bool Exists(const FString& Path) { return FPaths::FileExists(Path); }
bool Write(const FString& Path) { return FFileHelper::SaveStringToFile(TEXT("ready"), *Path); }
FGameplayEffectQuery CooldownQuery(const URPGSkillDefinition* Definition)
{
	FGameplayEffectQuery Query;
	Query.EffectDefinition = UGE_CoolDown::StaticClass();
	Query.EffectSource = Definition;
	return Query;
}
}

FRPGCharacterProgressionCombatProbe::~FRPGCharacterProgressionCombatProbe()
{
	if (Source.IsValid()) Source->RemoveGameplayEventTagContainerDelegate(FGameplayTagContainer(RPGGameplayTags::Player_Event_AOE), EventHandle);
	for (int32 Index = 0; Index < 4; ++Index)
		if (Targets[Index].IsValid()) Targets[Index]->ASC()->OnGameplayEffectAppliedDelegateToSelf.Remove(Effects[Index]);
}

bool FRPGCharacterProgressionCombatProbe::Fail(const TCHAR* Reason)
{
	if (Error.IsEmpty()) Error = Reason;
	return false;
}

bool FRPGCharacterProgressionCombatProbe::ObserveContributionHUD(APlayerController& PC)
{
	auto* Controller = Cast<ARPGPlayerController>(&PC);
	if (!Controller || !PC.IsLocalController()) return Fail(TEXT("ContributionHUDOwnerContext"));
	if (!PC.GetHUD()) return false;
	if (!Cast<ARPGHUD>(PC.GetHUD())) return Fail(TEXT("ContributionHUDNativeHUDMissing"));
	URPGCombatContributionWidget* Widget = nullptr;
	int32 OwnerWidgets = 0, WorldWidgets = 0;
	for (TObjectIterator<URPGCombatContributionWidget> It; It; ++It)
	{
		if (It->GetOwningPlayer() != &PC) continue;
		++OwnerWidgets;
		if (It->GetWorld() != PC.GetWorld()) continue;
		++WorldWidgets;
		if (!It->IsInViewport()) continue;
		if (Widget) return Fail(TEXT("ContributionHUDDuplicateViewportWidget"));
		Widget = *It;
	}
	if (!bLoggedContributionHUDContext)
	{
		UE_LOG(LogTemp, Display, TEXT("PROGRESSION_CONTRIBUTION_HUD CONTEXT OwnerWidgets=%d CurrentWorldWidgets=%d Viewport=%d"), OwnerWidgets, WorldWidgets, Widget != nullptr);
		bLoggedContributionHUDContext = true;
		if (Widget)
		{
			const FVector2D Size = Widget->GetCachedGeometry().GetLocalSize();
			UE_LOG(LogTemp, Display, TEXT("PROGRESSION_CONTRIBUTION_HUD CONTEXT Geometry=%g,%g TickFrequency=%d DisplayedTarget=%d"), Size.X, Size.Y,
				static_cast<int32>(Widget->GetDesiredTickFrequency()), Widget->GetDisplayedTarget() != nullptr);
		}
	}
	if (!Widget) return false; // Observe automatic HUD creation, never create/refresh a test widget.
	auto* Card = Cast<UBorder>(Widget->GetWidgetFromName(TEXT("ContributionCard")));
	auto* Stagger = Cast<UProgressBar>(Widget->GetWidgetFromName(TEXT("StaggerContributionMeter")));
	auto* Part = Cast<UProgressBar>(Widget->GetWidgetFromName(TEXT("PartBreakContributionMeter")));
	auto* StaggerText = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("StaggerContributionText")));
	auto* PartText = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("PartBreakContributionText")));
	if (!Card || !Stagger || !Part || !StaggerText || !PartText) return Fail(TEXT("ContributionHUDControlsMissing"));
	if (Widget->GetVisibility() != ESlateVisibility::HitTestInvisible || Widget->IsFocusable())
		return Fail(TEXT("ContributionHUDInterceptsInput"));
	if (Round == 1 && ContributionHUDStep == 2)
	{
		if (Widget->GetDisplayedTarget() || Card->GetVisibility() != ESlateVisibility::Collapsed
			|| Stagger->GetPercent() != 0.f || Part->GetPercent() != 0.f
			|| !StaggerText->GetText().IsEmpty() || !PartText->GetText().IsEmpty()) return false;
		Controller->SetSkillLockedTarget(Targets[0].Get());
		ContributionHUDStep = 3;
		return false;
	}
	const bool bOtherTarget = Round == 1 && ContributionHUDStep == 1;
	AActor* ExpectedTarget = Targets[bOtherTarget ? 1 : 0].Get();
	const float ExpectedUnits = bOtherTarget ? 0.f : FMath::Min(100.f, Round * 20.f);
	const FString Numbers = FText::AsNumber(ExpectedUnits).ToString() + TEXT("/100");
	if (Widget->GetDisplayedTarget() != ExpectedTarget || Card->GetVisibility() == ESlateVisibility::Collapsed
		|| !FMath::IsNearlyEqual(Stagger->GetPercent(), ExpectedUnits / 100.f)
		|| !FMath::IsNearlyEqual(Part->GetPercent(), ExpectedUnits / 100.f)
		|| !StaggerText->GetText().ToString().Contains(Numbers) || !PartText->GetText().ToString().Contains(Numbers)
		|| StaggerText->GetText().ToString().Contains(TEXT("완료")) != (ExpectedUnits == 100.f)
		|| PartText->GetText().ToString().Contains(TEXT("완료")) != (ExpectedUnits == 100.f)) return false;
	if (Round == 1 && ContributionHUDStep == 0)
	{
		Controller->SetSkillLockedTarget(Targets[1].Get());
		ContributionHUDStep = 1;
		return false;
	}
	if (Round == 1 && ContributionHUDStep == 1)
	{
		Controller->SetSkillLockedTarget(nullptr);
		ContributionHUDStep = 2;
		return false;
	}
	if (Round == 1 && ContributionHUDStep == 3) ContributionHUDStep = 4;
	UE_LOG(LogTemp, Display, TEXT("PROGRESSION_CONTRIBUTION_HUD OBSERVED Round=%d Units=%g AutoViewport=1 NativeTick=1 SwitchStep=%d"),
		Round, ExpectedUnits, ContributionHUDStep);
	return true;
}

bool FRPGCharacterProgressionCombatProbe::Prepare(APlayerController& PC, URPGAbilitySystemComponent& ASC)
{
	auto* Pawn = Cast<ARPGPlayer>(PC.GetPawn());
	const auto* Team = Cast<IGenericTeamAgentInterface>(&PC);
	const auto* Attributes = ASC.GetSet<URPGAttributeSet>();
	if (!Pawn || !Team || Team->GetGenericTeamId().GetId() == 255 || !Attributes) return Fail(TEXT("CombatFixtureContext"));
	// Stationary geometry fixture above the arena; admission, restored grants,
	// authored montage/events and all server targeting/damage checks stay active.
	Pawn->GetCharacterMovement()->DisableMovement();
	Pawn->SetActorLocation(FixtureLocation, false, nullptr, ETeleportType::TeleportPhysics);
	Pawn->SetActorRotation(FRotator::ZeroRotator);
	Pawn->FindComponentByClass<URPGSecurityValidationComponent>()->ResetMovementBaseline();
	Pawn->ForceNetUpdate();
	ASC.SetNumericAttributeBase(URPGAttributeSet::GetCriticalChanceAttribute(), 0.f);
	InitialMana = Attributes->GetCurrentMana();
	if (Attributes->GetCurrentIdentityGauge() != 0.f || Attributes->GetMaxIdentityGauge() != 100.f) return Fail(TEXT("CombatInitialGauge"));
	Source = &ASC;
	EventHandle = ASC.AddGameplayEventTagContainerDelegate(FGameplayTagContainer(RPGGameplayTags::Player_Event_AOE),
		FGameplayEventTagMulticastDelegate::FDelegate::CreateLambda([this](FGameplayTag, const FGameplayEventData*) { ++EventCount; }));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Target = PC.GetWorld()->SpawnActor<ARPGSkillDamageNetworkTarget>(FixtureLocation + Offsets[Index], FRotator::ZeroRotator, Params);
		auto* Controller = PC.GetWorld()->SpawnActor<AAIController>();
		if (!Target || !Controller) return Fail(TEXT("CombatTargetSpawn"));
		FRPGCombatContributionReceiverProfile ContributionProfile;
		ContributionProfile.bEnabled = true;
		if (!Target->Contributions()->ConfigureForNewAttempt(ContributionProfile)) return Fail(TEXT("CombatContributionSetup"));
		Controller->SetGenericTeamId(FGenericTeamId(Index == 2 ? Team->GetGenericTeamId().GetId() : (Team->GetGenericTeamId().GetId() == 0 ? 1 : 0)));
		Controller->Possess(Target);
		Target->TestIndex = Index;
		Target->FixtureSourceLocation = FixtureLocation;
		Targets[Index] = Target;
		Effects[Index] = Target->ASC()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
			[this, Index](UAbilitySystemComponent* Applying, const FGameplayEffectSpec& Effect, FActiveGameplayEffectHandle)
			{
				if (!Effect.Def || Effect.Def->GetClass()->GetName() != TEXT("GE_Shared_ApplyDamage_C")) return;
				++EffectCounts[Index];
				const float Magnitude = Effect.GetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, false, -1.f);
				if (Index != 0 || Applying != Source.Get() || !FMath::IsNearlyEqual(Magnitude, ExpectedSetByCaller, 0.01f))
					Fail(TEXT("CombatUnexpectedDamageEffect"));
			});
	}
	bPrepared = true;
	return true;
}

bool FRPGCharacterProgressionCombatProbe::Tick(APlayerController& PC, URPGAbilitySystemComponent& ASC,
	FGameplayAbilitySpecHandle Handle, UQuickSlotComponent& Slots, const FString& Gates, const FString& Choice)
{
	if (bPassed || !Error.IsEmpty()) return bPassed;
	const auto* Spec = ASC.FindAbilitySpecFromHandle(Handle);
	auto* Skill = Spec ? Cast<URPGGameplayAbility_SkillContainer>(Spec->GetPrimaryInstance()) : nullptr;
	if (!Skill) return Fail(TEXT("CombatMissingSkill"));
	const auto* Row = Skill->GetSkillDefinition()->SkillDataHandle.GetRow<FRPGSkillDataTable>(TEXT("ProgressionCombat"));
	if (!Row || Row->ManaCost != 0.f || Skill->GetCostGameplayEffect()) return Fail(TEXT("CombatAuthoredCostChanged"));
	return PC.HasAuthority() ? TickServer(PC, ASC, Handle, *Skill, Gates, Choice)
		: TickClient(PC, ASC, Handle, *Skill, Slots, Gates, Choice);
}

bool FRPGCharacterProgressionCombatProbe::TickServer(APlayerController& PC, URPGAbilitySystemComponent& ASC,
	FGameplayAbilitySpecHandle Handle, URPGGameplayAbility_SkillContainer& Skill, const FString& Gates, const FString& Choice)
{
	if (!bPrepared && !Prepare(PC, ASC)) return false;
	const auto* Spec = ASC.FindAbilitySpecFromHandle(Handle);
	const auto* Attributes = ASC.GetSet<URPGAttributeSet>();
	const auto Cooldowns = ASC.GetActiveEffects(CooldownQuery(Skill.GetSkillDefinition()));
	if (Attributes->GetCurrentMana() != InitialMana) return Fail(TEXT("CombatUnexpectedManaSpend"));
	if (Phase == 0 && Exists(FPaths::Combine(Gates, TEXT("cast.ready"))))
	{
		if (Round == 6)
		{
			const auto* Definition = PC.GetPawn()->FindComponentByClass<URPGPlayerSkillComponent>()->GetPlayerClassDefinition();
			const auto* IdentitySpec = ASC.FindAbilitySpecFromClass(Definition->IdentityAbility);
			const auto* Identity = IdentitySpec ? IdentitySpec->GetPrimaryInstance() : nullptr;
			FGameplayTagContainer FailureTags;
			const bool bEligible = Identity && Identity->CanActivateAbility(IdentitySpec->Handle, ASC.AbilityActorInfo.Get(), nullptr, nullptr, &FailureTags);
			UE_LOG(LogTemp, Display, TEXT("PROGRESSION_COMBAT_E2E IDENTITY_GATE Eligible=%d Input=%s Bound=%d AssetTags=%s Blocked=%s Failure=%s"),
				bEligible, *Definition->IdentityInputTag.ToString(), IdentitySpec && IdentitySpec->GetDynamicSpecSourceTags().HasTagExact(Definition->IdentityInputTag),
				Identity ? *Identity->GetAssetTags().ToStringSimple() : TEXT("Missing"), *ASC.GetBlockedAbilityTags().ToStringSimple(), *FailureTags.ToStringSimple());
			if (!bEligible) return Fail(TEXT("CombatIdentityNotActivatable"));
		}
		if (!Write(Gate(Gates, Round, TEXT("ready")))) return Fail(TEXT("CombatCommandWrite"));
		Phase = 1;
	}
	if (Phase == 1 && Spec->IsActive() && Cooldowns.Num() == 1)
	{
		if (Skill.GetSkillActivationSerial() != Serial + 1) return Fail(TEXT("CombatActivationMultiplicity"));
		Serial = Skill.GetSkillActivationSerial();
		const auto& Runtime = Skill.GetActiveSkillSpec();
		ExpectedDuration = Runtime.GetCooldownDuration();
		const auto* Effect = ASC.GetActiveGameplayEffect(Cooldowns[0]);
		if (!Effect || !FMath::IsNearlyEqual(Effect->Spec.GetDuration(), ExpectedDuration) || ExpectedDuration != AuthoredChargeCooldown)
			return Fail(TEXT("CombatCooldownCommit"));
		ExpectedSetByCaller = Runtime.BaseDamage * Runtime.GetStatScalar(FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.Damage"))) * (Round == 6 ? 1.25f : 1.f);
		const float Damage = static_cast<int32>(ExpectedSetByCaller * Attributes->GetAttack());
		if (!FMath::IsFinite(Damage) || Damage < 1.f || Damage > 100000.f) return Fail(TEXT("CombatDamageBounds"));
		if (Round == 1) BaseDamage = Damage;
		if (Round == 6)
		{
			BoostDamage = Damage;
			if (Attributes->GetCurrentIdentityGauge() != 0.f) return Fail(TEXT("CombatIdentityCostNotConsumed"));
		}
		if (Round > 0) TotalDamage += Damage;
		Targets[0]->ExpectedHealth = ARPGSkillDamageNetworkTarget::BaselineHealth - TotalDamage;
		Targets[0]->ForceNetUpdate();
		const float Before = Skill.GetCooldownTimeRemaining(ASC.AbilityActorInfo.Get());
		PC.GetPawn()->FindComponentByClass<URPGCharacterProgressionComponent>()->NotifyOwnerContextChanged();
		PC.GetPawn()->FindComponentByClass<URPGPlayerClassComponent>()->RefreshAppliedClass();
		if (!ASC.FindAbilitySpecFromHandle(Handle) || !ASC.FindAbilitySpecFromHandle(Handle)->IsActive()
			|| Skill.GetCooldownTimeRemaining(ASC.AbilityActorInfo.Get()) > Before + 0.02f) return Fail(TEXT("CombatRefreshReset"));
		UE_LOG(LogTemp, Display, TEXT("PROGRESSION_COMBAT_E2E COMMIT Round=%d Choice=%s Cooldown=%g ManaCost=0 ExpectedDamage=%g Gauge=%g"), Round, *Choice, ExpectedDuration, Round ? Damage : 0.f, Attributes->GetCurrentIdentityGauge());
		Phase = 2;
	}
	if (Phase == 2 && !Spec->IsActive())
	{
		const int32 ExpectedHits = Round;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (!Targets[Index].IsValid() || EffectCounts[Index] != (Index == 0 ? ExpectedHits : 0)
				|| Targets[Index]->Attributes()->GetCurrentHealth() != Targets[Index]->ExpectedHealth)
				return Fail(TEXT("CombatServerDamageOrFilterMismatch"));
			const auto Contribution = Targets[Index]->Contributions()->GetContributionState();
			const float ExpectedUnits = Index == 0 ? FMath::Min(100.f, ExpectedHits * 20.f) : 0.f;
			if (!Contribution.bEnabled || Contribution.Stagger != ExpectedUnits || Contribution.PartBreak != ExpectedUnits
				|| Contribution.bStaggerComplete != (ExpectedUnits == 100.f) || Contribution.bPartBroken != (ExpectedUnits == 100.f))
				return Fail(TEXT("CombatServerContributionMismatch"));
		}
		const float Gauge = Round == 6 ? 20.f : Round * 20.f;
		if (EventCount != ExpectedHits || Attributes->GetCurrentIdentityGauge() != Gauge) return Fail(TEXT("CombatNotifyOrGaugeMultiplicity"));
		if (!Write(Gate(Gates, Round, TEXT("done")))) return Fail(TEXT("CombatResultWrite"));
		UE_LOG(LogTemp, Display, TEXT("PROGRESSION_COMBAT_E2E HIT Round=%d Effects=%d Events=%d Health=%g Gauge=%g Excluded=3"), Round, EffectCounts[0], EventCount, Targets[0]->Attributes()->GetCurrentHealth(), Gauge);
		Phase = 3;
	}
	if (Phase == 3 && Exists(Gate(Gates, Round, TEXT("observed"))) && Cooldowns.IsEmpty())
	{
		if (!static_cast<const UGameplayAbility&>(Skill).CheckCooldown(Handle, ASC.AbilityActorInfo.Get())) return Fail(TEXT("CombatCooldownExpiry"));
		if (Round == 6)
		{
			const FString Body = FString::Printf(TEXT("{\"choice\":\"%s\",\"successfulHits\":6,\"cancelledHits\":0,\"excludedTargets\":3,\"baseDamage\":%g,\"empoweredDamage\":%g,\"totalDamage\":%g,\"manaCost\":0,\"cooldown\":%g,\"identityEarned\":100,\"identitySpent\":100,\"finalGauge\":20,\"stagger\":100,\"partBreak\":100,\"contributionPerTargetPerActivation\":20,\"excludedContributionTargets\":3}"), *Choice, BaseDamage, BoostDamage, TotalDamage, ExpectedDuration);
			if (!FFileHelper::SaveStringToFile(Body, *FPaths::Combine(Gates, TEXT("Combat.proof.json")))) return Fail(TEXT("CombatProofWrite"));
			UE_LOG(LogTemp, Display, TEXT("PROGRESSION_COMBAT_E2E PASS Role=Server Choice=%s Hits=6 Cancel=1 IdentityCost=100"), *Choice);
			bPassed = true;
			return true;
		}
		++Round;
		Phase = 0;
	}
	return false;
}

bool FRPGCharacterProgressionCombatProbe::TickClient(APlayerController& PC, URPGAbilitySystemComponent& ASC,
	FGameplayAbilitySpecHandle Handle, URPGGameplayAbility_SkillContainer& Skill, UQuickSlotComponent& Slots,
	const FString& Gates, const FString& Choice)
{
	for (TActorIterator<ARPGSkillDamageNetworkTarget> It(PC.GetWorld()); It; ++It)
		if (It->TestIndex >= 0 && It->TestIndex < 4) Targets[It->TestIndex] = *It;
	for (auto& Target : Targets) if (!Target.IsValid() || !Target->ASC()->GetAvatarActor() || !Target->Contributions()->GetContributionState().bEnabled) return false;
	if (!PC.GetPawn()->GetActorLocation().Equals(FixtureLocation, 2.f)) return false;
	const auto* Attributes = ASC.GetSet<URPGAttributeSet>();
	if (!Attributes) return false;
	if (!bPrepared) { InitialMana = Attributes->GetCurrentMana(); bPrepared = true; }
	const auto* Spec = ASC.FindAbilitySpecFromHandle(Handle);
	const auto Cooldowns = ASC.GetActiveEffects(CooldownQuery(Skill.GetSkillDefinition()));
	for (const auto EffectHandle : Cooldowns)
	{
		const auto* Effect = ASC.GetActiveGameplayEffect(EffectHandle);
		if (Effect && Effect->PredictionKey.WasReceived() && FMath::IsNearlyEqual(Effect->Spec.GetDuration(), AuthoredChargeCooldown)) bSawReceivedCooldown = true;
	}
	if (Phase == 0 && Exists(Gate(Gates, Round, TEXT("ready"))))
	{
		PC.SetControlRotation(FRotator::ZeroRotator);
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGProgressionContributionHUD")))
		{
			auto* Controller = Cast<ARPGPlayerController>(&PC);
			if (!Controller) return Fail(TEXT("ContributionHUDPlayerControllerMissing"));
			Controller->SetSkillLockedTarget(Targets[0].Get());
		}
		if (Round == 6)
		{
			if (Attributes->GetCurrentIdentityGauge() != 100.f) return false;
			const auto* Definition = PC.GetPawn()->FindComponentByClass<URPGPlayerSkillComponent>()->GetPlayerClassDefinition();
			const auto* Identity = ASC.FindAbilitySpecFromClass(Definition->IdentityAbility);
			if (!Identity || !Definition->IdentityInputTag.IsValid() || !Identity->GetDynamicSpecSourceTags().HasTagExact(Definition->IdentityInputTag))
				return Fail(TEXT("CombatIdentityInputNotBound"));
			ASC.OnAbilityInputPressed(Definition->IdentityInputTag);
			ASC.OnAbilityInputReleased(Definition->IdentityInputTag);
		}
		PhaseAt = FPlatformTime::Seconds();
		Phase = 1;
		return false;
	}
	if (Phase == 1 && Round == 6 && FPlatformTime::Seconds() - PhaseAt > 5.0 && Attributes->GetCurrentIdentityGauge() != 0.f)
		return Fail(TEXT("CombatIdentityInputDidNotConsume"));
	if (Phase == 1 && FPlatformTime::Seconds() - PhaseAt > 0.8 && (Round != 6 || Attributes->GetCurrentIdentityGauge() == 0.f))
	{
		if (!static_cast<const UGameplayAbility&>(Skill).CanActivateAbility(Handle, ASC.AbilityActorInfo.Get())) return Fail(TEXT("CombatFreshInputRejected"));
		Serial = Skill.GetSkillActivationSerial();
		Slots.BeginUseSkillSlot(0);
		PhaseAt = FPlatformTime::Seconds();
		Phase = 2;
		bSawReceivedCooldown = false;
		UE_LOG(LogTemp, Display, TEXT("PROGRESSION_COMBAT_E2E INPUT Round=%d Choice=%s"), Round, *Choice);
	}
	const double Hold = Round == 0 ? 0.15 : (Choice == TEXT("FullCharge") ? 1.5 : 1.0);
	if (Phase == 2 && FPlatformTime::Seconds() - PhaseAt > Hold)
	{
		Slots.EndUseSkillSlot(0);
		Phase = 3;
	}
	if (Phase == 3 && Exists(Gate(Gates, Round, TEXT("done"))) && !Spec->IsActive())
	{
		if (!bSawReceivedCooldown) return Fail(TEXT("CombatReceivedCooldownMissing"));
		if (Attributes->GetCurrentMana() != InitialMana) return Fail(TEXT("CombatOwnerManaMismatch"));
		const float Gauge = Round == 6 ? 20.f : Round * 20.f;
		if (Attributes->GetCurrentIdentityGauge() != Gauge) return false;
		for (auto& Target : Targets)
			if (Target->Attributes()->GetCurrentHealth() != Target->ExpectedHealth) return false;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const auto State = Targets[Index]->Contributions()->GetContributionState();
			const float Expected = Index == 0 ? FMath::Min(100.f, Round * 20.f) : 0.f;
			if (State.Stagger != Expected || State.PartBreak != Expected || State.bStaggerComplete != (Expected == 100.f)
				|| State.bPartBroken != (Expected == 100.f)) return false;
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGProgressionContributionHUD")))
		{
			if (!ObserveContributionHUD(PC)) return false;
			if (Round == 6)
			{
				if (ContributionHUDStep != 4) return Fail(TEXT("ContributionHUDSwitchObservationMissing"));
				if (!FFileHelper::SaveStringToFile(TEXT("{\"autoViewport\":true,\"observedRounds\":7,\"targetSwitch\":true,\"unlockCleared\":true,\"returnedToTarget\":true,\"units\":[0,20,40,60,80,100,100],\"completion\":true,\"nativeTickOnly\":true}"),
					*FPaths::Combine(Gates, TEXT("ContributionHUD.proof.json")))) return Fail(TEXT("ContributionHUDProofWrite"));
				UE_LOG(LogTemp, Display, TEXT("PROGRESSION_CONTRIBUTION_HUD PASS AutoViewport=1 Rounds=7 Switch=1 Unlock=1 Completion=1"));
			}
		}
		if (Round == 0)
		{
			if (static_cast<const UGameplayAbility&>(Skill).CheckCooldown(Handle, ASC.AbilityActorInfo.Get())) return Fail(TEXT("CombatCancelCooldownLost"));
			Slots.BeginUseSkillSlot(0);
			Slots.EndUseSkillSlot(0);
			if (Spec->IsActive() || Skill.GetSkillActivationSerial() != Serial + 1) return Fail(TEXT("CombatCooldownReinputAccepted"));
		}
		UE_LOG(LogTemp, Display, TEXT("PROGRESSION_COMBAT_E2E OBSERVED Round=%d OwnerHealth=1 ExcludedHealth=1 ReceivedCooldown=1 ManaUnchanged=1 Gauge=%g"), Round, Gauge);
		if (!Write(Gate(Gates, Round, TEXT("observed")))) return Fail(TEXT("CombatOwnerObservationWrite"));
		if (Round == 6)
		{
			UE_LOG(LogTemp, Display, TEXT("PROGRESSION_COMBAT_E2E PASS Role=Client Choice=%s Hits=6 Cancel=1 IdentityCost=100"), *Choice);
			bPassed = true;
			return true;
		}
		++Round;
		Phase = 0;
	}
	return false;
}
