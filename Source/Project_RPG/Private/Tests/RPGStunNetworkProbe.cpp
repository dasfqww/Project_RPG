#include "Tests/RPGBossMapE2EProbeComponent.h"
#include "Tests/RPGAirborneTerrainProbe.h"

#include "Ability/RPGNPCGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Attribute/RPGAttributeSet.h"
#include "BrainComponent.h"
#include "Character/RPGPlayer.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Combat/RPGCombatControlTypes.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/RPGCharacterMovementComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Controller/RPGAIController.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "DataAsset/StartUpData/DataAsset_NPCStartUpData.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameplayEffect/RPGStunEffect.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "Navigation/PathFollowingComponent.h"
#include "RPGGameplayTags.h"
#include "RPGInputTags.h"
#include "NiagaraSystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	UAbilitySystemComponent* StunASC(APawn* Pawn)
	{
		auto* Character = Cast<ARPGBaseCharacter>(Pawn);
		return Character ? Character->GetAbilitySystemComponent() : nullptr;
	}
	FGameplayAbilitySpec* StunAttack(UAbilitySystemComponent* ASC)
	{
		return ASC ? ASC->GetActivatableAbilities().FindByPredicate([](const FGameplayAbilitySpec& Spec)
		{
			return Spec.Ability && Spec.Ability->GetClass()->GetPathName() == TEXT("/Game/Blueprints/Character/Player/Abilities/DefaultAttack/GA_Player_DefaultAttack.GA_Player_DefaultAttack_C");
		}) : nullptr;
	}
	URPGCombatControlComponent* StunControl(APawn* Pawn)
	{
		return Pawn ? Pawn->FindComponentByClass<URPGCombatControlComponent>() : nullptr;
	}
	TArray<FActiveGameplayEffectHandle> StunHandles(APawn* Pawn)
	{
		TArray<FActiveGameplayEffectHandle> Result;
		if (auto* ASC = StunASC(Pawn))
			for (auto Handle : ASC->GetActiveEffects(FGameplayEffectQuery()))
			{
				const auto* Effect = ASC->GetActiveGameplayEffect(Handle);
				if (Effect && Effect->Spec.Def->GetClass() == URPGStunEffect::StaticClass()) Result.Add(Handle);
			}
		return Result;
	}
}

void URPGBossMapE2EProbeComponent::SendStunProbeStep(int32 Step)
{
#if !UE_BUILD_SHIPPING
	StunStage = Step;
	for (const auto& PC : ReadyClients)
		PC->FindComponentByClass<URPGBossMapE2EProbeComponent>()->ClientObserveStun(LightHitPlayer.Get(), LightHitNPC.Get(), Step);
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_STUN_STEP Step=%d Clients=%d"), Step, ReadyClients.Num());
#endif
}

bool URPGBossMapE2EProbeComponent::ApplyStunProbeDamage(bool bPlayerSource, float Seconds)
{
#if !UE_BUILD_SHIPPING
	APawn* Source = bPlayerSource ? LightHitPlayer.Get() : LightHitNPC.Get();
	APawn* Target = bPlayerSource ? LightHitNPC.Get() : LightHitPlayer.Get();
	if (!Source || !Target) return false;
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAirborneTerrain")))
	{
		if (!RPGAirborneTerrainProbe::PositionNPC(*CastChecked<ACharacter>(LightHitPlayer.Get()), *CastChecked<ACharacter>(LightHitNPC.Get())))
		{ Fail(TEXT("AirborneTerrainNPCPlacementFailed")); return false; }
	}
	else LightHitNPC->SetActorLocation(LightHitPlayer->GetActorLocation() + LightHitPlayer->GetActorForwardVector() * 180.f);
	auto* Spec = StunASC(Source)->FindAbilitySpecFromHandle(bPlayerSource ? LightHitPlayerAbility : LightHitNPCAbility);
	if (!Spec || !RPGCombatControl::SetGrantedStunDuration(*Spec, Seconds)) return false;
	StunASC(Source)->MarkAbilitySpecDirty(*Spec);
	auto* Ability = Spec->GetPrimaryInstance();
	UFunction* Function = Ability ? Ability->FindFunction(bPlayerSource ? TEXT("OnGameplayEventReceived") : TEXT("HandleApplyDamage")) : nullptr;
	if (!Function) { Fail(TEXT("StunProbeDamageHandlerMissing")); return false; }
	const float Before = StunASC(Target)->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	int32 LegacyEvents = 0;
	FGameplayTagContainer Tags;
	Tags.AddTag(RPGGameplayTags::Shared_Event_HitReact); Tags.AddTag(RPGGameplayTags::Shared_Ability_HitReact);
	const auto Observer = StunASC(Target)->AddGameplayEventTagContainerDelegate(Tags,
		FGameplayEventTagMulticastDelegate::FDelegate::CreateLambda([&](FGameplayTag, const FGameplayEventData*) { ++LegacyEvents; }));
	FGameplayEventData Event;
	Event.EventTag = RPGGameplayTags::Shared_Event_Hit; Event.Instigator = Source; Event.Target = Target;
	Ability->ProcessEvent(Function, &Event);
	StunASC(Target)->RemoveGameplayEventTagContainerDelegate(Tags, Observer);
	const float After = StunASC(Target)->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	if (!(After < Before) || LegacyEvents) { Fail(TEXT("StunProbeDamageOrLegacyRouting")); return false; }
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_STUN_DAMAGE PlayerSource=%d HP=%g->%g StunSeconds=%g Stunned=%d LegacyEvents=0"),
		bPlayerSource, Before, After, Seconds, RPGCombatControl::IsStunned(Target));
	return true;
