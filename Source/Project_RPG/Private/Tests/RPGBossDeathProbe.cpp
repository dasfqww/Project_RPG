#include "Tests/RPGBossMapE2EProbeComponent.h"

#if !UE_BUILD_SHIPPING
#include "Tests/RPGBossDamageTestFixture.h"
#include "Tests/RPGSkillDamageSecurityTestFixture.h"
#include "Ability/RPGBossTelegraphedStrikeAbility.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "AI/RPGBossTelegraphPresentationComponent.h"
#include "AIController.h"
#include "Animation/AnimMontage.h"
#include "Attribute/RPGAttributeSet.h"
#include "BrainComponent.h"
#include "Character/RPGBossCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameMode/RPGBossBattleGameMode.h"
#include "GameState/RPGBossBattleGameState.h"
#include "HAL/PlatformTime.h"
#include "RPGGameplayTags.h"

namespace
{
bool DeathMontage(const UAbilitySystemComponent* ASC)
{
	const UAnimMontage* Montage = ASC ? ASC->GetCurrentMontage() : nullptr;
	return Montage && Montage->GetName().Contains(TEXT("Death"));
}
}
#endif

void URPGBossMapE2EProbeComponent::CleanupBossDeathProbe()
{
#if !UE_BUILD_SHIPPING
	if (BossDeathASC.IsValid())
	{
		BossDeathASC->AbilityActivatedCallbacks.Remove(BossDeathActivatedBinding);
		BossDeathASC->OnAbilityEnded.Remove(BossDeathEndedBinding);
	}
	BossDeathASC.Reset();
	if (APawn* Source = BossDeathSource.Get())
	{
		AController* Controller = Source->GetController();
		Source->Destroy();
		if (IsValid(Controller)) Controller->Destroy();
	}
	BossDeathSource.Reset();
#endif
}

void URPGBossMapE2EProbeComponent::OnBossDeathAbilityActivated(UGameplayAbility* Ability)
{
#if !UE_BUILD_SHIPPING
	if (Ability && Ability->GetCurrentAbilitySpecHandle() == BossDeathHandle)
	{
		++BossDeathActivatedCount;
		if (BossDeathActivatedCount != 1) Fail(TEXT("BossDeathAbilityActivatedMoreThanOnce"));
	}
#endif
}

void URPGBossMapE2EProbeComponent::OnBossDeathAbilityEnded(const FAbilityEndedData& Data)
{
#if !UE_BUILD_SHIPPING
	if (Data.AbilitySpecHandle != BossDeathHandle) return;
	++BossDeathEndedCount;
	if (Data.bWasCancelled || BossDeathEndedCount != 1) Fail(TEXT("BossDeathAbilityDidNotEndNaturallyOnce"));
#endif
}

