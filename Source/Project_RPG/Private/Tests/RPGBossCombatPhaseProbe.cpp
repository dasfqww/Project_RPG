#include "Tests/RPGBossMapE2EProbeComponent.h"

#include "Ability/RPGBossTelegraphedStrikeAbility.h"
#include "Ability/RPGBossPersistentHazardAbility.h"
#include "AI/RPGBossPersistentHazard.h"
#include "AbilitySystemComponent.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossDefinition.h"
#include "AI/RPGBossThreatComponent.h"
#include "AI/RPGBossTelegraphPresentationComponent.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBossCharacter.h"
#include "Character/RPGPlayer.h"
#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameMode/RPGBossBattleGameMode.h"
#include "GameState/RPGBossBattleGameState.h"
#include "GameplayEffect.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	FGameplayTag StrikeTag() { return FGameplayTag::RequestGameplayTag(TEXT("Boss.Action.SwordMan.Strike")); }
	FGameplayTag HeavyTag() { return FGameplayTag::RequestGameplayTag(TEXT("Boss.Action.SwordMan.HeavyStrike")); }
	FGameplayTag PhaseTwoTag() { return FGameplayTag::RequestGameplayTag(TEXT("Boss.Phase.Two")); }
	float Health(const ARPGPlayer* Player)
	{
		return Player->GetAbilitySystemComponent()->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	}
}

void URPGBossMapE2EProbeComponent::ObserveVariantHazard()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESwordManVariant")) || !OriginalBoss.IsValid()) { return; }
	ARPGBossCharacter* Boss = OriginalBoss.Get();
	const FGameplayTag Action = FGameplayTag::RequestGameplayTag(TEXT("Boss.Action.SwordMan.GroundHazard"));
	const FRPGBossTelegraphState Warning = Boss->BossAIComponent->GetTelegraphState();
	if (VariantHazardSequence == 0 && Warning.bActive && Warning.ActionTag == Action)
	{
		if (!Boss->HasAuthority() && (!Boss->BossTelegraphPresentation->IsIndicatorVisible() ||
			!FMath::IsNearlyEqual(Boss->BossTelegraphPresentation->GetIndicator()->DecalSize.Y, 250.f))) { return; }
		VariantHazardSequence = Warning.Sequence;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_VARIANT_HAZARD_WINDUP Authority=%d Sequence=%d"), Boss->HasAuthority(), Warning.Sequence);
	}
	if (!VariantHazardSequence) { return; }
	for (TActorIterator<ARPGBossPersistentHazard> It(GetWorld()); It; ++It)
	{
		const FRPGBossHazardState State = It->GetHazardState();
		if (It->IsActorBeingDestroyed() || State.SourcePawn != Boss || State.ActionTag != Action ||
			State.CastSequence != VariantHazardSequence || State.PulseCount < 1) { continue; }
		if (State.PhaseIndex != 1 || State.EncounterId != Boss->BossAIComponent->GetBossState().EncounterId ||
			!FMath::IsNearlyEqual(State.Radius, 250.f) || !FMath::IsNearlyEqual(State.EndServerTime - State.StartServerTime, 5.0) ||
			!FMath::IsNearlyEqual(State.PulseInterval, 1.f)) { Fail(TEXT("VariantHazardStateMismatch")); return; }
		if (!Boss->HasAuthority() && !It->IsIndicatorVisible()) { continue; }
		if (Boss->HasAuthority())
		{
			for (const FGameplayAbilitySpec& Spec : Boss->GetAbilitySystemComponent()->GetActivatableAbilities())
			{
				if (Cast<URPGBossPersistentHazardAbility>(Spec.Ability) && Spec.IsActive()) { Fail(TEXT("VariantHazardCastDidNotRelease")); return; }
			}
		}
		VariantHazard = *It;
		if (!bSawVariantHazardActor)
		{
			bSawVariantHazardActor = true;
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_VARIANT_HAZARD_ACTOR Authority=%d Sequence=%d Phase=1 Radius=250 Lifetime=5 Pulse=1"), Boss->HasAuthority(), State.CastSequence);
		}
		if (!Boss->HasAuthority()) { ServerConfirmCombatPhase(3, State.CastSequence); }
	}
}

