#include "Tests/RPGBossMapE2EProbeComponent.h"

#include "Ability/RPGGameplayAbility.h"
#include "Ability/RPGNPCGameplayAbility.h"
#include "DataAsset/StartUpData/DataAsset_NPCStartUpData.h"
#include "Type/RPGStructTypes.h"
#include "RPGInputTags.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Attribute/RPGAttributeSet.h"
#include "BrainComponent.h"
#include "Character/RPGBaseCharacter.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Component/Combat/RPGCombatReactionComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameMode/RPGBossBattleGameMode.h"
#include "HAL/PlatformTime.h"
#include "RPGGameplayTags.h"

namespace
{
	const TCHAR* PlayerAttack = TEXT("/Game/Blueprints/Character/Player/Abilities/DefaultAttack/GA_Player_DefaultAttack.GA_Player_DefaultAttack_C");
	const TCHAR* NPCAttack = TEXT("/Game/Blueprints/Character/NPC/Gruntling/GameplayAbility/GA_Guardian_Melee1.GA_Guardian_Melee1_C");
	URPGCombatReactionComponent* Receiver(APawn* Pawn)
	{
		return IsValid(Pawn) ? Pawn->FindComponentByClass<URPGCombatReactionComponent>() : nullptr;
	}
	UAbilitySystemComponent* ASC(APawn* Pawn)
	{
		auto* Character = Cast<ARPGBaseCharacter>(Pawn);
		return Character ? Character->GetAbilitySystemComponent() : nullptr;
	}
	float Health(APawn* Pawn)
	{
		return ASC(Pawn) ? ASC(Pawn)->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) : -1.f;
	}
}

