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
#include "Component/RPGSecurityValidationComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Controller/RPGAIController.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "DataAsset/StartUpData/DataAsset_NPCStartUpData.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayEffect/RPGStunEffect.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NiagaraSystem.h"
#include "RPGGameplayTags.h"
#include "RPGInputTags.h"
#include "UnrealClient.h"

namespace
{
	UAbilitySystemComponent* KnockASC(APawn* Pawn)
	{
		const auto* Character = Cast<ARPGBaseCharacter>(Pawn);
		return Character ? Character->GetAbilitySystemComponent() : nullptr;
	}
	URPGCombatControlComponent* KnockControl(APawn* Pawn)
	{
		return Pawn ? Pawn->FindComponentByClass<URPGCombatControlComponent>() : nullptr;
	}
	FGameplayAbilitySpec* KnockAttack(UAbilitySystemComponent* ASC)
	{
		return ASC ? ASC->GetActivatableAbilities().FindByPredicate([](const FGameplayAbilitySpec& Spec)
		{
			return Spec.Ability && Spec.Ability->GetClass()->GetPathName() == TEXT("/Game/Blueprints/Character/Player/Abilities/DefaultAttack/GA_Player_DefaultAttack.GA_Player_DefaultAttack_C");
		}) : nullptr;
	}
	float KnockHealth(APawn* Pawn) { return KnockASC(Pawn)->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()); }
	void LogKnockbackNetwork(const UWorld* World)
	{
#if DO_ENABLE_NET_TEST
		if (const auto* Driver = World ? World->GetNetDriver() : nullptr)
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_KNOCKBACK_NET LagMin=%d LagMax=%d Loss=%d"),
				Driver->PacketSimulationSettings.PktLagMin, Driver->PacketSimulationSettings.PktLagMax, Driver->PacketSimulationSettings.PktLoss);
#endif
	}
}

void URPGBossMapE2EProbeComponent::SendKnockbackProbeStep(int32 Step, APawn* Subject)
{
#if !UE_BUILD_SHIPPING
	KnockbackStage = Step;
	const FVector Location = Subject ? Subject->GetActorLocation() : FVector::ZeroVector;
	for (const auto& PC : ReadyClients)
		PC->FindComponentByClass<URPGBossMapE2EProbeComponent>()->ClientObserveKnockback(LightHitPlayer.Get(), LightHitNPC.Get(), Step, Location);
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_KNOCKBACK_STEP Step=%d Location=%s"), Step, *Location.ToCompactString());
#endif
}

bool URPGBossMapE2EProbeComponent::ApplyKnockbackProbeDamage(bool bPlayerSource, float Distance, float Duration, float StunDuration)
{
#if !UE_BUILD_SHIPPING
	APawn* Source = bPlayerSource ? LightHitPlayer.Get() : LightHitNPC.Get();
	APawn* Target = bPlayerSource ? LightHitNPC.Get() : LightHitPlayer.Get();
	auto* Spec = Source ? KnockASC(Source)->FindAbilitySpecFromHandle(bPlayerSource ? LightHitPlayerAbility : LightHitNPCAbility) : nullptr;
	FRPGKnockbackSettings Settings; Settings.Distance = Distance; Settings.Duration = Duration;
	if (!Spec || !RPGKnockback::SetGrantedSettings(*Spec, Settings)) { Fail(TEXT("KnockbackGrantInvalid")); return false; }
	// Same real Player/NPC damage handlers and approval/legacy-event observer as the stun probe.
	if (!ApplyStunProbeDamage(bPlayerSource, StunDuration)) return false;
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_KNOCKBACK_DAMAGE PlayerSource=%d Distance=%g Duration=%g Active=%d Id=%u"),
		bPlayerSource, Distance, Duration, KnockControl(Target)->GetKnockbackState().bActive, KnockControl(Target)->GetKnockbackState().ExecutionId);
	return true;
#else
	return false;
#endif
}