void URPGBossMapE2EProbeComponent::CleanupCombatPhaseProbe()
{
	for (const auto& Binding : PhaseEffectBindings)
	{
		if (Binding.Key.IsValid()) { Binding.Key->OnGameplayEffectAppliedDelegateToSelf.Remove(Binding.Value); }
	}
	PhaseEffectBindings.Reset();
}

bool URPGBossMapE2EProbeComponent::PrepareBossAreaParticipants()
{
	ARPGBossCharacter* Boss = OriginalBoss.Get();
	APawn* Aim = Boss ? Boss->BossThreatComponent->GetCurrentTarget() : nullptr;
	if (!IsValid(Aim)) { return false; }
	if (!bAreaParticipantsPositioned)
	{
		if (ReadyClients.Num() != 2) { return false; }
		const FVector Center = Aim->GetActorLocation();
		FVector Side = FVector::CrossProduct((Center - Boss->GetActorLocation()).GetSafeNormal2D(), FVector::UpVector).GetSafeNormal();
		if (Side.IsNearlyZero()) { Side = Boss->GetActorRightVector(); }
		bool bAimIsRemote = false;
		for (const auto& Client : ReadyClients) { bAimIsRemote |= Client.IsValid() && Client->GetPawn() == Aim; }
		int32 Index = 0;
		for (const auto& Client : ReadyClients)
		{
			ARPGPlayer* Player = Client.IsValid() ? Cast<ARPGPlayer>(Client->GetPawn()) : nullptr;
			if (!IsValid(Player) || Client->IsLocalController() || !FMath::IsFinite(Health(Player)) || Health(Player) <= 0.f)
			{
				Fail(TEXT("AreaRemoteParticipantInvalid")); return false;
			}
			const float Offset = bAimIsRemote ? (Player == Aim ? 0.f : 160.f) : (Index == 0 ? -120.f : 120.f);
			const FVector Destination = Center + Side * Offset;
			// Explicit opt-in server placement fixture only. No damage/ability/threat injection.
			if (!Player->GetActorLocation().Equals(Destination, 1.f) && !Player->TeleportTo(Destination, Player->GetActorRotation()))
			{
				Fail(TEXT("AreaParticipantPlacementBlocked")); return false;
			}
			AreaParticipants.Add(Player);
			++Index;
		}
		bAreaParticipantsPositioned = true;
	}
	// Let production perception admit both players before switching the phase.
	for (const auto& Participant : AreaParticipants)
	{
		if (!Participant.IsValid() || Boss->BossThreatComponent->GetThreatForTarget(Participant.Get()) <= 0.0) { return false; }
	}
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_AREA_PARTICIPANTS_READY RemotePlayers=2 ServerPlacement=1 NaturalThreat=1"));
	return AreaParticipants.Num() == 2;
}