void URPGBossMapE2EProbeComponent::TickBossDeathServer()
{
#if !UE_BUILD_SHIPPING
	ARPGBossCharacter* Boss = OriginalBoss.Get();
	if (!Boss || !bSawCombatDamage || CombatClients.Num() != 2) return;
	UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent();
	if (BossDeathStep == 0)
	{
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
			if (Spec.Ability && Spec.Ability->GetAssetTags().HasTagExact(RPGGameplayTags::Shared_Status_Death))
			{
				if (BossDeathHandle.IsValid()) { Fail(TEXT("AmbiguousBossDeathAbility")); return; }
				BossDeathHandle = Spec.Handle;
			}
		if (!BossDeathHandle.IsValid()) { Fail(TEXT("MissingAuthoredBossDeathAbility")); return; }
		BossDeathASC = ASC;
		BossDeathActivatedBinding = ASC->AbilityActivatedCallbacks.AddUObject(this, &ThisClass::OnBossDeathAbilityActivated);
		BossDeathEndedBinding = ASC->OnAbilityEnded.AddUObject(this, &ThisClass::OnBossDeathAbilityEnded);
		Deadline = FPlatformTime::Seconds() + 45.0;
		BossDeathStep = 1;
	}
	if (BossDeathStep == 1)
	{
		const FRPGBossTelegraphState Telegraph = Boss->BossAIComponent->GetTelegraphState();
		if (!Telegraph.bActive || Telegraph.Sequence <= FirstCombatSequence) return;
		if (BossDeathSequence != Telegraph.Sequence)
		{
			BossDeathSequence = Telegraph.Sequence;
			BossDeathWindupClients.Reset();
		}
		if (BossDeathWindupClients.Num() != 2 || Telegraph.EndServerTime - GetWorld()->GetTimeSeconds() < .35) return;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
			if (Spec.IsActive() && Cast<URPGBossTelegraphedStrikeAbility>(Spec.Ability)) BossDeathStrikeHandle = Spec.Handle;
		if (!BossDeathStrikeHandle.IsValid()) { Fail(TEXT("BossDeathRequiresActiveNaturalStrike")); return; }
		for (const auto& Entry : CombatHealth)
		{
			UAbilitySystemComponent* Participant = Entry.Key.IsValid() ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Entry.Key.Get()) : nullptr;
			if (!Participant) { Fail(TEXT("BossDeathParticipantLost")); return; }
			BossDeathParticipantHealth.Add(Entry.Key, Participant->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()));
		}
		const float Before = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
		const float Defense = ASC->GetNumericAttribute(URPGAttributeSet::GetDefenseAttribute());
		const float Base = Before * Defense + 10.f;
		if (!FMath::IsFinite(Base) || Before <= 0.f || Defense <= 0.f || Base > 1.e6f) { Fail(TEXT("InvalidBossDeathDamageFixture")); return; }
		auto* Source = GetWorld()->SpawnActor<ARPGSkillDamageSecurityTestPawn>();
		auto* Controller = GetWorld()->SpawnActor<AAIController>();
		if (!Source || !Controller) { if (Source) Source->Destroy(); if (Controller) Controller->Destroy(); Fail(TEXT("BossDeathSourceSpawnFailed")); return; }
		BossDeathSource = Source;
		Source->SetActorLocation(Boss->GetActorLocation() - FVector(100.f, 0.f, 0.f));
		Controller->SetGenericTeamId(FGenericTeamId(0));
		Controller->Possess(Source);
		const FHitResult Hit(Boss, nullptr, Boss->GetActorLocation(), FVector::UpVector);
		FGameplayEffectSpecHandle Spec = MakeBossTestDamageSpec(Source->ASC(), Base);
		FGameplayEffectContextHandle Context = Spec.Data->GetContext();
		Context.AddHitResult(Hit);
		Spec.Data->SetContext(Context);
		FRPGSkillSecurityProfile Profile;
		Profile.MaximumServerHitDistance = 1000.f;
		Profile.MaximumDamagePerHit = 1.e6f;
		Profile.MaximumFinalDamagePerHit = 1.e6f;
		BossDeathOldStrikeEnd = Telegraph.EndServerTime + .25;
		BossDeathStep = 2;
		float AppliedDamage = 0.f;
		bool bThreatAdded = false;
		// Server-only native attack stimulus. No player input, HP assignment,
		// manual boss/death activation, or backend settlement is asserted.
		if (!Boss->BossThreatComponent->ApplyDamageFromServerHit(Source, Hit, Spec, Telegraph.EncounterId,
			Profile, AppliedDamage, bThreatAdded) || !FMath::IsNearlyEqual(AppliedDamage, Before) || bThreatAdded)
		{ Fail(TEXT("BossDeathLethalServerHitRejected")); return; }
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_BOSS_DEATH_SERVER_HIT Before=%g After=0 ReceiptBacked=1 NativeAttackFixture=1 PlayerInput=0 Sequence=%d"), Before, BossDeathSequence);
		return; // Observe after GAS/BT callback scopes have unwound, on the next real tick.
	}
	if (BossDeathStep != 2 || bFinished) return;
	const FGameplayAbilitySpec* Strike = ASC->FindAbilitySpecFromHandle(BossDeathStrikeHandle);
	AAIController* AI = Cast<AAIController>(Boss->GetController());
	const auto Snapshot = GetWorld()->GetGameState<ARPGBossBattleGameState>()->GetBattleSnapshot();
	if (Boss->BossAIComponent->GetBossState().EncounterState != ERPGBossEncounterState::Dead ||
		ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) != 0.f ||
		ASC->GetTagCount(RPGGameplayTags::Shared_Status_Death) != 1 || !Strike || Strike->IsActive() ||
		Boss->BossAIComponent->GetTelegraphState().bActive || Boss->BossThreatComponent->GetCurrentTarget() ||
		!AI || !AI->GetBrainComponent() || AI->GetBrainComponent()->IsRunning() ||
		Boss->GetCharacterMovement()->MovementMode != MOVE_None || !Boss->GetVelocity().IsNearlyZero() ||
		Snapshot.BattleState != EBossBattleState::Victory || GetWorld()->GetGameState<ARPGBossBattleGameState>()->GetRemainingBattleTime() != 0.f)
	{ Fail(TEXT("BossDeathDidNotClearCombatAndMovement")); return; }
	for (const auto& Entry : BossDeathParticipantHealth)
	{
		const UAbilitySystemComponent* Participant = Entry.Key.IsValid() ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Entry.Key.Get()) : nullptr;
		if (!Participant || Participant->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) != Entry.Value)
		{ Fail(TEXT("DeadBossAppliedLateParticipantDamage")); return; }
		if (Boss->BossThreatComponent->GetThreatForTarget(Entry.Key.Get()) != 0.0) { Fail(TEXT("DeadBossRetainedThreat")); return; }
	}
	bBossDeathServerMontage |= DeathMontage(ASC);
	if (BossDeathActivatedCount != 1) { Fail(TEXT("BossDeathDidNotActivateAuthoredAbility")); return; }
	if (BossDeathEndedCount != 1) return;
	if (!bBossDeathServerMontage || Boss->GetCapsuleComponent()->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
	{ Fail(TEXT("BossDeathMissingAuthoredPresentation")); return; }
	if (BossDeathQuietStart == 0.0) BossDeathQuietStart = GetWorld()->GetTimeSeconds();
	if (GetWorld()->GetTimeSeconds() < BossDeathOldStrikeEnd || GetWorld()->GetTimeSeconds() - BossDeathQuietStart < .5 || BossDeathClients.Num() != 2) return;
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_BOSS_DEATH_SERVER_COMPLETE DeathAbility=1 NaturalEnd=1 BTStopped=1 MovementStopped=1 NoStaleDamage=1 Clients=2"));
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_NATURAL_SERVER_COMPLETE Navigation=1 BTMove=1 GASDamage=1 Clients=2"));
	bFinished = true;
	CleanupBossDeathProbe();
	SetComponentTickEnabled(false);
#endif
}