#else
	return false;
#endif
}

void URPGBossMapE2EProbeComponent::TickStunServer()
{
#if !UE_BUILD_SHIPPING
	if (ReadyClients.IsEmpty()) return;
	if (StunStage == -1)
	{
		if (!PreparePlayerAttackLoadouts()) return;
		auto* Player = Cast<ARPGPlayer>(GetWorld()->GetFirstPlayerController()->GetPawn());
		auto* Spec = StunAttack(Player ? Player->GetAbilitySystemComponent() : nullptr);
		if (!Player || !Spec) return;
		if (!LightHitNPC.IsValid())
		{
			UClass* Class = LoadClass<ARPGNonPlayerCharacter>(nullptr, TEXT("/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Gruntling_Guardian.BP_Gruntling_Guardian_C"));
			FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			auto* NPC = Class ? GetWorld()->SpawnActor<ARPGNonPlayerCharacter>(Class, Player->GetActorLocation() + Player->GetActorForwardVector() * 180.f, FRotator::ZeroRotator, Params) : nullptr;
			if (!NPC) { Fail(TEXT("StunNPCSpawnFailed")); return; }
			LightHitNPC = NPC; LightHitPlayer = Player;
		}
		auto* NPC = Cast<ARPGNonPlayerCharacter>(LightHitNPC.Get());
		auto* AI = Cast<ARPGAIController>(NPC->GetController());
		if (!AI || !NPC->IsNPCStartUpDataReady()) return;
		if (!Player->GetCharacterMovement()->IsA<URPGCharacterMovementComponent>()
			|| !NPC->GetCharacterMovement()->IsA<URPGCharacterMovementComponent>())
		{ Fail(TEXT("StunAuthoredPawnMissingSharedMovement")); return; }
		if (AI->GetBrainComponent()) AI->GetBrainComponent()->StopLogic(TEXT("Stun probe command fixture"));
		AI->StopMovement();
		auto* Data = NewObject<UDataAsset_NPCStartUpData>(this);
		UClass* Class = LoadClass<URPGNPCGameplayAbility>(nullptr, TEXT("/Game/Blueprints/Character/NPC/Gruntling/GameplayAbility/GA_Guardian_Melee1.GA_Guardian_Melee1_C"));
		Data->AttackStunDurations.Add(Class, 8.f);
		FGameplayAbilitySpec NPCSpec;
		if (!Data->BuildAttackAbilitySpec(Class, 1, NPC, NPCSpec)) { Fail(TEXT("StunNPCGrantInvalid")); return; }
		LightHitNPCAbility = StunASC(NPC)->GiveAbility(NPCSpec);
		RPGHitReaction::SetGrantedPolicy(*Spec, ERPGAttackHitReactionPolicy::None);
		LightHitPlayerAbility = Spec->Handle;
		LightInputASC = Player->GetAbilitySystemComponent();
		LightInputEndedHandle = LightInputASC->OnAbilityEnded.AddUObject(this, &ThisClass::HandleLightHitInputEnded);
		for (APawn* Pawn : {LightHitPlayer.Get(), LightHitNPC.Get()})
		{
			StunASC(Pawn)->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 10000.f);
			StunASC(Pawn)->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 10000.f);
		}
		StunViolationBaseline = Player->FindComponentByClass<URPGSecurityValidationComponent>()->GetTotalViolationCount();
		SendStunProbeStep(0);
		return;
	}
	const auto* Security = LightHitPlayer->FindComponentByClass<URPGSecurityValidationComponent>();
	if (!Security || Security->GetTotalViolationCount() != StunViolationBaseline) { Fail(TEXT("StunMovementViolation")); return; }
	if (StunStage == 0)
	{
		if (StunConfirmations.FindOrAdd(0).Num() != 1 || !StunASC(LightHitPlayer.Get())->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive()) return;
		if (!ApplyStunProbeDamage(false, 8.f)) return;
		if (!RPGCombatControl::IsStunned(LightHitPlayer.Get()) || !bLightInputCancelled) { Fail(TEXT("StunDidNotCancelCombat")); return; }
		StunOrigin = LightHitPlayer->GetActorLocation();
		SendStunProbeStep(1);
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_STUN_WAIT_LATE_JOIN ActiveStun=1 CancelledAttack=1"));
		return;
	}
	if (StunStage == 1)
	{
		for (const auto& PC : ReadyClients)
			if (!StunConfirmations.FindOrAdd(1).Contains(PC))
				PC->FindComponentByClass<URPGBossMapE2EProbeComponent>()->ClientObserveStun(LightHitPlayer.Get(), LightHitNPC.Get(), 1);
		if (ReadyClients.Num() != 2 || StunConfirmations.FindOrAdd(1).Num() != 2)
		{
			if (StunControl(LightHitPlayer.Get())->GetStunPresentationState().EndServerTime - GetWorld()->GetTimeSeconds() < 4.f)
				ApplyStunProbeDamage(false, 8.f); // Real approved refreshes keep late join inside a bounded GE lifetime.
			return;
		}
		if (RPGCombatControl::IsStunned(LightHitPlayer.Get()))
		{
			if (FVector::Dist2D(StunOrigin, LightHitPlayer->GetActorLocation()) > 10.f
				|| StunASC(LightHitPlayer.Get())->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive()) Fail(TEXT("StunInputEscapedServerControl"));
			return;
		}
		if (!StunHandles(LightHitPlayer.Get()).IsEmpty()) { Fail(TEXT("StunExpiryLeftEffect")); return; }
		SendStunProbeStep(2);
		return;
	}
	if (StunConfirmations.FindOrAdd(StunStage).Num() != 2) return;
	if (StunStage == 2)
	{
		if (!ApplyStunProbeDamage(true, 3.f) || !RPGCombatControl::IsStunned(LightHitNPC.Get())) { Fail(TEXT("PlayerNPCStunMissing")); return; }
		auto* AI = Cast<ARPGAIController>(LightHitNPC->GetController());
		FAIMoveRequest Move(LightHitNPC->GetActorLocation() + FVector(400.f, 0.f, 0.f)); Move.SetUsePathfinding(false); Move.SetProjectGoalLocation(false);
		if (!AI || AI->MoveTo(Move).Code != EPathFollowingRequestResult::Failed) { Fail(TEXT("StunNPCCommandNotBlocked")); return; }
		SendStunProbeStep(3); return;
	}
	if (StunStage == 3)
	{
		const double End = StunControl(LightHitNPC.Get())->GetStunPresentationState().EndServerTime;
		if (!ApplyStunProbeDamage(true, .5f)) return;
		auto Handles = StunHandles(LightHitNPC.Get());
		if (Handles.Num() != 2 || StunControl(LightHitNPC.Get())->GetStunPresentationState().EndServerTime < End - .02)
		{ Fail(TEXT("StunOverlapShortened")); return; }
		for (auto Handle : Handles)
			if (StunASC(LightHitNPC.Get())->GetActiveGameplayEffect(Handle)->GetDuration() < 1.f)
				StunControl(LightHitNPC.Get())->RemoveOwnedStun(Handle);
		if (!RPGCombatControl::IsStunned(LightHitNPC.Get())) { Fail(TEXT("StunCleanseRemovedOtherSource")); return; }
		SendStunProbeStep(4); return;
	}
	if (StunStage == 4)
	{
		for (auto Handle : StunHandles(LightHitNPC.Get())) StunControl(LightHitNPC.Get())->RemoveOwnedStun(Handle);
		auto* AI = Cast<ARPGAIController>(LightHitNPC->GetController());
		if (RPGCombatControl::IsStunned(LightHitNPC.Get()) || (AI && AI->GetBrainComponent() && AI->GetBrainComponent()->IsRunning()))
		{ Fail(TEXT("StunReleaseRestartedAIOrKeptControl")); return; }
		SendStunProbeStep(5); return;
	}
	if (StunStage == 5)
	{
		StunASC(LightHitPlayer.Get())->AddLooseGameplayTag(RPGCombatControl::StunImmunityTag());
		const bool bApplied = ApplyStunProbeDamage(false, 2.f);
		StunASC(LightHitPlayer.Get())->RemoveLooseGameplayTag(RPGCombatControl::StunImmunityTag());
		if (!bApplied || RPGCombatControl::IsStunned(LightHitPlayer.Get())) { Fail(TEXT("StunImmunityFailed")); return; }
		SendStunProbeStep(6); return;
	}
	if (StunStage == 6)
	{
		StunASC(LightHitPlayer.Get())->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 1.f);
		if (!ApplyStunProbeDamage(false, 2.f) || RPGCombatControl::IsStunned(LightHitPlayer.Get())) { Fail(TEXT("StunLethalReactionStarted")); return; }
		SendStunProbeStep(7); return;
	}
	if (StunStage == 7)
	{
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_STUN_COMPLETE PlayerNPC=1 NPCPlayer=1 CancelCombat=1 NaturalExpiry=1 LateJoin=1 BlockInput=1 OverlapCleanse=1 Immunity=1 Lethal=1 Clients=2"));
		CleanupLightHitInput(); bFinished = true; SetComponentTickEnabled(false);
	}