void URPGBossMapE2EProbeComponent::OnCombatPhaseEffectApplied(UAbilitySystemComponent* SourceASC,
	const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle, UAbilitySystemComponent* TargetASC)
{
	if (!bEnabled || bFinished || !bCombatPhaseScenario || CombatPhaseStep < 2 || !OriginalBoss.IsValid()) { return; }
	const ARPGBossPersistentHazard* Hazard = Cast<ARPGBossPersistentHazard>(Spec.GetContext().GetSourceObject());
	if (Hazard && FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESwordManVariant")))
	{
		const FRPGBossHazardState State = Hazard->GetHazardState();
		const URPGBossPersistentHazardAbility* Ability = Cast<URPGBossPersistentHazardAbility>(Spec.GetContext().GetAbility());
		const float* Before = TargetASC ? PhaseCombatHealth.Find(Cast<APawn>(TargetASC->GetAvatarActor())) : nullptr;
		const float After = TargetASC ? TargetASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) : 0.f;
		if (!Ability || Ability->GetClass()->GetPathName() != TEXT("/Game/Blueprints/Character/NPC/Raid/SwordManVariant/GameplayAbility/GA_RaidSwordManVariant_GroundHazard.GA_RaidSwordManVariant_GroundHazard_C") ||
			SourceASC != OriginalBoss->GetAbilitySystemComponent() || State.SourcePawn != OriginalBoss.Get() ||
			Spec.GetContext().GetInstigatorAbilitySystemComponent() != SourceASC || State.CastSequence != VariantHazardSequence ||
			State.PhaseIndex != 1 || !Before || !FMath::IsFinite(After) || After >= *Before)
		{
			Fail(TEXT("VariantHazardDamageContextMismatch")); return;
		}
		if (!bSawVariantHazardDamage)
		{
			bSawVariantHazardDamage = true;
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_VARIANT_HAZARD_DAMAGE Sequence=%d Before=%g After=%g AuthoredGAS=1"), State.CastSequence, *Before, After);
		}
		return;
	}
	const URPGBossTelegraphedStrikeAbility* Attack = Cast<URPGBossTelegraphedStrikeAbility>(Spec.GetContext().GetAbility());
	if (!Attack || SourceASC != OriginalBoss->GetAbilitySystemComponent() ||
		Spec.GetContext().GetInstigatorAbilitySystemComponent() != SourceASC || !TargetASC) { return; }
	if (Attack->ActionTag == StrikeTag()) { Fail(TEXT("CancelledPhaseStrikeAppliedGE")); return; }
	if (Attack->ActionTag != HeavyTag()) { return; }
	APawn* Victim = Cast<APawn>(TargetASC->GetAvatarActor());
	const float* Before = PhaseCombatHealth.Find(Victim);
	const float After = TargetASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	if (!Before || !FMath::IsFinite(After) || After >= *Before ||
		OriginalBoss->BossAIComponent->GetBossState().PhaseIndex != 1)
	{
		Fail(TEXT("HeavyDamageEffectDidNotLowerParticipantHealth")); return;
	}
	if (bBossAreaStrikeScenario)
	{
		const URPGBossTelegraphedStrikeAbility* Execution = Cast<URPGBossTelegraphedStrikeAbility>(Spec.GetContext().GetAbilityInstance_NotReplicated());
		if (!Attack->bHitAllThreatParticipants || !Execution || AreaStrikeExecutionSerial == 0 ||
			Execution->GetBossExecutionSerial() != AreaStrikeExecutionSerial)
		{
			Fail(TEXT("AreaDamageFromDifferentExecution")); return;
		}
		if (AreaParticipants.Contains(Victim))
		{
			if (AreaDamagedParticipants.Contains(Victim)) { Fail(TEXT("AreaRemoteParticipantHitTwice")); return; }
			if (AreaStrikeDamageFrame != MAX_uint64 && AreaStrikeDamageFrame != GFrameCounter)
			{
				Fail(TEXT("AreaRemoteHitsNotInSameFrame")); return;
			}
			AreaStrikeDamageFrame = GFrameCounter;
			AreaDamagedParticipants.Add(Victim);
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_AREA_SERVER_HIT Victim=%s Before=%g After=%g Sequence=%d Serial=%llu Frame=%llu"),
				*GetNameSafe(Victim), *Before, After, HeavySequence, AreaStrikeExecutionSerial, GFrameCounter);
		}
	}
	bSawHeavyGASDamage = true;
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_PHASE_SERVER_HEAVY_DAMAGE Victim=%s Before=%g After=%g AuthoredAbilityContext=1"), *GetNameSafe(Victim), *Before, After);
}