void URPGBossMapE2EProbeComponent::TickLightHitServer()
{
#if !UE_BUILD_SHIPPING
	if (!bLightHitScenario || !GetOwner()->HasAuthority()) return;
	if (bKnockbackScenario) { TickKnockbackServer(); return; }
	if (bLightHitInputScenario) { TickLightHitInputServer(); return; }
	if (LightHitStep == 0)
	{
		if (!LightHitNPC.IsValid())
		{
			APawn* Player = ReadyClients.CreateConstIterator()->Get()->GetPawn();
			UClass* Class = LoadClass<ARPGNonPlayerCharacter>(nullptr,
				TEXT("/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Gruntling_Guardian.BP_Gruntling_Guardian_C"));
			UClass* PlayerAbilityClass = LoadClass<URPGGameplayAbility>(nullptr, PlayerAttack);
			UClass* NPCAbilityClass = LoadClass<URPGGameplayAbility>(nullptr, NPCAttack);
			if (!Player || !Class || !PlayerAbilityClass || !NPCAbilityClass) { Fail(TEXT("LightHitContentMissing")); return; }
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			auto* NPC = GetWorld()->SpawnActor<ARPGNonPlayerCharacter>(Class,
				Player->GetActorLocation() + FVector(200.f, 0.f, 0.f), FRotator::ZeroRotator, Params);
			if (!NPC) { Fail(TEXT("LightHitSpawnFailed")); return; }
			LightHitPlayer = Player;
			LightHitNPC = NPC;
			FRPGPlayerAbilitySet PlayerData;
			PlayerData.InputTag = RPGGameplayTags::InputTag_LightAttack_GreatSword;
			PlayerData.AbilityToGrant = PlayerAbilityClass;
			PlayerData.HitReactionPolicy = ERPGAttackHitReactionPolicy::Cosmetic;
			auto* NPCData = NewObject<UDataAsset_NPCStartUpData>(this);
			NPCData->AttackHitReactionPolicies.Add(NPCAbilityClass, ERPGAttackHitReactionPolicy::Cosmetic);
			FGameplayAbilitySpec PlayerSpec, NPCSpec, LegacySpec;
			if (!PlayerData.BuildAbilitySpec(1, Player, PlayerSpec)
				|| !NPCData->BuildAttackAbilitySpec(NPCAbilityClass, 1, NPC, NPCSpec))
			{ Fail(TEXT("LightHitGrantDataInvalid")); return; }
			NPCData->AttackHitReactionPolicies.Reset();
			if (!NPCData->BuildAttackAbilitySpec(NPCAbilityClass, 1, NPC, LegacySpec)
				|| LegacySpec.Ability->GetClass() != NPCSpec.Ability->GetClass()
				|| RPGHitReaction::GetGrantedPolicy(&NPCSpec) != ERPGAttackHitReactionPolicy::Cosmetic
				|| RPGHitReaction::GetGrantedPolicy(&LegacySpec) != ERPGAttackHitReactionPolicy::Legacy)
			{ Fail(TEXT("LightHitPolicyNotOwnedByData")); return; }
			LightHitPlayerAbility = ASC(Player)->GiveAbility(PlayerSpec);
			LightHitNPCAbility = ASC(NPC)->GiveAbility(NPCSpec);
		}
		auto* NPC = Cast<ARPGNonPlayerCharacter>(LightHitNPC.Get());
		if (AAIController* AI = NPC ? Cast<AAIController>(NPC->GetController()) : nullptr)
			if (AI->GetBrainComponent()) AI->GetBrainComponent()->StopLogic(TEXT("Opt-in light-hit presentation fixture"));
		if (!NPC || !NPC->IsNPCStartUpDataReady()) return;
		NPC->GetCharacterMovement()->StopMovementImmediately();
		// Keep the eight presentation hits nonlethal, independently of the
		// authored combat balance. Checkpoint ten separately prepares a lethal hit.
		for (APawn* Pawn : {LightHitPlayer.Get(), LightHitNPC.Get()})
		{
			ASC(Pawn)->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 10000.f);
			ASC(Pawn)->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 10000.f);
			Pawn->ForceNetUpdate();
		}
		for (const auto& PC : ReadyClients)
			PC->FindComponentByClass<URPGBossMapE2EProbeComponent>()->ClientSetLightHitSubjects(LightHitPlayer.Get(), NPC);
		LightHitStep = 1;
		LightHitNextAt = FPlatformTime::Seconds() + 1.0;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_LIGHT_HIT_PREPARED DataDrivenGrant=1 SharedGAClasses=1 ServerDamageEventStimulus=1 DurabilityFixture=10000 NaturalInput=0"));
		return;
	}
	if (LightHitConfirmations.FindOrAdd(LightHitStep - 1).Num() != 2 || FPlatformTime::Seconds() < LightHitNextAt) return;
	if (LightHitStep == 11)
	{
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_LIGHT_HIT_COMPLETE PlayerNPC=1 NPCPlayer=1 Directions=4 Clients=2 Duplicate=1 Invalid=1 Lethal=1"));
		bFinished = true;
		SetComponentTickEnabled(false);
		return;
	}
	const int32 Checkpoint = LightHitStep;
	const bool bPlayerSource = Checkpoint <= 4;
	APawn* Source = bPlayerSource ? LightHitPlayer.Get() : LightHitNPC.Get();
	APawn* Victim = bPlayerSource ? LightHitNPC.Get() : LightHitPlayer.Get();
	auto* Component = Receiver(Victim);
	if (!Source || !Victim || !Component || !Component->PresentationProfile) { Fail(TEXT("LightHitReceiverMissing")); return; }
	const int32 Direction = (Checkpoint - 1) % 4;
	// Select all four server-relative directions without teleporting a player
	// or changing its movement security budget.
	const float SourceYaw = (Source->GetActorLocation() - Victim->GetActorLocation()).Rotation().Yaw;
	Victim->SetActorRotation(FRotator(0.f, SourceYaw - Direction * 90.f, 0.f));
	const FName Sections[] = {TEXT("Front"), TEXT("Right"), TEXT("Back"), TEXT("Left")};
	const FName ExpectedSection = bPlayerSource ? FName(TEXT("React1")) : Sections[Direction];
	const uint32 BeforeSequence = Component->ServerSequence;
	if (Checkpoint == 10) ASC(Victim)->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 1.f);
	const float BeforeHealth = Health(Victim);
	if (Checkpoint == 9) ASC(Victim)->AddLooseGameplayTag(RPGGameplayTags::Shared_Status_Invincible);
	const bool bPresent = Checkpoint <= 8;
	for (const auto& PC : ReadyClients)
		PC->FindComponentByClass<URPGBossMapE2EProbeComponent>()->ClientExpectLightHit(Victim, Checkpoint,
			BeforeSequence + (bPresent ? 1 : 0), ExpectedSection);
	FGameplayAbilitySpec* Spec = ASC(Source)->FindAbilitySpecFromHandle(bPlayerSource ? LightHitPlayerAbility : LightHitNPCAbility);
	UGameplayAbility* Ability = Spec ? Spec->GetPrimaryInstance() : nullptr;
	// Run the selected authored ability's existing damage event handler. It still
	// creates and approves its real Instant damage spec; clients never apply HP.
	UFunction* Function = Ability ? Ability->FindFunction(bPlayerSource ? TEXT("OnGameplayEventReceived") : TEXT("HandleApplyDamage")) : nullptr;
	if (!Function) { Fail(TEXT("LightHitAuthoredHandlerMissing")); return; }
	FGameplayEventData Payload;
	Payload.EventTag = RPGGameplayTags::Shared_Event_Hit;
	Payload.Instigator = Source;
	Payload.Target = Victim;
	FGameplayTagContainer LegacyTags;
	LegacyTags.AddTag(RPGGameplayTags::Shared_Event_HitReact);
	LegacyTags.AddTag(RPGGameplayTags::Shared_Ability_HitReact);
	int32 LegacyEvents = 0;
	const FDelegateHandle LegacyObserver = ASC(Victim)->AddGameplayEventTagContainerDelegate(LegacyTags,
		FGameplayEventTagMulticastDelegate::FDelegate::CreateLambda(
			[&LegacyEvents](FGameplayTag, const FGameplayEventData*) { ++LegacyEvents; }));
	Ability->ProcessEvent(Function, &Payload);
	ASC(Victim)->RemoveGameplayEventTagContainerDelegate(LegacyTags, LegacyObserver);
	if (LegacyEvents != 0) { Fail(TEXT("LightHitAlsoSentLegacyReaction")); return; }
	if (Checkpoint == 9) ASC(Victim)->RemoveLooseGameplayTag(RPGGameplayTags::Shared_Status_Invincible);
	const float AfterHealth = Health(Victim);
	if (Component->ServerSequence != BeforeSequence + (bPresent ? 1 : 0)
		|| (Checkpoint == 9 ? AfterHealth != BeforeHealth : !(AfterHealth < BeforeHealth))
		|| (Checkpoint == 10 && AfterHealth != 0.f)) { Fail(TEXT("LightHitApprovalOrLethalRouting")); return; }
	if (bPresent)
	{
		const FHitResult Hit(Victim, nullptr, Victim->GetActorLocation(), FVector::ForwardVector);
		if (Component->PresentApprovedDamageHit(Source, Hit, Component->RecentApprovedHits.Last()) != ERPGHitReactionRoute::Duplicate
			|| Component->ServerSequence != BeforeSequence + 1 || Health(Victim) != AfterHealth)
		{ Fail(TEXT("LightHitRedeliveryRepeated")); return; }
	}
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_LIGHT_HIT_APPROVED Checkpoint=%d HP=%g->%g Sequence=%u Presented=%d"),
		Checkpoint, BeforeHealth, AfterHealth, Component->ServerSequence, bPresent);
	++LightHitStep;
	LightHitNextAt = FPlatformTime::Seconds() + 1.2;