#endif
}

void URPGBossMapE2EProbeComponent::ClientObserveStun_Implementation(APawn* Player, APawn* NPC, int32 Step)
{
	if (!bEnabled || !bStunScenario || !Player || !NPC || Step < StunExpected) return;
	LightHitPlayer = Player; LightHitNPC = NPC; StunExpected = Step;
}

void URPGBossMapE2EProbeComponent::TickStunClient()
{
#if !UE_BUILD_SHIPPING
	auto* PC = Cast<APlayerController>(GetOwner());
	auto* Player = Cast<ARPGPlayer>(LightHitPlayer.Get());
	if (!PC || !Player || !LightHitNPC.IsValid() || StunExpected < 0) return;
	auto* ASC = Player->GetAbilitySystemComponent();
	const bool bOwner = PC->GetPawn() == Player;
	const double Now = FPlatformTime::Seconds();
	if (bOwner && !bLightInputBound)
	{
		auto* Spec = StunAttack(ASC);
		auto* Weapon = Player->GetPlayerCombatComponent()->GetPlayerCurrentEquippedWeapon();
		const auto* Config = LoadObject<UDataAsset_InputConfig>(nullptr, TEXT("/Game/Blueprints/Character/Player/Input/DA_InputConfig.DA_InputConfig"));
		auto* Input = PC->GetLocalPlayer() ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
		if (!Spec || !Weapon || Spec->SourceObject.Get() != Weapon || !Config || !Input || !Input->HasMappingContext(Weapon->PlayerWeaponData.WeaponInputMappingContext)) return;
		LightInputAttackAction = Config->FindAbilityInputActionByTag(RPGGameplayTags::InputTag_LightAttack_GreatSword);
		LightInputMoveAction = Config->FindNativeInputActionByTag(RPGGameplayTags::InputTag_Move);
		if (!LightInputAttackAction.IsValid() || !LightInputMoveAction.IsValid()) { Fail(TEXT("StunInputActionsMissing")); return; }
		LightInputSubsystem = Input; LightInputASC = ASC; LightHitPlayerAbility = Spec->Handle;
		LightInputEndedHandle = ASC->OnAbilityEnded.AddUObject(this, &ThisClass::HandleLightHitInputEnded);
		bLightInputBound = true;
		// Resolve the optional visual before measuring a short control window.
		LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/RPGEffects/ParticlesNiagara/StatusEffects/Stun/NS_Status_Stun.NS_Status_Stun"));
	}
	if (bOwner && StunLocalInputStep != StunExpected)
	{
		StunLocalInputStep = StunExpected; StunInputAt = Now; bStunAttemptedInput = false;
		LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputAttackAction.Get());
		if (StunExpected == 0)
			LightInputSubsystem->StartContinuousInputInjectionForAction(LightInputAttackAction.Get(), FInputActionValue(true), {}, {});
		if (StunExpected == 1 || StunExpected == 2)
		{
			StunOrigin = Player->GetActorLocation();
			// Walk away from the damage fixture so its capsule cannot cap recovery travel.
			LightInputSubsystem->StartContinuousInputInjectionForAction(LightInputMoveAction.Get(), FInputActionValue(FVector2D(0.f, -1.f)), {}, {});
		}
		else LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputMoveAction.Get());
	}
	if (bOwner && StunExpected == 1 && !bStunAttemptedInput && Now - StunInputAt > .1)
	{
		LightInputSubsystem->StartContinuousInputInjectionForAction(LightInputAttackAction.Get(), FInputActionValue(true), {}, {});
		bStunAttemptedInput = true;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_STUN_BLOCKED_INPUT EnhancedInput=1 MoveAndAttack=1"));
	}
	if (bOwner && Now - StunInputAt > .4) LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputAttackAction.Get());
	if (StunConfirmed == StunExpected) return;
	APawn* Subject = StunExpected >= 3 && StunExpected <= 5 ? LightHitNPC.Get() : LightHitPlayer.Get();
	const bool bExpected = StunExpected == 1 || StunExpected == 3 || StunExpected == 4;
	auto* Control = StunControl(Subject);
	if (!Control || RPGCombatControl::IsStunned(Subject) != bExpected || Control->GetStunPresentationState().bActive != bExpected) return;
	if (StunExpected == 0)
	{
		if (!bOwner || !StunAttack(ASC)->IsActive() || !ASC->GetCurrentMontage()) return;
	}
	if (bOwner && StunExpected == 1)
	{
		if (!bStunAttemptedInput || Now - StunInputAt < .6 || !bLightInputCancelled || StunAttack(ASC)->IsActive()) return;
		if (FVector::Dist2D(StunOrigin, Player->GetActorLocation()) > 10.f) { Fail(TEXT("StunLocalMovementEscaped")); return; }
	}
	if (bOwner && StunExpected == 2)
	{
		if (FVector::Dist2D(StunOrigin, Player->GetActorLocation()) < 100.f) return;
		LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputMoveAction.Get());
	}
	if (StunExpected == 7 && ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) != 0.f) return;
	if (bOwner && StunExpected == 1)
	{
		FString Directory;
		if (FParse::Value(FCommandLine::Get(), TEXT("RPGLightHitCaptureDir="), Directory))
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory, TEXT("Stun-Active.png")), false, false);
	}
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_STUN_CLIENT Step=%d Active=%d Owner=%d Revision=%u"),
		StunExpected, bExpected, bOwner, Control->GetStunPresentationState().Revision);
	StunConfirmed = StunExpected;
	ServerConfirmStun(StunExpected);
#endif
}

void URPGBossMapE2EProbeComponent::ServerConfirmStun_Implementation(int32 Step)
{
#if !UE_BUILD_SHIPPING
	auto* PC = Cast<APlayerController>(GetOwner());
	auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr;
	auto* Driver = Mode ? Mode->FindComponentByClass<URPGBossMapE2EProbeComponent>() : nullptr;
	if (bEnabled && bStunScenario && Driver && Driver->bStunScenario && !Driver->bFinished
		&& Driver->ReadyClients.Contains(PC) && Step == Driver->StunStage) Driver->StunConfirmations.FindOrAdd(Step).Add(PC);
#endif
}