void URPGBossMapE2EProbeComponent::TickCombatPhaseServer()
{
	ARPGBossCharacter* Boss = OriginalBoss.Get();
	if (!Boss || !bSawCombatDamage || CombatClients.Num() != 2) { return; }
	UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent();
	const FRPGBossTelegraphState Telegraph = Boss->BossAIComponent->GetTelegraphState();
	if (CombatPhaseStep == 2) { ObserveVariantHazard(); }
	if (CombatPhaseStep >= 3 && FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESwordManVariant")))
	{
		if (CombatPhaseStep == 3)
		{
			// Cancel a real subsequent BT windup, not an idle boss or a manually activated ability.
			if (!Telegraph.bActive) { return; }
			if (!VariantHazard.IsValid() || VariantHazard->IsActorBeingDestroyed()) { Fail(TEXT("VariantResetHazardNotActive")); return; }
			PhaseCombatHealth.Reset();
			for (const auto& Entry : CombatHealth)
			{
				ARPGPlayer* Player = Cast<ARPGPlayer>(Entry.Key.Get());
				if (!IsValid(Player)) { Fail(TEXT("VariantResetParticipantLost")); return; }
				PhaseCombatHealth.Add(Player, Health(Player));
			}
			CancelledStrikeEnd = FMath::Max(GetWorld()->GetTimeSeconds() + 3.0, Telegraph.EndServerTime + .5);
			if (!Boss->BossAIComponent->MarkWiped() || !Boss->BossAIComponent->BeginReset()) { Fail(TEXT("VariantResetRejected")); return; }
			CombatBossStart = Boss->GetActorLocation();
			CombatPhaseStep = 4;
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_VARIANT_RESET_STARTED RealWindup=1 Wiped=1 Resetting=1"));
		}
		if (VariantHazard.IsValid() && !VariantHazard->IsActorBeingDestroyed()) { Fail(TEXT("VariantResetLeftHazardAlive")); return; }
		const AAIController* AI = Cast<AAIController>(Boss->GetController());
		if (Boss->BossAIComponent->GetBossState().EncounterState != ERPGBossEncounterState::Resetting ||
			Boss->BossAIComponent->GetTelegraphState().bActive || Boss->BossThreatComponent->GetCurrentTarget() ||
			!AI || (AI->GetBrainComponent() && AI->GetBrainComponent()->IsRunning()) ||
			Boss->GetCharacterMovement()->MovementMode != MOVE_None || !Boss->GetVelocity().IsNearlyZero() ||
			!Boss->GetActorLocation().Equals(CombatBossStart, 1.f)) { Fail(TEXT("VariantResetDidNotStop")); return; }
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (Spec.IsActive() && Cast<URPGBossGameplayAbility>(Spec.GetPrimaryInstance())) { Fail(TEXT("VariantResetLeftPatternActive")); return; }
		}
		for (const auto& Entry : PhaseCombatHealth)
		{
			if (!Entry.Key.IsValid() || !FMath::IsNearlyEqual(Health(CastChecked<ARPGPlayer>(Entry.Key.Get())), Entry.Value))
			{
				Fail(TEXT("VariantResetStaleDamage")); return;
			}
		}
		if (GetWorld()->GetTimeSeconds() < CancelledStrikeEnd) { return; }
		const int32 OldEncounter = Boss->BossAIComponent->GetBossState().EncounterId;
		ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), ASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute()));
		if (!Boss->BossAIComponent->CompleteReset() || !Boss->BossAIComponent->StartEncounter() ||
			Boss->BossAIComponent->GetBossState().EncounterId <= OldEncounter || Boss->BossAIComponent->GetBossState().PhaseIndex != 0)
		{
			Fail(TEXT("VariantResetRestartFailed")); return;
		}
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_VARIANT_RESET_COMPLETE Cancelled=1 BTStopped=1 Stationary=1 NoStaleDamage=1 RestartedPhase0=1"));
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_VARIANT_HAZARD_COMPLETE NaturalBT=1 GASDamage=1 IrisClients=2 ResetRemoved=1"));
		bFinished = true;
		SetComponentTickEnabled(false);
		return;
	}
	if (CombatPhaseStep == 0)
	{
		if (bBossAreaStrikeScenario && !PrepareBossAreaParticipants()) { return; }
		CombatPhaseStep = 1;
		Deadline = FPlatformTime::Seconds() + 45.0;
		for (const auto& Entry : CombatHealth)
		{
			ARPGPlayer* Player = Cast<ARPGPlayer>(Entry.Key.Get());
			if (!Player) { Fail(TEXT("PhaseParticipantLost")); return; }
			UAbilitySystemComponent* ParticipantASC = Player->GetAbilitySystemComponent();
			// GAS supplies the effect SOURCE as the first argument, not this target ASC.
			PhaseEffectBindings.Emplace(ParticipantASC, ParticipantASC->OnGameplayEffectAppliedDelegateToSelf.AddUObject(this, &ThisClass::OnCombatPhaseEffectApplied, ParticipantASC));
		}
	}
	if (CombatPhaseStep == 1 && Telegraph.bActive && Telegraph.ActionTag == StrikeTag() && Telegraph.Sequence > FirstCombatSequence)
	{
		if (TransitionSequence != Telegraph.Sequence)
		{
			TransitionSequence = Telegraph.Sequence;
			PhaseWindupClients.Reset();
		}
		if (PhaseWindupClients.Num() != 2 || Telegraph.EndServerTime - GetWorld()->GetTimeSeconds() < .35) { return; }
		const URPGBossDefinition* Definition = Boss->BossAIComponent->Definition;
		if (!Definition || Definition->Phases.Num() != 2 || Definition->Phases[1].PhaseTag != PhaseTwoTag() ||
			!FMath::IsNearlyEqual(Definition->Phases[1].HealthThreshold, .7f))
		{
			Fail(TEXT("UnsupportedPhaseFixtureDefinition")); return;
		}
		const float Before = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
		const float Max = ASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute());
		const float TargetHP = FMath::FloorToFloat(Max * (Definition->Phases[1].HealthThreshold - .01f));
		if (!FMath::IsFinite(Before) || !FMath::IsFinite(TargetHP) || TargetHP <= 0.f || TargetHP >= Before || Boss->BossAIComponent->GetBossState().PhaseIndex != 0)
		{
			Fail(TEXT("InvalidPhaseThresholdDamageFixture")); return;
		}
		FGameplayAbilitySpecHandle StrikeHandle;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			const URPGBossTelegraphedStrikeAbility* Attack = Cast<URPGBossTelegraphedStrikeAbility>(Spec.Ability);
			if (Attack && Attack->ActionTag == StrikeTag() && Spec.IsActive()) { StrikeHandle = Spec.Handle; }
		}
		if (!StrikeHandle.IsValid()) { Fail(TEXT("PhaseTransitionStrikeNotActive")); return; }
		for (const auto& Entry : CombatHealth) { PhaseCombatHealth.Add(Entry.Key, Health(CastChecked<ARPGPlayer>(Entry.Key.Get()))); }
		CancelledStrikeEnd = Telegraph.EndServerTime + .15;
		const UGameplayAbility* PreviousAnimatingAbility = ASC->GetAnimatingAbility();
		CombatPhaseStep = 2;
		// Explicit server-only threshold stimulus, not a player input/attack test.
		// Production AttributeSet damage callback drives phase/cancellation; never
		// assign PhaseIndex, set HP directly, or manually activate a boss ability.
		UGameplayEffect* Damage = NewObject<UGameplayEffect>(this);
		Damage->DurationPolicy = EGameplayEffectDurationType::Instant;
		FGameplayModifierInfo& Modifier = Damage->Modifiers.AddDefaulted_GetRef();
		Modifier.Attribute = URPGAttributeSet::GetTakeDamageAttribute();
		Modifier.ModifierOp = EGameplayModOp::Override;
		Modifier.ModifierMagnitude = FScalableFloat(FMath::CeilToFloat(Before - TargetHP));
		ASC->ApplyGameplayEffectToSelf(Damage, 1.f, ASC->MakeEffectContext());
		const FGameplayAbilitySpec* Strike = ASC->FindAbilitySpecFromHandle(StrikeHandle);
		if (Boss->BossAIComponent->GetBossState().PhaseIndex != 1 || !Strike || Strike->IsActive() || Boss->BossAIComponent->GetTelegraphState().bActive)
		{
			Fail(TEXT("PhaseChangeDidNotCancelStrikeAndTelegraph")); return;
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAttackPresentation")))
		{
			if (!PreviousAnimatingAbility || ASC->GetAnimatingAbility() == PreviousAnimatingAbility)
			{
				Fail(TEXT("PhaseChangeDidNotReleaseStrikeMontageOwnership")); return;
			}
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_BOSS_ATTACK_CANCEL Phase=1 OldMontageOwnershipCleared=1"));
		}
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_PHASE_SERVER_TRANSITION OldSequence=%d BossBefore=%g BossAfter=%g Phase=1 StrikeCancelled=1 TelegraphCleared=1 InjectedBossDamage=1"),
			TransitionSequence, Before, ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()));
	}
	if (CombatPhaseStep >= 2)
	{
		if (GetWorld()->GetTimeSeconds() <= CancelledStrikeEnd)
		{
			for (const auto& Entry : PhaseCombatHealth)
			{
				if (!Entry.Key.IsValid() || !FMath::IsNearlyEqual(Health(CastChecked<ARPGPlayer>(Entry.Key.Get())), Entry.Value))
				{
					Fail(TEXT("DamageBeforeCancelledStrikeWindowExpired")); return;
				}
			}
		}
		else if (!bOldStrikeWindowPassed)
		{
			bOldStrikeWindowPassed = true;
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_PHASE_SERVER_STALE_WINDOW Clear=1 ParticipantHPUnchanged=1"));
		}
		const FRPGBossTelegraphState Current = Boss->BossAIComponent->GetTelegraphState();
		if (Current.bActive && Current.ActionTag == HeavyTag() && Current.Sequence > TransitionSequence && HeavySequence == 0)
		{
			HeavySequence = Current.Sequence;
			if (bBossAreaStrikeScenario)
			{
				for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
				{
					const URPGBossTelegraphedStrikeAbility* Attack = Cast<URPGBossTelegraphedStrikeAbility>(Spec.GetPrimaryInstance());
					if (Spec.IsActive() && Attack && Attack->ActionTag == HeavyTag()) { AreaStrikeExecutionSerial = Attack->GetBossExecutionSerial(); }
				}
				if (AreaStrikeExecutionSerial == 0) { Fail(TEXT("AreaHeavyExecutionNotFound")); return; }
				for (const auto& Participant : AreaParticipants)
				{
					if (!Participant.IsValid() || FVector::Dist(Participant->GetActorLocation(), Current.AimLocation) > 300.f)
					{
						Fail(TEXT("AreaRemoteParticipantOutsideWarning")); return;
					}
				}
			}
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_PHASE_SERVER_HEAVY Sequence=%d NaturalBT=1"), HeavySequence);
		}
		const bool bVariantReady = !FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESwordManVariant")) ||
			(bSawVariantHazardActor && bSawVariantHazardDamage && VariantHazardClients.Num() == 2);
		if (bVariantReady && bOldStrikeWindowPassed && bSawHeavyGASDamage && HeavyReadyClients.Num() == 2 && HeavyDamageClients.Num() == 2 &&
			(!bBossAreaStrikeScenario || AreaDamagedParticipants.Num() == 2))
		{
			if (bBossAreaStrikeScenario)
			{
				UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_AREA_SERVER_COMPLETE RemoteVictims=2 SameExecution=1 SameFrame=1 OnceEach=1 OwnedHPClients=2"));
			}
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_PHASE_SERVER_COMPLETE PhaseTransition=1 OldAttackCancelled=1 HeavyGASDamage=1 Clients=2"));
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_NATURAL_SERVER_COMPLETE Navigation=1 BTMove=1 GASDamage=1 Clients=2"));
			if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESwordManVariant")))
			{
				CleanupCombatPhaseProbe();
				CombatPhaseStep = 3;
				return;
			}
			bFinished = true;
			CleanupCombatPhaseProbe();
			SetComponentTickEnabled(false);
		}
	}
}

