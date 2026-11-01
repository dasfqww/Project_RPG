#include "Tests/RPGBossMapE2EProbeComponent.h"
#include "Tests/RPGAirborneJoinGate.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "GameplayCueManager.h"
#include "GameplayCueSet.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Attribute/RPGAttributeSet.h"
#include "Components/CapsuleComponent.h"
#include "Character/RPGBaseCharacter.h"
#include "Combat/RPGAirbornePresentationProfile.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "HAL/FileManager.h"
#include "InputActionValue.h"
#include "InputAction.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "UnrealClient.h"

namespace
{
	UAbilitySystemComponent* AirASC(APawn* P) { return CastChecked<ARPGBaseCharacter>(P)->GetAbilitySystemComponent(); }
	URPGCombatControlComponent* AirControl(APawn* P) { return P->FindComponentByClass<URPGCombatControlComponent>(); }
	bool AirborneLateJoin() { return FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAirborneLateJoin")); }
	int32 AirborneJoinStep()
	{
		FString Phase; FParse::Value(FCommandLine::Get(), TEXT("RPGAirborneJoinPhase="), Phase);
		const int32 Step = Phase.Equals(TEXT("Downed"), ESearchCase::IgnoreCase) ? 2 : Phase.Equals(TEXT("GettingUp"), ESearchCase::IgnoreCase) ? 3 : 1;
		return Step + (FParse::Param(FCommandLine::Get(), TEXT("RPGAirborneJoinNPC")) ? 4 : 0);
	}
	void PrepareAirborneJoinObservation(APawn* Pawn, bool bFollowup = false)
	{
		// Keep initial Airborne visible through controller/actor replication using
		// supported physics. Other initial phases retain the authored gravity.
		if (AirborneLateJoin() && (AirborneJoinStep() == 1 || AirborneJoinStep() == 5 || bFollowup))
			if (auto* Character=Cast<ACharacter>(Pawn)) Character->GetCharacterMovement()->GravityScale=.32f;
	}
}

FString RPGAirborneJoinGate::File(const TCHAR* Name)
{
	FString Directory;
	if (!FParse::Value(FCommandLine::Get(), TEXT("RPGAirborneJoinDirectory="), Directory)
		|| !FPaths::IsUnderDirectory(Directory, FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved")))) return {};
	return FPaths::Combine(Directory, Name);
}

bool RPGAirborneJoinGate::DelayCompletion(UGameInstance& Instance)
{
#if !UE_BUILD_SHIPPING
	if (!AirborneLateJoin() || !FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAirborneJoinGate"))) return false;
	const FString MapReady = File(TEXT("map.ready")), JoinReady = File(TEXT("join.ready"));
	if (MapReady.IsEmpty() || !Instance.GetWorld() || !Instance.GetWorld()->GetMapName().Contains(TEXT("bossmap")))
	{
		UE_LOG(LogTemp, Error, TEXT("RAIDMAP_E2E_FAIL Reason=AirborneJoinInvalidRendezvous"));
		FPlatformMisc::RequestExitWithStatus(false, 1); return true;
	}
	// The engine calls this after LoadMap and before sending NMT_Join. Transport
	// remains live; no gameplay controller or victim snapshot has been received.
	if (!IFileManager::Get().FileExists(*MapReady))
	{
		auto* PlayerClass = LoadClass<ACharacter>(nullptr, TEXT("/Game/Blueprints/Character/Player/BP_Player.BP_Player_C"));
		auto* Guardian = LoadClass<ACharacter>(nullptr, TEXT("/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Gruntling_Guardian.BP_Gruntling_Guardian_C"));
		auto* BossClass = LoadClass<ACharacter>(nullptr, TEXT("/Game/Blueprints/Character/NPC/SwordMan/BP_RaidSwordMan.BP_RaidSwordMan_C"));
		// Actor replication otherwise starts the first cue scan/Blueprint load after
		// NMT_Join, making the short phase test measure cold editor asset loading.
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().WaitForCompletion();
		auto* CueManager = UAbilitySystemGlobals::Get().GetGameplayCueManager();
		int32 CueClasses = 0;
		if (CueManager)
		{
			CueManager->InitializeRuntimeObjectLibrary();
			if (const auto* Set = CueManager->GetRuntimeCueSet())
				for (const auto& Cue : Set->GameplayCueData)
					if (auto* Loaded = Cue.GameplayCueNotifyObj.TryLoad()) { Instance.RegisterReferencedObject(Loaded); ++CueClasses; }
		}
		FlushAsyncLoading();
		if (!PlayerClass || !Guardian || !BossClass || !CueManager || CueClasses == 0 || !FFileHelper::SaveStringToFile(TEXT("loaded"), *MapReady))
		{
			UE_LOG(LogTemp, Error, TEXT("RAIDMAP_E2E_FAIL Reason=AirborneJoinMapPreparationFailed"));
			FPlatformMisc::RequestExitWithStatus(false, 1); return true;
		}
		Instance.RegisterReferencedObject(PlayerClass); Instance.RegisterReferencedObject(Guardian);
		Instance.RegisterReferencedObject(BossClass);
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_AIRBORNE_JOIN_ASSETS_READY BossClass=1 CueClasses=%d"), CueClasses);
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_AIRBORNE_JOIN_MAP_READY GameplayJoined=0"));
	}
	if (!IFileManager::Get().FileExists(*JoinReady)) return true;
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_AIRBORNE_JOIN_RELEASE SendingJoin=1"));
	return false;
#else
	return false;
#endif
}
bool URPGBossMapE2EProbeComponent::ApplyAirborneProbeDamage(bool bPlayerSource)
{
#if !UE_BUILD_SHIPPING
	APawn* Source = bPlayerSource ? LightHitPlayer.Get() : LightHitNPC.Get();
	PrepareAirborneJoinObservation(bPlayerSource ? LightHitNPC.Get() : LightHitPlayer.Get(), bPlayerSource && AirborneJoinStep() <= 3);
	if (bPlayerSource && FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAirborneTerrain")) && KnockbackProbeWall.IsValid())
	{
		// Face along the slope: the next victim starts higher and leaves its far edge.
		const FRotator Facing(0, KnockbackProbeWall->GetActorRotation().Yaw+180.f, 0);
		LightHitPlayer->SetActorRotation(Facing);
		if (auto* Controller=Cast<APlayerController>(LightHitPlayer->GetController()))
		{ Controller->SetControlRotation(Facing); Controller->ClientSetRotation(Facing, true); }
	}
	auto* Spec = AirASC(Source)->FindAbilitySpecFromHandle(bPlayerSource ? LightHitPlayerAbility : LightHitNPCAbility);
	FRPGKnockbackSettings Settings; Settings.Distance = 300; Settings.Height = 400; Settings.DownDuration = 2.5f; Settings.GetUpDuration = 2.5f;
	if (AirborneLateJoin()) { Settings.Height = 600; Settings.DownDuration = 4.f; Settings.GetUpDuration = 3.f; }
	if (!Spec || !RPGKnockback::SetGrantedSettings(*Spec, Settings)) { Fail(TEXT("AirborneGrantInvalid")); return false; }
	return ApplyStunProbeDamage(bPlayerSource, 0.f);
#else
	return false;
#endif
}
void URPGBossMapE2EProbeComponent::TickAirborneServer()
{
#if !UE_BUILD_SHIPPING
	auto* Player = LightHitPlayer.Get(); auto* NPC = LightHitNPC.Get();
	auto* Security = Player->FindComponentByClass<URPGSecurityValidationComponent>();
	if (Security->GetTotalViolationCount() != KnockbackViolationBaseline) { Fail(TEXT("AirborneMovementViolation")); return; }
	const bool bLateJoin = AirborneLateJoin();
	if (bLateJoin && IFileManager::Get().FileExists(*RPGAirborneJoinGate::File(TEXT("join.ready"))))
	{
		for (auto It=GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			auto* Observer=It->Get();
			if (Observer && Observer != Player->GetController() && !Observer->IsLocalController()
				&& Observer->GetNetConnection() && !ReadyClients.Contains(Observer)
				&& Observer->FindComponentByClass<URPGBossMapE2EProbeComponent>())
			{
				ReadyClients.Add(Observer);
				UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_AIRBORNE_JOIN_OBSERVER_REGISTERED Step=%d"), KnockbackStage);
			}
		}
	}
	const bool bConfirmed = KnockbackConfirmations.FindOrAdd(KnockbackStage).Num() == (bLateJoin ? ReadyClients.Num() : 2);
	auto* Subject = KnockbackStage >= 5 && KnockbackStage <= 8 ? NPC : Player;
	auto* Control = AirControl(Subject); const auto State = Control->GetAirborneState();
	const auto Advance = [this, Subject](int32 Step) { SendKnockbackProbeStep(Step, Subject); };
	if (bLateJoin && KnockbackStage >= 1 && KnockbackStage <= 7 && KnockbackStage != 4)
	{
		const double Now = GetWorld()->GetTimeSeconds();
		const int32 PhaseStep = KnockbackStage >= 5 ? KnockbackStage-4 : KnockbackStage;
		if (Now-StunDiagnosticAt > .1)
		{
			StunDiagnosticAt = Now;
			for (const auto& Client : ReadyClients)
				if (!KnockbackConfirmations.FindOrAdd(KnockbackStage).Contains(Client))
					Client->FindComponentByClass<URPGBossMapE2EProbeComponent>()->ClientObserveKnockback(Player, NPC, KnockbackStage,
						PhaseStep == 1 ? KnockbackProbeOrigin : Subject->GetActorLocation());
		}
		const FString Signal = RPGAirborneJoinGate::File(TEXT("join.ready"));
		if (Signal.IsEmpty()) { Fail(TEXT("AirborneJoinMissingRendezvous")); return; }
		const bool bReleased = IFileManager::Get().FileExists(*Signal);
		if (!bReleased && KnockbackStage == AirborneJoinStep() && Now-State.PhaseStartServerTime >= .15)
		{
			if (!FFileHelper::SaveStringToFile(TEXT("connect"), *Signal)) { Fail(TEXT("AirborneJoinSignalWriteFailed")); return; }
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_AIRBORNE_WAIT_LATE_JOIN Step=%d Id=%u Start=%.6f PhaseStart=%.6f ServerNow=%.6f ArcSeconds=%g GravityScale=%g"),
				KnockbackStage, State.ExecutionId, State.StartServerTime, State.PhaseStartServerTime, Now, State.ArcDuration,
				CastChecked<ACharacter>(Subject)->GetCharacterMovement()->GravityScale);
		}
		// A missed phase fails instead of applying a fresh hit or extending a timer.
		if (bReleased && !bConfirmed && static_cast<int32>(State.Phase) != PhaseStep)
		{ Fail(TEXT("AirborneLateJoinMissedPhase")); return; }
		if (bReleased && ReadyClients.Num() < 2 && static_cast<int32>(State.Phase) != PhaseStep)
		{ Fail(TEXT("AirborneLateJoinArrivedTooLate")); return; }
	}
	if (KnockbackStage == 0 && FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAirborneTerrain")))
	{
		auto* OwnerController = Cast<APlayerController>(Player->GetController());
		const double Now = GetWorld()->GetTimeSeconds();
		if (OwnerController && !KnockbackConfirmations.FindOrAdd(0).Contains(OwnerController) && Now-StunDiagnosticAt>.5)
		{
			StunDiagnosticAt=Now; OwnerController->ClientSetLocation(Player->GetActorLocation(), Player->GetActorRotation());
		}
	}
	if (KnockbackStage == 0 && bConfirmed)
	{
		if (bLateJoin && !IFileManager::Get().FileExists(*RPGAirborneJoinGate::File(TEXT("map.ready")))) return;
		if (!AirASC(Player)->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive()) return;
		if (!ApplyAirborneProbeDamage(false)) return;
		if (!AirControl(Player)->HasAirborneMotion() || !Security->IsMovementAuthorizationActive() || !bLightInputCancelled)
		{ Fail(TEXT("AirbornePlayerStartOrCancel")); return; }
		KnockbackProbeOrigin = Player->GetActorLocation();
		Advance(1); return;
	}
	if (!bConfirmed) return;
	if ((KnockbackStage == 1 || KnockbackStage == 5) && State.Phase == ERPGAirbornePhase::Downed)
	{
		if (!CastChecked<ACharacter>(Subject)->GetCharacterMovement()->IsMovingOnGround() || Control->HasAirborneMotion())
		{ Fail(TEXT("AirborneFakeLanding")); return; }
		if (Subject == Player && Security->IsMovementAuthorizationActive()) { Fail(TEXT("AirborneLeaseAfterLanding")); return; }
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAirborneTerrain")))
		{
			const auto* Movement = CastChecked<ACharacter>(Subject)->GetCharacterMovement();
			const auto* Floor = Movement->CurrentFloor.HitResult.GetActor();
			const bool Slope = KnockbackStage == 1;
			UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_AIRBORNE_TERRAIN_LANDING_CHECK Slope=%d Floor=%s Location=%s NormalZ=%g Flight=%g Arc=%g"),
				Slope,*GetNameSafe(Floor),*Subject->GetActorLocation().ToCompactString(),Movement->CurrentFloor.HitResult.ImpactNormal.Z,
				State.PhaseStartServerTime-State.StartServerTime,State.ArcDuration);
			if (!Floor || !Floor->GetName().StartsWith(Slope ? TEXT("RPG_AirborneTerrain_Ramp") : TEXT("RPG_AirborneTerrain_Floor"))
				|| (Slope && Movement->CurrentFloor.HitResult.ImpactNormal.Z >= .99f)
				|| (!Slope && State.PhaseStartServerTime <= State.StartServerTime + State.ArcDuration))
			{ Fail(TEXT("AirborneTerrainWrongLanding")); return; }
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_AIRBORNE_TERRAIN_LANDED Slope=%d NormalZ=%g FlightSeconds=%g ArcSeconds=%g"),
				Slope, Movement->CurrentFloor.HitResult.ImpactNormal.Z, State.PhaseStartServerTime-State.StartServerTime, State.ArcDuration);
		}
		Advance(KnockbackStage + 1); return;
	}
	if ((KnockbackStage == 2 || KnockbackStage == 6) && State.Phase == ERPGAirbornePhase::GettingUp) { Advance(KnockbackStage + 1); return; }
	if ((KnockbackStage == 3 || KnockbackStage == 7) && !State.IsActive())
	{
		if (State.EndReason != ERPGAirborneEndReason::Completed || RPGCombatControl::IsActionBlocked(Subject)) { Fail(TEXT("AirborneRecoveryIncomplete")); return; }
		Advance(KnockbackStage + 1); return;
	}
	if (KnockbackStage == 4)
	{
		if (bLateJoin)
		{
			// The initial fixture ran before the second gameplay controller existed.
			// Keep its newly spawned pawn out of the followup NPC flight lane too.
			for (const auto& Client : ReadyClients)
			{
				if (Client.Get() == Player->GetController()) continue;
				APawn* Observer = Client->GetPawn(); if (!Observer) return;
				auto* ObserverSecurity = Observer->FindComponentByClass<URPGSecurityValidationComponent>();
				if (!ObserverSecurity || ObserverSecurity->IsMovementAuthorizationActive()) return;
				const FVector Destination = Player->GetActorLocation() + Player->GetActorRightVector() * 700.f;
				const float Distance = FVector::Dist(Observer->GetActorLocation(), Destination);
				if (Distance < 5.f) continue;
				const auto Authorization = ObserverSecurity->BeginMovementAuthorization(.5f, Distance + 25.f, TEXT("AirborneLateJoinObserver"));
				if (!Authorization.IsValid()) { Fail(TEXT("AirborneJoinObserverFixtureAuthorization")); return; }
				Observer->SetActorLocation(Destination);
				Client->ClientSetLocation(Destination, Observer->GetActorRotation());
				ObserverSecurity->EndMovementAuthorization(Authorization, true);
				UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_AIRBORNE_OBSERVER_REPOSITIONED Location=%s"), *Observer->GetActorLocation().ToCompactString());
			}
		}
		if (!ApplyAirborneProbeDamage(true)) return;
		if (!AirControl(NPC)->HasAirborneMotion()) { Fail(TEXT("AirborneNPCStart")); return; }
		KnockbackProbeOrigin = NPC->GetActorLocation();
		SendKnockbackProbeStep(5, NPC); return;
	}
	if (KnockbackStage == 8)
	{
		auto* ASC = AirASC(Player); ASC->AddLooseGameplayTag(RPGAirborne::ImmunityTag());
		if (!ApplyAirborneProbeDamage(false)) return;
		ASC->RemoveLooseGameplayTag(RPGAirborne::ImmunityTag());
		if (AirControl(Player)->GetAirborneState().IsActive()) { Fail(TEXT("AirborneImmunityFailed")); return; }
		SendKnockbackProbeStep(9, Player); return;
	}
	if (KnockbackStage == 9)
	{
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_AIRBORNE_COMPLETE PlayerNPC=1 NPCPlayer=1 OwnerObserver=1 ActualLanding=1 DownGetUp=1 InputBlocked=1 Converged=1 Immunity=1 SecurityViolations=0 Clients=2"));
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAirborneTerrain")))
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_AIRBORNE_TERRAIN_COMPLETE WalkableSlope=1 LedgeLowerFloor=1 DelayedLanding=1 BothRoles=1 SecurityViolations=0 Clients=2"));
		if (bLateJoin)
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_AIRBORNE_LATE_JOIN_COMPLETE JoinStep=%d SameExecution=1 NaturalLanding=1 DownGetUp=1 SecurityViolations=0 Clients=2"), AirborneJoinStep());
		bFinished = true; CleanupLightHitInput();
	}
#endif
}
void URPGBossMapE2EProbeComponent::TickAirborneClient()
{
#if !UE_BUILD_SHIPPING
	auto* PC = Cast<APlayerController>(GetOwner()); auto* Player = LightHitPlayer.Get();
	if (!PC || !Player || !LightHitNPC.IsValid()) return;
	const bool bOwner = PC->GetPawn() == Player; const int32 Step = KnockbackExpected;
	PrepareAirborneJoinObservation(Player); PrepareAirborneJoinObservation(LightHitNPC.Get(), Step >= 5 && AirborneJoinStep() <= 3);
	if (AirborneLateJoin() && bOwner && Step == 0
		&& !IFileManager::Get().FileExists(*RPGAirborneJoinGate::File(TEXT("map.ready")))) return;
	const double Now = FPlatformTime::Seconds();
	if (Step == 0 && FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAirborneTerrain")))
	{
		FHitResult Floor; FCollisionQueryParams Query; Query.AddIgnoredActor(Player); Query.AddIgnoredActor(LightHitNPC.Get());
		const float HalfHeight = CastChecked<ACharacter>(Player)->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const bool Hit = GetWorld()->LineTraceSingleByChannel(Floor, Player->GetActorLocation(), Player->GetActorLocation()-FVector(0,0,HalfHeight+30), ECC_Visibility, Query);
		// Dynamic actor names can differ between peers. Verify the replicated surface geometrically.
		if (FVector::Dist(Player->GetActorLocation(), KnockbackExpectedLocation)>30.f || !Hit
			|| FMath::Abs(Floor.ImpactPoint.Z-(KnockbackExpectedLocation.Z-HalfHeight))>15.f)
		{
			if (Now-StunDiagnosticAt>1.)
			{
				StunDiagnosticAt=Now;
				UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_AIRBORNE_TERRAIN_WAIT Owner=%d Position=%s Expected=%s Hit=%d Floor=%s"),
					bOwner,*Player->GetActorLocation().ToCompactString(),*KnockbackExpectedLocation.ToCompactString(),Hit,*GetNameSafe(Floor.GetActor()));
			}
			return;
		}
	}
	const bool bPlayerControlStep = Step >= 1 && Step <= 3;
	if (bOwner && KnockbackInputStep != Step)
	{
		KnockbackInputStep = Step; KnockbackInputAt = Now;
		LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputAttackAction.Get());
		LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputMoveAction.Get());
		if (Step == 0 || bPlayerControlStep) LightInputSubsystem->StartContinuousInputInjectionForAction(LightInputAttackAction.Get(), FInputActionValue(true), {}, {});
		if (bPlayerControlStep) LightInputSubsystem->StartContinuousInputInjectionForAction(LightInputMoveAction.Get(), FInputActionValue(FVector2D(0, 1)), {}, {});
	}
	if (KnockbackConfirmed == Step) return;
	auto* Subject = Step >= 5 && Step <= 8 ? LightHitNPC.Get() : Player;
	auto* C = AirControl(Subject); auto* ASC = AirASC(Subject); const auto State = C->GetAirborneState();
	const int32 PhaseIndex = Step >= 5 && Step <= 8 ? Step - 4 : Step;
	if (AirborneLateJoin() && KnockbackConfirmed != Step && Now-StunDiagnosticAt > .25)
	{
		StunDiagnosticAt=Now;
		const auto* GameState=GetWorld()->GetGameState();
		const auto Motion=CastChecked<ACharacter>(Subject)->GetCharacterMovement()->GetRootMotionSource(C->AirborneMotionName);
		auto* Anim=C->AirborneAnimInstance.Get(); auto* Montage=C->AirborneMontage.Get();
		const auto* Movement=CastChecked<ACharacter>(Subject)->GetCharacterMovement();
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_AIRBORNE_JOIN_WAIT Step=%d Phase=%d Tag=%d Blocked=%d RMS=%d MotionTime=%g Pose=%d PoseTime=%g Clock=%.6f Start=%.6f PhaseStart=%.6f Z=%g ExpectedZ=%g MotionActive=%d Arc=%g MoveActive=%d Mode=%d Role=%d MotionName=%s MeshRootMotion=%d Sources=%d Pending=%d SameAnim=%d"),
			Step,int32(State.Phase),ASC->HasMatchingGameplayTag(RPGAirborne::SequenceTag()),RPGCombatControl::IsActionBlocked(Subject),
			C->HasAirborneMotion(),Motion.IsValid()?Motion->GetTime():-1.f,Montage!=nullptr,Anim&&Montage?Anim->Montage_GetPosition(Montage):-1.f,
			GameState?GameState->GetServerWorldTimeSeconds():0.,State.StartServerTime,State.PhaseStartServerTime,Subject->GetActorLocation().Z,KnockbackExpectedLocation.Z,
			State.bMotionActive,State.ArcDuration,Movement->IsActive(),int32(Movement->MovementMode),int32(Subject->GetLocalRole()),*C->AirborneMotionName.ToString(),
			CastChecked<ACharacter>(Subject)->GetMesh()->IsPlayingRootMotion(),Movement->CurrentRootMotion.RootMotionSources.Num(),Movement->CurrentRootMotion.PendingAddRootMotionSources.Num(),Anim==CastChecked<ACharacter>(Subject)->GetMesh()->GetAnimInstance());
	}
	const auto Expected = PhaseIndex == 1 ? ERPGAirbornePhase::Airborne : PhaseIndex == 2 ? ERPGAirbornePhase::Downed
		: PhaseIndex == 3 ? ERPGAirbornePhase::GettingUp : ERPGAirbornePhase::None;
	if (State.Phase != Expected || ASC->HasMatchingGameplayTag(RPGAirborne::SequenceTag()) != (Expected != ERPGAirbornePhase::None)) return;
	if (Step == 0 && bOwner && !AirASC(Player)->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive()) return;
	if (Expected != ERPGAirbornePhase::None)
	{
		const auto Tag = Expected == ERPGAirbornePhase::Airborne ? RPGAirborne::AirborneTag()
			: Expected == ERPGAirbornePhase::Downed ? RPGAirborne::DownedTag() : RPGAirborne::GettingUpTag();
		if (!ASC->HasMatchingGameplayTag(Tag) || !RPGCombatControl::IsActionBlocked(Subject)) return;
		if (Expected == ERPGAirbornePhase::Airborne && !C->HasAirborneMotion()) return;
		if (Expected == ERPGAirbornePhase::Airborne && Subject->GetActorLocation().Z < KnockbackExpectedLocation.Z + 60.f) return;
		if (Expected != ERPGAirbornePhase::Airborne && FVector::Dist2D(Subject->GetActorLocation(), KnockbackExpectedLocation) > 15.f)
			return; // Phase and CharacterMovement corrections can arrive in different packets.
		if (!C->AirborneMontage.IsValid()) return;
		if (AirborneLateJoin() && !bOwner && KnockbackConfirmed < 0)
		{
			if (Step != AirborneJoinStep()) { Fail(TEXT("AirborneLateJoinWrongInitialPhase")); return; }
			const auto* GameState = GetWorld()->GetGameState();
			const double ServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : 0.;
			const double Elapsed = ServerTime-State.PhaseStartServerTime;
			auto* Anim = C->AirborneAnimInstance.Get(); auto* Montage = C->AirborneMontage.Get();
			const auto* Instance = Anim && Montage ? Anim->GetActiveInstanceForMontage(Montage) : nullptr;
			if (!Instance || Instance->GetInstanceID() != C->AirborneMontageInstanceId || Elapsed < .05 || ServerTime >= State.PhaseEndServerTime) return;
			const auto* Profile = C->AirbornePresentationProfile.Get(); if (!Profile) return;
			const auto& Presentation = PhaseIndex == 1 ? Profile->Airborne : PhaseIndex == 2 ? Profile->Downed : Profile->GettingUp;
			const int32 Section = Presentation.Section.IsNone() ? 0 : Montage->GetSectionIndex(Presentation.Section);
			float Start = 0.f, End = 0.f; Montage->GetSectionStartAndEndTime(Section, Start, End);
			const float Length = End-Start; if (Length <= 0.f) return;
			const float Offset = PhaseIndex == 3 ? Elapsed*FMath::Clamp(Length/State.GetUpDuration,.1f,4.f) : FMath::Fmod(Elapsed,Length);
			float PoseError = FMath::Abs(Anim->Montage_GetPosition(Montage)-Start-Offset);
			if (PhaseIndex != 3) PoseError = FMath::Min(PoseError,FMath::Abs(Length-PoseError));
			if (PoseError > .5f) return;
			float MotionTime = -1.f;
			if (PhaseIndex == 1)
			{
				const auto Motion = CastChecked<ACharacter>(Subject)->GetCharacterMovement()->GetRootMotionSource(C->AirborneMotionName);
				if (!Motion.IsValid()) return;
				MotionTime = Motion->GetTime();
				if (FMath::Abs(MotionTime-(ServerTime-State.StartServerTime)) > .5) return;
			}
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_AIRBORNE_JOIN_SNAPSHOT Step=%d Id=%u Start=%.6f PhaseStart=%.6f ServerNow=%.6f Elapsed=%.6f PosePosition=%g PoseError=%g MotionTime=%g Location=%s"),
				Step, State.ExecutionId, State.StartServerTime, State.PhaseStartServerTime, ServerTime, Elapsed,
				Anim->Montage_GetPosition(Montage), PoseError, MotionTime, *Subject->GetActorLocation().ToCompactString());
		}
		if (bOwner && bPlayerControlStep)
		{
			if (Now - KnockbackInputAt < .3 || AirASC(Player)->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive() || !bLightInputCancelled) return;
			if (Expected != ERPGAirbornePhase::Airborne
				&& !CastChecked<ACharacter>(Subject)->GetCharacterMovement()->GetCurrentAcceleration().IsNearlyZero(.1f))
			{ Fail(TEXT("AirborneGroundedInputAcceleration")); return; }
			LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputAttackAction.Get());
			LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputMoveAction.Get());
		}
		if (bOwner && bPlayerControlStep)
		{
			FString Directory;
			if (FParse::Value(FCommandLine::Get(), TEXT("RPGLightHitCaptureDir="), Directory))
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory, FString::Printf(TEXT("Airborne-Phase%d.png"), PhaseIndex)), false, false);
		}
	}
	if (Step == 4 || Step == 8 || Step == 9)
	{
		if (C->HasAirborneMotion() || RPGCombatControl::IsActionBlocked(Subject)
			|| FVector::Dist(Subject->GetActorLocation(), KnockbackExpectedLocation) > 30.f) return;
	}
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_AIRBORNE_CLIENT Step=%d Phase=%d Owner=%d Id=%u Pose=%d Location=%s"),
		Step, static_cast<int32>(State.Phase), bOwner, State.ExecutionId, C->AirborneMontage.IsValid(), *Subject->GetActorLocation().ToCompactString());
	KnockbackConfirmed = Step; ServerConfirmKnockback(Step);
#endif
}