void URPGBossMapE2EProbeComponent::TickBossDeathClient()
{
#if !UE_BUILD_SHIPPING
	ARPGBossCharacter* Boss = OriginalBoss.Get();
	if (!Boss || bBossDeathClientConfirmed) return;
	const auto Snapshot = GetWorld()->GetGameState<ARPGBossBattleGameState>()->GetBattleSnapshot();
	if (Snapshot.BattleState == EBossBattleState::InProgress) ServerConfirmSnapshot(3);
	const auto Telegraph = Boss->BossAIComponent->GetTelegraphState();
	if (Telegraph.bActive && Telegraph.Sequence > FirstCombatSequence && Boss->BossTelegraphPresentation->IsIndicatorVisible())
	{
		BossDeathSequence = Telegraph.Sequence;
		ServerConfirmBossDeath(1, Telegraph.Sequence);
	}
	const UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent();
	bBossDeathClientMontage |= DeathMontage(ASC) && ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death);
	if (Snapshot.BattleState != EBossBattleState::Victory || Snapshot.Boss != Boss || GetWorld()->GetGameState<ARPGBossBattleGameState>()->GetRemainingBattleTime() != 0.f ||
		Boss->BossAIComponent->GetBossState().EncounterState != ERPGBossEncounterState::Dead ||
		ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) != 0.f ||
		ASC->GetTagCount(RPGGameplayTags::Shared_Status_Death) != 1 || !bBossDeathClientMontage ||
		Telegraph.bActive || Boss->BossTelegraphPresentation->IsIndicatorVisible()) return;
	bBossDeathClientConfirmed = true;
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_BOSS_DEATH_CLIENT_COMPLETE Dead=1 HPZero=1 DeathTag=1 Montage=1 TelegraphCleared=1 Victory=1"));
	ServerConfirmBossDeath(2, BossDeathSequence);
#endif
}

void URPGBossMapE2EProbeComponent::ServerConfirmBossDeath_Implementation(int32 Checkpoint, int32 Sequence)
{
#if !UE_BUILD_SHIPPING
	if (!bEnabled || !bBossDeathScenario) return;
	auto* PC = Cast<APlayerController>(GetOwner());
	auto* Mode = GetWorld()->GetAuthGameMode<ARPGBossBattleGameMode>();
	auto* Driver = Mode ? Mode->FindComponentByClass<URPGBossMapE2EProbeComponent>() : nullptr;
	if (!PC || !Driver || !Driver->bBossDeathScenario || Driver->bFinished || !Driver->CombatClients.Contains(PC) || !Driver->OriginalBoss.IsValid()) return;
	const auto Telegraph = Driver->OriginalBoss->BossAIComponent->GetTelegraphState();
	if (Checkpoint == 1 && Driver->BossDeathStep == 1 && Telegraph.bActive && Telegraph.Sequence == Sequence && Sequence > Driver->FirstCombatSequence)
	{
		if (Driver->BossDeathSequence != Sequence) { Driver->BossDeathSequence = Sequence; Driver->BossDeathWindupClients.Reset(); }
		Driver->BossDeathWindupClients.Add(PC);
	}
	if (Checkpoint == 2 && Driver->BossDeathStep == 2 && Sequence == Driver->BossDeathSequence &&
		Driver->OriginalBoss->BossAIComponent->GetBossState().EncounterState == ERPGBossEncounterState::Dead)
		Driver->BossDeathClients.Add(PC); // Observation only; never authorizes damage or victory.
#endif
}