void URPGBossMapE2EProbeComponent::TickCombatPhaseClient()
{
	if (!bSawCombatDamage || ClientPhase != 3 || !OriginalBoss.IsValid()) { return; }
	ARPGBossCharacter* Boss = OriginalBoss.Get();
	ObserveVariantHazard();
	const FRPGBossState State = Boss->BossAIComponent->GetBossState();
	const FRPGBossTelegraphState Telegraph = Boss->BossAIComponent->GetTelegraphState();
	if (State.PhaseIndex == 0 && Telegraph.bActive && Telegraph.ActionTag == StrikeTag() && Telegraph.Sequence > FirstCombatSequence && Boss->BossTelegraphPresentation->IsIndicatorVisible() && ObserveBossAttackPresentation(StrikeTag()))
	{
		if (TransitionSequence != Telegraph.Sequence)
		{
			TransitionSequence = Telegraph.Sequence;
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_PHASE_CLIENT_WINDUP Sequence=%d Indicator=1"), TransitionSequence);
		}
		ServerConfirmCombatPhase(0, Telegraph.Sequence);
	}
	const UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent();
	const float HP = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	const float Max = ASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute());
	if (ClientCombatPhaseStep == 0 && TransitionSequence > 0 && State.PhaseIndex == 1 && State.PhaseTag == PhaseTwoTag() &&
		FMath::IsFinite(HP) && FMath::IsFinite(Max) && HP > 0.f && Max > 0.f && HP <= Max * .7f &&
		Telegraph.bActive && Telegraph.ActionTag == HeavyTag() && Telegraph.Sequence > TransitionSequence && Boss->BossTelegraphPresentation->IsIndicatorVisible() && ObserveBossAttackPresentation(HeavyTag()))
	{
		const UDecalComponent* Indicator = Boss->BossTelegraphPresentation->GetIndicator();
		if (!Indicator || !FMath::IsNearlyEqual(Indicator->DecalSize.Y, 300.f)) { Fail(TEXT("HeavyIndicatorRadiusMismatch")); return; }
		HeavySequence = Telegraph.Sequence;
		for (TActorIterator<ARPGPlayer> It(GetWorld()); It; ++It)
		{
			UAbilitySystemComponent* Participant = It->GetAbilitySystemComponent();
			if (Participant && Participant->GetAvatarActor() == *It) { PhaseCombatHealth.Add(*It, Health(*It)); }
		}
		ClientCombatPhaseStep = 1;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_PHASE_CLIENT_HEAVY Phase=1 BossHealth=%g Sequence=%d Radius=300 OldSequenceReplaced=1"), HP, HeavySequence);
	}
	if (ClientCombatPhaseStep >= 1) { ServerConfirmCombatPhase(1, HeavySequence); }
	if (ClientCombatPhaseStep == 1)
	{
		const APlayerController* OwnerPC = Cast<APlayerController>(GetOwner());
		const APawn* OwnedPawn = OwnerPC ? OwnerPC->GetPawn() : nullptr;
		for (const auto& Entry : PhaseCombatHealth)
		{
			if (bBossAreaStrikeScenario && Entry.Key.Get() != OwnedPawn) { continue; }
			if (ARPGPlayer* Player = Cast<ARPGPlayer>(Entry.Key.Get()); Player && Health(Player) < Entry.Value)
			{
				if (bBossAreaStrikeScenario)
				{
					UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_AREA_CLIENT_DAMAGE OwnHPDecreased=1 Sequence=%d Before=%g After=%g"), HeavySequence, Entry.Value, Health(Player));
				}
				ClientCombatPhaseStep = 2;
				UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_PHASE_CLIENT_COMPLETE Phase=1 HeavyTelegraph=1 HeavyDamageReplicated=1"));
				break;
			}
		}
	}
	if (ClientCombatPhaseStep == 2) { ServerConfirmCombatPhase(2, HeavySequence); }
}