void URPGBossMapE2EProbeComponent::TickKnockbackServer()
{
#if !UE_BUILD_SHIPPING
	if (KnockbackStage == -1)
	{
		const bool bAuthoredStagger = FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAuthoredStagger"));
		if (bAuthoredStagger && ReadyClients.Num() != 2) return;
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAirborneTerrain")))
		{
			const auto* TerrainMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			if (!TerrainMesh) { Fail(TEXT("AirborneTerrainMeshMissing")); return; }
#if WITH_EDITOR
			if (TerrainMesh->IsCompiling()) return;
#endif
		}
		if (!PreparePlayerAttackLoadouts()) return;
		auto* Player = Cast<ARPGPlayer>(GetWorld()->GetFirstPlayerController()->GetPawn());
		auto* Spec = KnockAttack(Player ? Player->GetAbilitySystemComponent() : nullptr);
		if (!Player || !Spec) return;
		if (!LightHitNPC.IsValid())
		{
			UClass* Class = LoadClass<ARPGNonPlayerCharacter>(nullptr, TEXT("/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Gruntling_Guardian.BP_Gruntling_Guardian_C"));
			FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			LightHitNPC = Class ? GetWorld()->SpawnActor<ARPGNonPlayerCharacter>(Class, Player->GetActorLocation() + Player->GetActorForwardVector() * 180.f, FRotator::ZeroRotator, Params) : nullptr;
			LightHitPlayer = Player;
		}
		auto* NPC = Cast<ARPGNonPlayerCharacter>(LightHitNPC.Get());
		auto* AI = NPC ? Cast<ARPGAIController>(NPC->GetController()) : nullptr;
		if (!AI || !NPC->IsNPCStartUpDataReady()) return;
		if (AI->GetBrainComponent()) AI->GetBrainComponent()->StopLogic(TEXT("Knockback probe fixture")); AI->StopMovement();
		// Keep the observer's real pawn out of the travel lane. Use its own bounded
		// authorization rather than weakening collision/security for the victim.
		for (const auto& PC : ReadyClients)
		{
			APawn* Observer = PC->GetPawn(); if (!Observer || Observer == Player) continue;
			auto* ObserverSecurity = Observer->FindComponentByClass<URPGSecurityValidationComponent>();
			if (!ObserverSecurity || ObserverSecurity->IsMovementAuthorizationActive()) return;
			const auto Authorization = ObserverSecurity->BeginMovementAuthorization(.25f, 800.f, TEXT("KnockbackProbeObserver"));
			if (!Authorization.IsValid()) { Fail(TEXT("KnockbackObserverFixtureAuthorization")); return; }
			Observer->SetActorLocation(Player->GetActorLocation() + Player->GetActorRightVector() * 700.f);
			ObserverSecurity->EndMovementAuthorization(Authorization, true);
		}
		auto* Data = NewObject<UDataAsset_NPCStartUpData>(this);
		UClass* Class = LoadClass<URPGNPCGameplayAbility>(nullptr, TEXT("/Game/Blueprints/Character/NPC/Gruntling/GameplayAbility/GA_Guardian_Melee1.GA_Guardian_Melee1_C"));
		Data->AttackHitReactionPolicies.Add(Class, ERPGAttackHitReactionPolicy::None);
		FGameplayAbilitySpec NPCSpec;
		if (!Data->BuildAttackAbilitySpec(Class, 1, NPC, NPCSpec)) { Fail(TEXT("KnockbackNPCGrantInvalid")); return; }
		if (bAuthoredStagger)
		{
			// Inspect the original startup grant; never replace authored stagger metadata.
			auto* Authored = KnockASC(NPC)->FindAbilitySpecFromClass(Class);
			if (!Authored || !FMath::IsNearlyEqual(RPGStagger::GetGrantedSettings(Authored).Duration, .35f)
				|| !FMath::IsNearlyEqual(RPGStagger::GetGrantedSettings(Spec).Duration, .25f))
			{ Fail(TEXT("AuthoredStaggerStartupGrantMissing")); return; }
			LightHitNPCAbility = Authored->Handle;
		}
		else
		{
			LightHitNPCAbility = KnockASC(NPC)->GiveAbility(NPCSpec);
			RPGHitReaction::SetGrantedPolicy(*Spec, ERPGAttackHitReactionPolicy::None);
		}
		LightHitPlayerAbility = Spec->Handle;
		LightInputASC = Player->GetAbilitySystemComponent();
		LightInputEndedHandle = LightInputASC->OnAbilityEnded.AddUObject(this, &ThisClass::HandleLightHitInputEnded);
		for (APawn* Pawn : {LightHitPlayer.Get(), LightHitNPC.Get()})
		{
			KnockASC(Pawn)->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 10000.f);
			KnockASC(Pawn)->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 10000.f);
		}
		const bool bTerrain = FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAirborneTerrain"));
		if (bTerrain && !RPGAirborneTerrainProbe::Prepare(*Player, *NPC, KnockbackProbeWall))
		{ Fail(TEXT("AirborneTerrainPreparationFailed")); return; }
		KnockbackViolationBaseline = Player->FindComponentByClass<URPGSecurityValidationComponent>()->GetTotalViolationCount();
		LogKnockbackNetwork(GetWorld());
		SendKnockbackProbeStep(0, bTerrain ? Player : nullptr); return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAirborne"))) { TickAirborneServer(); return; }
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESlow"))) { TickSlowServer(); return; }
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EBurn"))) { TickBurnServer(); return; }
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EStagger"))) { TickStaggerServer(); return; }
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EMontageImmunity"))) { TickMontageImmunityServer(); return; }
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EImmunity"))) { TickImmunityServer(); return; }
	auto* Security = LightHitPlayer->FindComponentByClass<URPGSecurityValidationComponent>();
	if (Security->GetTotalViolationCount() != KnockbackViolationBaseline) { Fail(TEXT("KnockbackMovementViolation")); return; }
	auto* PlayerControl = KnockControl(LightHitPlayer.Get()); auto* NPCControl = KnockControl(LightHitNPC.Get());
	const bool bConfirmed = KnockbackConfirmations.FindOrAdd(KnockbackStage).Num() == 2;
	const auto AwaitObservedControl = [this, bConfirmed](const URPGCombatControlComponent& Control)
	{
		if (bConfirmed) return false;
		const auto& State = Control.GetKnockbackState();
		// Clients must observe the ACTIVE GE/RMS before acknowledging. Only delivery
		// of that reliable acknowledgement may follow expiry under lag/packet loss.
		if (GetWorld()->GetGameState()->GetServerWorldTimeSeconds() > State.StartServerTime + State.Duration + 2.0)
			Fail(TEXT("KnockbackActiveObservationAcknowledgementTimeout"));
		return true;
	};
	if (KnockbackStage == 0 && bConfirmed)
	{
		if (!KnockASC(LightHitPlayer.Get())->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive()) return;
		KnockbackProbeOrigin = LightHitPlayer->GetActorLocation();
		if (!ApplyKnockbackProbeDamage(false, 450.f, 1.2f)) return;
		if (!PlayerControl->HasKnockbackMotion() || !Security->IsMovementAuthorizationActive() || !bLightInputCancelled)
		{ Fail(TEXT("KnockbackPlayerStartOrCancel")); return; }
		SendKnockbackProbeStep(1); return;
	}
	if (KnockbackStage == 1 && !PlayerControl->GetKnockbackState().bActive)
	{
		const float Distance = FVector::Dist2D(KnockbackProbeOrigin, LightHitPlayer->GetActorLocation());
		if (Distance < 200.f || Distance > 490.f || PlayerControl->HasKnockbackMotion() || Security->IsMovementAuthorizationActive())
		{
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_KNOCKBACK_PLAYER_GATE Distance=%g Confirmed=%d Motion=%d Authorization=%d EndReason=%d"),
				Distance, bConfirmed, PlayerControl->HasKnockbackMotion(), Security->IsMovementAuthorizationActive(),
				static_cast<int32>(PlayerControl->GetKnockbackState().EndReason));
			Fail(TEXT("KnockbackPlayerTravelOrCleanup")); return;
		}
		if (AwaitObservedControl(*PlayerControl)) return;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_KNOCKBACK_TRAVEL Player=1 Distance=%g AuthorizationEnded=1"), Distance);
		SendKnockbackProbeStep(2, LightHitPlayer.Get()); return;
	}
	if (KnockbackStage == 2 && bConfirmed)
	{
		if (!ApplyKnockbackProbeDamage(true, 400.f, 1.2f)) return;
		KnockbackProbeOrigin = LightHitNPC->GetActorLocation(); SendKnockbackProbeStep(3); return;
	}
	if (KnockbackStage == 3 && !NPCControl->GetKnockbackState().bActive)
	{
		const float Distance = FVector::Dist2D(KnockbackProbeOrigin, LightHitNPC->GetActorLocation());
		auto* AI = Cast<ARPGAIController>(LightHitNPC->GetController());
		if (Distance < 180.f || Distance > 440.f || (AI->GetBrainComponent() && AI->GetBrainComponent()->IsRunning()))
		{ Fail(TEXT("KnockbackNPCTravelOrBrain")); return; }
		if (AwaitObservedControl(*NPCControl)) return;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_KNOCKBACK_TRAVEL Player=0 Distance=%g BrainNotRestarted=1"), Distance);
		SendKnockbackProbeStep(4, LightHitNPC.Get()); return;
	}
	if (KnockbackStage == 4 && bConfirmed)
	{
		LightHitNPC->SetActorLocation(LightHitPlayer->GetActorLocation() + LightHitPlayer->GetActorForwardVector() * 180.f);
		const FVector Direction = (LightHitNPC->GetActorLocation() - LightHitPlayer->GetActorLocation()).GetSafeNormal2D();
		FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Wall = GetWorld()->SpawnActor<AActor>(Params);
		auto* Box = NewObject<UBoxComponent>(Wall, TEXT("KnockbackProbeWall")); Wall->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(20.f, 220.f, 200.f)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
		Wall->SetActorLocation(LightHitNPC->GetActorLocation() + Direction * 160.f); Wall->SetActorRotation(Direction.Rotation());
		KnockbackProbeWall = Wall;
		if (!ApplyKnockbackProbeDamage(true, 450.f, 1.2f)) return;
		KnockbackProbeOrigin = LightHitNPC->GetActorLocation(); KnockbackStage = 50; return;
	}
	if (KnockbackStage == 50 && !NPCControl->GetKnockbackState().bActive)
	{
		const float Distance = FVector::Dist2D(KnockbackProbeOrigin, LightHitNPC->GetActorLocation());
		if (NPCControl->GetKnockbackState().EndReason != ERPGKnockbackEndReason::Wall || Distance > 150.f || Distance < 1.f)
		{ Fail(TEXT("KnockbackWallNotStopped")); return; }
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_KNOCKBACK_WALL Distance=%g Collision=1"), Distance);
		SendKnockbackProbeStep(5, LightHitNPC.Get()); return;
	}
	if (KnockbackStage == 5 && bConfirmed)
	{
		if (KnockbackProbeWall.IsValid()) KnockbackProbeWall->Destroy();
		if (!ApplyKnockbackProbeDamage(false, 450.f, 1.5f)) return;
		SendKnockbackProbeStep(6); return;
	}
	if (KnockbackStage == 6 && bConfirmed)
	{
		const uint32 Old = PlayerControl->GetKnockbackState().ExecutionId;
		const auto OldAuthorization = PlayerControl->KnockbackMovementAuthorization;
		if (!ApplyKnockbackProbeDamage(false, 300.f, 1.f)) return;
		Security->EndMovementAuthorization(OldAuthorization, true);
		if (PlayerControl->GetKnockbackState().ExecutionId == Old || PlayerControl->CancelKnockback(Old) || !Security->IsMovementAuthorizationActive())
		{ Fail(TEXT("KnockbackStaleCleanupCancelledReplacement")); return; }
		SendKnockbackProbeStep(7); return;
	}
	if (KnockbackStage == 7 && !PlayerControl->GetKnockbackState().bActive)
	{
		if (Security->IsMovementAuthorizationActive()) { Fail(TEXT("KnockbackReplacementNotObserved")); return; }
		if (AwaitObservedControl(*PlayerControl)) return;
		SendKnockbackProbeStep(8, LightHitPlayer.Get()); return;
	}
	if (KnockbackStage == 8 && bConfirmed)
	{
		if (!ApplyKnockbackProbeDamage(false, 300.f, .8f, 3.f)) return;
		if (!RPGCombatControl::IsStunned(LightHitPlayer.Get()) || !PlayerControl->HasKnockbackMotion()) { Fail(TEXT("KnockbackStunComposition")); return; }
		SendKnockbackProbeStep(9); return;
	}
	if (KnockbackStage == 9 && !PlayerControl->GetKnockbackState().bActive)
	{
		if (!RPGCombatControl::IsStunned(LightHitPlayer.Get())) { Fail(TEXT("KnockbackEndCleansedStun")); return; }
		if (AwaitObservedControl(*PlayerControl)) return;
		SendKnockbackProbeStep(10, LightHitPlayer.Get()); return;
	}
	if (KnockbackStage == 10 && bConfirmed)
	{
		for (auto Handle : KnockASC(LightHitPlayer.Get())->GetActiveEffects(FGameplayEffectQuery()))
		{
			const auto* Effect = KnockASC(LightHitPlayer.Get())->GetActiveGameplayEffect(Handle);
			if (Effect && Effect->Spec.Def->GetClass() == URPGStunEffect::StaticClass()) PlayerControl->RemoveOwnedStun(Handle);
		}
		SendKnockbackProbeStep(11, LightHitPlayer.Get()); return;
	}
	if (KnockbackStage == 11 && bConfirmed)
	{
		KnockASC(LightHitPlayer.Get())->AddLooseGameplayTag(RPGKnockback::ImmunityTag());
		const bool bDamage = ApplyKnockbackProbeDamage(false, 300.f, .8f);
		KnockASC(LightHitPlayer.Get())->RemoveLooseGameplayTag(RPGKnockback::ImmunityTag());
		if (!bDamage || PlayerControl->GetKnockbackState().bActive) { Fail(TEXT("KnockbackImmunityFailed")); return; }
		SendKnockbackProbeStep(12, LightHitPlayer.Get()); return;
	}
	if (KnockbackStage == 12 && bConfirmed)
	{
		if (!ApplyKnockbackProbeDamage(false, 350.f, 1.2f)) return;
		SendKnockbackProbeStep(13); return;
	}
	if (KnockbackStage == 13 && !PlayerControl->HasKnockbackMotion())
	{
		Fail(TEXT("KnockbackDeathSetupExpiredBeforeObservation")); return;
	}
	if (KnockbackStage == 13 && bConfirmed)
	{
		KnockASC(LightHitPlayer.Get())->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 1.f);
		if (!ApplyKnockbackProbeDamage(false, 0.f, .4f)) return;
		if (PlayerControl->GetKnockbackState().bActive || PlayerControl->HasKnockbackMotion() || Security->IsMovementAuthorizationActive())
		{ Fail(TEXT("KnockbackDeathCleanup")); return; }
		SendKnockbackProbeStep(14, LightHitPlayer.Get()); return;
	}
	if (KnockbackStage == 14 && bConfirmed)
	{
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_KNOCKBACK_COMPLETE PlayerNPC=1 NPCPlayer=1 OwnerObserver=1 InputBlocked=1 Converged=1 Wall=1 Replace=1 Stun=1 Immunity=1 Death=1 SecurityViolations=0 Clients=2"));
		CleanupLightHitInput(); bFinished = true; SetComponentTickEnabled(false);
	}