#endif
}

void URPGBossMapE2EProbeComponent::ClientSetLightHitSubjects_Implementation(APawn* Player, APawn* NPC)
{
	if (!bLightHitScenario || !bEnabled) return;
	LightHitPlayer = Player;
	LightHitNPC = NPC;
}

void URPGBossMapE2EProbeComponent::ClientExpectLightHit_Implementation(APawn* Victim, int32 Checkpoint, uint32 Sequence, FName Section)
{
	if (!bLightHitScenario || !bEnabled) return;
	LightHitExpectedVictim = Victim;
	LightHitExpectedCheckpoint = Checkpoint;
	LightHitExpectedSequence = Sequence;
	LightHitExpectedSection = Section;
}

void URPGBossMapE2EProbeComponent::TickLightHitClient()
{
#if !UE_BUILD_SHIPPING
	if (bKnockbackScenario) { TickKnockbackClient(); return; }
	if (bStunScenario) { TickStunClient(); return; }
	if (bLightHitInputScenario) { TickLightHitInputClient(); return; }
	if (!LightHitPlayer.IsValid() || !LightHitNPC.IsValid() || LightHitExpectedCheckpoint == LightHitLastConfirmed) return;
	if (LightHitExpectedCheckpoint == 0)
	{
		for (APawn* Pawn : {LightHitPlayer.Get(), LightHitNPC.Get()})
		{
			auto* Character = Cast<ARPGBaseCharacter>(Pawn);
			auto* Component = Receiver(Pawn);
			UAnimInstance* Anim = Character ? Character->GetMesh()->GetAnimInstance() : nullptr;
			if (!Anim || Anim->IsAnyMontagePlaying() || !Component || !Component->PresentationProfile || Health(Pawn) <= 0.f) return;
		}
	}
	else
	{
		auto* Component = Receiver(LightHitExpectedVictim.Get());
		if (!Component || Component->LastReceivedSequence != LightHitExpectedSequence) return;
		UAnimInstance* Anim = Component->PresentationAnimInstance.Get();
		UAnimMontage* Montage = Component->PresentationMontage.Get();
		if (LightHitExpectedCheckpoint <= 8)
		{
			const FName Expected = LightHitExpectedCheckpoint <= 4 ? FName(TEXT("AM_Guardian_LightHit")) : FName(TEXT("AM_Player_LightHit"));
			if (!Anim || !Montage || Montage->GetFName() != Expected || !Anim->Montage_IsPlaying(Montage)
				|| Anim->Montage_GetCurrentSection(Montage) != LightHitExpectedSection) return;
		}
		else if (LightHitExpectedCheckpoint == 10)
		{
			if (Health(LightHitExpectedVictim.Get()) != 0.f || Montage) return;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_LIGHT_HIT_CLIENT Checkpoint=%d Sequence=%u Section=%s OwnedPlayer=%d"),
		LightHitExpectedCheckpoint, LightHitExpectedSequence, *LightHitExpectedSection.ToString(), LightHitPlayer->IsLocallyControlled());
	LightHitLastConfirmed = LightHitExpectedCheckpoint;
	ServerConfirmLightHit(LightHitExpectedCheckpoint);
#endif
}

void URPGBossMapE2EProbeComponent::ServerConfirmLightHit_Implementation(int32 Checkpoint)
{
#if !UE_BUILD_SHIPPING
	auto* PC = Cast<APlayerController>(GetOwner());
	auto* Mode = GetWorld()->GetAuthGameMode<ARPGBossBattleGameMode>();
	auto* Driver = Mode ? Mode->FindComponentByClass<URPGBossMapE2EProbeComponent>() : nullptr;
	if (!bEnabled || !bLightHitScenario || !PC || !Driver || !Driver->bLightHitScenario || Driver->bFinished
		|| !Driver->ReadyClients.Contains(PC) || Checkpoint != Driver->LightHitStep - 1) return;
	Driver->LightHitConfirmations.FindOrAdd(Checkpoint).Add(PC);
#endif
}