void URPGBossMapE2EProbeComponent::ServerConfirmCombatPhase_Implementation(int32 Checkpoint, int32 Sequence)
{
	if (bEnabled && bSummonMechanicScenario) { ConfirmSummonMechanic(Checkpoint, Sequence); return; }
	if (!bEnabled || !bCombatPhaseScenario) { return; }
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	ARPGBossBattleGameMode* Mode = GetWorld()->GetAuthGameMode<ARPGBossBattleGameMode>();
	URPGBossMapE2EProbeComponent* Driver = Mode ? Mode->FindComponentByClass<URPGBossMapE2EProbeComponent>() : nullptr;
	if (!PC || PC->IsLocalController() || !Driver || Driver->bFinished || !Driver->bCombatPhaseScenario ||
		Driver->ServerStage != 2 || !Driver->ReadyClients.Contains(PC) || !Driver->OriginalBoss.IsValid()) { return; }
	const FRPGBossState State = Driver->OriginalBoss->BossAIComponent->GetBossState();
	const FRPGBossTelegraphState Telegraph = Driver->OriginalBoss->BossAIComponent->GetTelegraphState();
	if (Checkpoint == 0 && Driver->CombatPhaseStep == 1 && State.PhaseIndex == 0 && Telegraph.bActive &&
		Telegraph.ActionTag == StrikeTag() && Sequence == Telegraph.Sequence && Sequence == Driver->TransitionSequence)
	{
		Driver->PhaseWindupClients.Add(PC);
	}
	else if (Checkpoint == 1 && Driver->CombatPhaseStep >= 2 && State.PhaseIndex == 1 &&
		Telegraph.ActionTag == HeavyTag() && Sequence == Driver->HeavySequence && Sequence > Driver->TransitionSequence)
	{
		Driver->HeavyReadyClients.Add(PC);
	}
	else if (Checkpoint == 2 && Driver->bSawHeavyGASDamage && State.PhaseIndex == 1 &&
		(!Driver->bBossAreaStrikeScenario || Driver->AreaDamagedParticipants.Contains(PC->GetPawn())) &&
		Driver->HeavyReadyClients.Contains(PC) && Sequence == Driver->HeavySequence)
	{
		Driver->HeavyDamageClients.Add(PC);
	}
	else if (Checkpoint == 3 && FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESwordManVariant")) &&
		State.PhaseIndex == 1 && Driver->bSawVariantHazardActor && Sequence == Driver->VariantHazardSequence)
	{
		Driver->VariantHazardClients.Add(PC);
	}
}