#endif
}

void URPGBossMapE2EProbeComponent::ClientObserveKnockback_Implementation(APawn* Player, APawn* NPC, int32 Step, FVector ExpectedLocation)
{
	if (!bEnabled || !bKnockbackScenario || !Player || !NPC || Step < KnockbackExpected) return;
	LightHitPlayer = Player; LightHitNPC = NPC; KnockbackExpected = Step; KnockbackExpectedLocation = ExpectedLocation;
}

void URPGBossMapE2EProbeComponent::TickKnockbackClient()
{
#if !UE_BUILD_SHIPPING
	auto* PC = Cast<APlayerController>(GetOwner()); auto* Player = Cast<ARPGPlayer>(LightHitPlayer.Get());
	if (!PC || !Player || !LightHitNPC.IsValid() || KnockbackExpected < 0) return;
	auto* ASC = Player->GetAbilitySystemComponent(); const bool bOwner = PC->GetPawn() == Player;
	const double Now = FPlatformTime::Seconds();
	if (bOwner && !bLightInputBound)
	{
		auto* Spec = KnockAttack(ASC); auto* Weapon = Player->GetPlayerCombatComponent()->GetPlayerCurrentEquippedWeapon();
		const auto* Config = LoadObject<UDataAsset_InputConfig>(nullptr, TEXT("/Game/Blueprints/Character/Player/Input/DA_InputConfig.DA_InputConfig"));
		auto* Input = PC->GetLocalPlayer() ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
		if (!Spec || !Weapon || Spec->SourceObject.Get() != Weapon || !Config || !Input || !Input->HasMappingContext(Weapon->PlayerWeaponData.WeaponInputMappingContext)) return;
		LightInputAttackAction = Config->FindAbilityInputActionByTag(RPGGameplayTags::InputTag_LightAttack_GreatSword);
		LightInputMoveAction = Config->FindNativeInputActionByTag(RPGGameplayTags::InputTag_Move);
		if (!LightInputAttackAction.IsValid() || !LightInputMoveAction.IsValid()) { Fail(TEXT("KnockbackInputMissing")); return; }
		LightInputSubsystem = Input; LightInputASC = ASC; LightHitPlayerAbility = Spec->Handle;
		LightInputEndedHandle = ASC->OnAbilityEnded.AddUObject(this, &ThisClass::HandleLightHitInputEnded);
		bLightInputBound = true;
		LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/RPGEffects/ParticlesNiagara/StatusEffects/Stun/NS_Status_Stun.NS_Status_Stun"));
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAirborne")))
	{
		if (KnockbackConfirmed < 0) LogKnockbackNetwork(GetWorld());
		TickAirborneClient(); return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESlow")))
	{
		if (KnockbackConfirmed < 0) LogKnockbackNetwork(GetWorld());
		TickSlowClient(); return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EBurn")))
	{
		if (KnockbackConfirmed < 0) LogKnockbackNetwork(GetWorld());
		TickBurnClient(); return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EStagger")))
	{
		if (KnockbackConfirmed < 0) LogKnockbackNetwork(GetWorld());
		TickStaggerClient(); return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EMontageImmunity")))
	{
		if (KnockbackConfirmed < 0) LogKnockbackNetwork(GetWorld());
		TickMontageImmunityClient(); return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EImmunity")))
	{
		if (KnockbackConfirmed < 0) LogKnockbackNetwork(GetWorld());
		TickImmunityClient(); return;
	}
	if (bOwner && KnockbackInputStep != KnockbackExpected)
	{
		KnockbackInputStep = KnockbackExpected; KnockbackInputAt = Now; bKnockbackAttemptedInput = false;
		LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputAttackAction.Get());
		LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputMoveAction.Get());
		if (KnockbackExpected == 0) LightInputSubsystem->StartContinuousInputInjectionForAction(LightInputAttackAction.Get(), FInputActionValue(true), {}, {});
		if (KnockbackExpected == 1 || KnockbackExpected == 10)
			LightInputSubsystem->StartContinuousInputInjectionForAction(LightInputMoveAction.Get(), FInputActionValue(FVector2D(0.f, 1.f)), {}, {});
	}
	if (bOwner && KnockbackExpected == 1 && !bKnockbackAttemptedInput && Now - KnockbackInputAt > .1)
	{
		LightInputSubsystem->StartContinuousInputInjectionForAction(LightInputAttackAction.Get(), FInputActionValue(true), {}, {});
		bKnockbackAttemptedInput = true;
	}
	if (bOwner && Now - KnockbackInputAt > .4) LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputAttackAction.Get());
	if (KnockbackConfirmed == KnockbackExpected) return;
	APawn* Subject = KnockbackExpected >= 3 && KnockbackExpected <= 5 ? LightHitNPC.Get() : LightHitPlayer.Get();
	auto* Control = KnockControl(Subject);
	const bool bExpected = KnockbackExpected == 1 || KnockbackExpected == 3 || KnockbackExpected == 6 || KnockbackExpected == 7 || KnockbackExpected == 9 || KnockbackExpected == 13;
	static double NextDiagnostic = 0.0; // Opt-in probe only; one local client per process.
	if (Control && Now >= NextDiagnostic)
	{
		NextDiagnostic = Now + .2;
		const auto& State = Control->GetKnockbackState();
		const auto* Anim = Cast<ACharacter>(Subject)->GetMesh()->GetAnimInstance();
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_KNOCKBACK_WAIT Step=%d Owner=%d Active=%d Tag=%d Motion=%d Pose=%d AnyMontage=%d Id=%u Name=%s Elapsed=%g Duration=%g"),
			KnockbackExpected, bOwner, State.bActive, KnockASC(Subject)->HasMatchingGameplayTag(RPGKnockback::StatusTag()),
			Control->HasKnockbackMotion(), Control->KnockbackMontage.IsValid(), Anim && Anim->IsAnyMontagePlaying(),
			State.ExecutionId, *Control->KnockbackMotionName.ToString(), GetWorld()->GetGameState()->GetServerWorldTimeSeconds() - State.StartServerTime, State.Duration);
	}
	if (!Control || Control->GetKnockbackState().bActive != bExpected
		|| KnockASC(Subject)->HasMatchingGameplayTag(RPGKnockback::StatusTag()) != bExpected) return;
	if (KnockbackExpected == 0 && bOwner && (!KnockAttack(ASC)->IsActive() || !ASC->GetCurrentMontage())) return;
	if (bExpected)
	{
		if (!Control->HasKnockbackMotion()) return;
		// The input-hold assertion belongs only to the first input-blocking case.
		// Other active snapshots are confirmed as soon as GE, RMS and pose are observed.
		if (bOwner && KnockbackExpected == 1 && Now - KnockbackInputAt < .3) return;
		if (bOwner && KnockbackExpected == 1 && (!bKnockbackAttemptedInput || !bLightInputCancelled || KnockAttack(ASC)->IsActive())) return;
		if (bOwner && !Control->KnockbackMontage.IsValid()) return;
	}
	if (KnockbackExpected == 2 || KnockbackExpected == 4 || KnockbackExpected == 5 || KnockbackExpected == 8 || KnockbackExpected == 11)
	{
		if (FVector::Dist2D(Subject->GetActorLocation(), KnockbackExpectedLocation) > 25.f) return;
	}
	if (KnockbackExpected == 10)
	{
		if (!RPGCombatControl::IsStunned(Player)) return;
		if (FVector::Dist2D(Player->GetActorLocation(), KnockbackExpectedLocation) > (bOwner ? 5.f : 25.f)) return;
		if (bOwner && !bKnockbackAttemptedInput)
		{
			bKnockbackAttemptedInput = true; KnockbackInputAt = Now; KnockbackProbeOrigin = Player->GetActorLocation(); return;
		}
		if (bOwner && Now - KnockbackInputAt < .3) return;
		if (bOwner && FVector::Dist2D(Player->GetActorLocation(), KnockbackProbeOrigin) > 10.f) { Fail(TEXT("KnockbackReleasedOtherStun")); return; }
		if (bOwner) LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputMoveAction.Get());
	}
	if (KnockbackExpected == 11 && RPGCombatControl::IsActionBlocked(Player)) return;
	if (KnockbackExpected == 14 && KnockHealth(Player) != 0.f) return;
	if (bOwner && KnockbackExpected == 1)
	{
		// The held-input assertion has completed while control is active. Do not
		// keep injecting voluntary movement after expiry while the ACK is in flight.
		LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputMoveAction.Get());
		LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputAttackAction.Get());
		FString Directory;
		if (FParse::Value(FCommandLine::Get(), TEXT("RPGLightHitCaptureDir="), Directory))
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory, TEXT("Knockback-Active.png")), false, false);
	}
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_KNOCKBACK_CLIENT Step=%d Active=%d Owner=%d Id=%u Location=%s"),
		KnockbackExpected, bExpected, bOwner, Control->GetKnockbackState().ExecutionId, *Subject->GetActorLocation().ToCompactString());
	if (KnockbackExpected == 0) LogKnockbackNetwork(GetWorld());
	KnockbackConfirmed = KnockbackExpected; ServerConfirmKnockback(KnockbackExpected);
#endif
}

void URPGBossMapE2EProbeComponent::ServerConfirmKnockback_Implementation(int32 Step)
{
#if !UE_BUILD_SHIPPING
	auto* PC = Cast<APlayerController>(GetOwner()); auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr;
	auto* Driver = Mode ? Mode->FindComponentByClass<URPGBossMapE2EProbeComponent>() : nullptr;
	if (bEnabled && bKnockbackScenario && Driver && Driver->bKnockbackScenario && !Driver->bFinished
		&& Driver->ReadyClients.Contains(PC) && Step == Driver->KnockbackStage) Driver->KnockbackConfirmations.FindOrAdd(Step).Add(PC);
#endif
}
