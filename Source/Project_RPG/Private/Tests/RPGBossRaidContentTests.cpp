#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/RPGBossTestFixture.h"

#include "AbilitySystemComponent.h"
#include "Ability/RPGBossTelegraphedStrikeAbility.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossDefinition.h"
#include "AI/RPGBossThreatComponent.h"
#include "AI/BTTask_RotateToFaceTarget.h"
#include "AI/BTTask_RPGBossAbility.h"
#include "Attribute/RPGAttributeSet.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Controller/RPGAIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Sight.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossRaidContentTest, "ProjectRPG.Boss.RaidContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossRaidContentTest::RunTest(const FString& Parameters)
{
	UClass* RaidControllerClass = LoadClass<ARPGAIController>(nullptr,
		TEXT("/Game/Blueprints/Character/NPC/SwordMan/AIC_RaidSwordMan.AIC_RaidSwordMan_C"));
	if (!TestNotNull(TEXT("Saved raid controller"), RaidControllerClass)) { return false; }
	ARPGAIController* RaidCDO = RaidControllerClass->GetDefaultObject<ARPGAIController>();
	const UAIPerceptionComponent* Perception = Cast<UAIPerceptionComponent>(RaidCDO->GetDefaultSubobjectByName(TEXT("NPCPerceptionComponent")));
	const UAISenseConfig_Sight* Sight = Perception ? Perception->GetSenseConfig<UAISenseConfig_Sight>() : nullptr;
	if (!TestNotNull(TEXT("Saved raid sight configuration"), Sight)) { return false; }
	TestTrue(TEXT("Raid sight acquisition radius is positive and finite"), FMath::IsFinite(Sight->SightRadius) && Sight->SightRadius > 0.f);
	TestTrue(TEXT("Seen targets are not immediately lost by a zero/smaller radius"),
		FMath::IsFinite(Sight->LoseSightRadius) && Sight->LoseSightRadius >= Sight->SightRadius);
	// Execute the saved production BT and attack BPs on isolated GAS Pawns.
	// This covers content wiring without relying on a level, navigation mesh or HUD.
	UBehaviorTree* Tree = LoadObject<UBehaviorTree>(nullptr,
		TEXT("/Game/Blueprints/Character/NPC/SwordMan/BT_RaidSwordMan.BT_RaidSwordMan"));
	URPGBossDefinition* Definition = LoadObject<URPGBossDefinition>(nullptr,
		TEXT("/Game/Blueprints/Character/NPC/SwordMan/DA_RaidSwordMan.DA_RaidSwordMan"));
	UClass* StrikeClass = LoadClass<URPGBossTelegraphedStrikeAbility>(nullptr,
		TEXT("/Game/Blueprints/Character/NPC/SwordMan/GameplayAbility/GA_RaidSwordMan_Strike.GA_RaidSwordMan_Strike_C"));
	UClass* HeavyClass = LoadClass<URPGBossTelegraphedStrikeAbility>(nullptr,
		TEXT("/Game/Blueprints/Character/NPC/SwordMan/GameplayAbility/GA_RaidSwordMan_HeavyStrike.GA_RaidSwordMan_HeavyStrike_C"));
	if (!TestNotNull(TEXT("Saved raid BT"), Tree) || !TestNotNull(TEXT("Saved phases"), Definition) ||
		!TestNotNull(TEXT("Saved phase one attack"), StrikeClass) || !TestNotNull(TEXT("Saved phase two attack"), HeavyClass))
	{
		return false;
	}
	if (!TestEqual(TEXT("Authored raid still has two phases"), Definition->Phases.Num(), 2)) { return false; }
	TestTrue(TEXT("First phase selects strike through data"),
		Definition->Phases[0].AbilityPatterns.Num() == 1 && Definition->Phases[0].AbilityPatterns[0] == StrikeClass);
	TestTrue(TEXT("Second phase prioritizes heavy strike with standard strike fallback"),
		Definition->Phases[1].AbilityPatterns.Num() == 2 && Definition->Phases[1].AbilityPatterns[0] == HeavyClass &&
		Definition->Phases[1].AbilityPatterns[1] == StrikeClass);
	TestTrue(TEXT("Authored automatic enrage starts at 180 seconds with 1.5 attack"),
		FMath::IsNearlyEqual(Definition->EnrageSeconds, 180.f) && FMath::IsNearlyEqual(Definition->EnragedAttackMultiplier, 1.5f));
	const FGameplayTag StrikeCooldown = FGameplayTag::RequestGameplayTag(TEXT("Boss.Cooldown.SwordMan.Strike"));
	const FGameplayTag HeavyCooldown = FGameplayTag::RequestGameplayTag(TEXT("Boss.Cooldown.SwordMan.HeavyStrike"));
	const URPGBossTelegraphedStrikeAbility* StrikeDefaults = StrikeClass->GetDefaultObject<URPGBossTelegraphedStrikeAbility>();
	const URPGBossTelegraphedStrikeAbility* HeavyDefaults = HeavyClass->GetDefaultObject<URPGBossTelegraphedStrikeAbility>();
	TestTrue(TEXT("Authored strike has its independent four second cooldown"),
		FMath::IsNearlyEqual(StrikeDefaults->PatternCooldownSeconds, 4.f) && StrikeDefaults->PatternCooldownTag == StrikeCooldown);
	TestTrue(TEXT("Authored heavy has its independent eight second cooldown"),
		FMath::IsNearlyEqual(HeavyDefaults->PatternCooldownSeconds, 8.f) && HeavyDefaults->PatternCooldownTag == HeavyCooldown);
	TestFalse(TEXT("Standard strike remains single-target"), StrikeClass->GetDefaultObject<URPGBossTelegraphedStrikeAbility>()->bHitAllThreatParticipants);
	TestTrue(TEXT("Heavy strike damages area participants"), HeavyClass->GetDefaultObject<URPGBossTelegraphedStrikeAbility>()->bHitAllThreatParticipants);
	int32 PhaseTasks = 0;
	TFunction<void(const UBTCompositeNode*)> VerifyTasks = [&](const UBTCompositeNode* Composite)
	{
		if (!Composite) { return; }
		for (const FBTCompositeChild& Child : Composite->Children)
		{
			if (const UBTTask_RPGBossAbility* Task = Cast<UBTTask_RPGBossAbility>(Child.ChildTask))
			{
				TestTrue(TEXT("Authored task selects frozen phase patterns"), Task->bUsePhasePatterns);
				TestNull(TEXT("Authored task has no hidden explicit-class fallback"), Task->AbilityClass.Get());
				++PhaseTasks;
			}
			VerifyTasks(Child.ChildComposite);
		}
	};
	VerifyTasks(Tree->RootNode);
	TestEqual(TEXT("Both authored attack tasks migrated without rebuilding the tree"), PhaseTasks, 2);
	for (UClass* AttackClass : { StrikeClass, HeavyClass })
	{
		const URPGBossTelegraphedStrikeAbility* Attack = AttackClass->GetDefaultObject<URPGBossTelegraphedStrikeAbility>();
		const UAnimMontage* Montage = Attack->PresentationMontage;
		if (!TestNotNull(TEXT("Raid strike has a cosmetic montage"), Montage)) { return false; }
		TestFalse(TEXT("Raid montage cannot drive root motion"), Montage->HasRootMotion());
		TestEqual(TEXT("Raid montage has no gameplay notifies"), Montage->Notifies.Num(), 0);
		const UAnimSequence* Sequence = Cast<UAnimSequence>(Montage->GetFirstAnimReference());
		if (!TestNotNull(TEXT("Raid montage has a scoped sequence"), Sequence)) { return false; }
		TestFalse(TEXT("Raid sequence root motion disabled"), Sequence->HasRootMotion());
		TestTrue(TEXT("Raid sequence locks its root"), Sequence->bForceRootLock);
		TestEqual(TEXT("Raid sequence cannot trigger original weapon/warp notifies"), Sequence->Notifies.Num(), 0);
		TestTrue(TEXT("Raid montage play rate is finite"), FMath::IsFinite(Attack->PresentationPlayRate) && Attack->PresentationPlayRate > 0.f);
		TestEqual(TEXT("Raid windup uses native cosmetic Cue"), Attack->WindupCueTag,
			FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Boss.AttackWarning")));
		TestTrue(TEXT("Raid recovery configured"), FMath::IsNearlyEqual(Attack->RecoverySeconds, .35f));
	}
	UWorld::InitializationValues Initialization;
	Initialization.EnableTraceCollision(true);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage(), true,
		ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("Raid content world"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	World->InitializeActorsForPlay(FURL());
	World->CreateAISystem();
	World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();
	ARPGBossTestPawn* Boss = World->SpawnActor<ARPGBossTestPawn>();
	ARPGBossTestPawn* Target = World->SpawnActor<ARPGBossTestPawn>();
	ARPGAIController* Controller = World->SpawnActor<ARPGAIController>();
	AAIController* TargetController = World->SpawnActor<AAIController>();
	if (!TestNotNull(TEXT("Boss Pawn"), Boss) || !TestNotNull(TEXT("Target Pawn"), Target) ||
		!TestNotNull(TEXT("Boss controller"), Controller) || !TestNotNull(TEXT("Target controller"), TargetController))
	{
		return false;
	}
	// MoveTo needs a navigation movement interface even when already in reach.
	UFloatingPawnMovement* Movement = NewObject<UFloatingPawnMovement>(Boss);
	Movement->RegisterComponent();
	Movement->SetUpdatedComponent(Boss->GetRootComponent());
	Controller->Possess(Boss);
	Controller->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
	TargetController->SetGenericTeamId(FGenericTeamId(0));
	TargetController->Possess(Target);
	Target->SetActorLocation(FVector(100.f, 0.f, 0.f));
	Boss->ASC->SetNumericAttributeBase(URPGAttributeSet::GetCriticalChanceAttribute(), 0.f);
	Target->ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 1000.f);
	Target->SetTestHealth(1000.f);
	ARPGBossTestPawn* NearbyParticipant = World->SpawnActor<ARPGBossTestPawn>();
	AAIController* NearbyController = World->SpawnActor<AAIController>();
	if (!TestNotNull(TEXT("Nearby participant"), NearbyParticipant) || !TestNotNull(TEXT("Nearby controller"), NearbyController)) { return false; }
	NearbyController->SetGenericTeamId(FGenericTeamId(0));
	NearbyController->Possess(NearbyParticipant);
	NearbyParticipant->SetActorLocation(FVector(100.f, 80.f, 0.f));
	NearbyParticipant->ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 1000.f);
	NearbyParticipant->SetTestHealth(1000.f);
	Boss->Boss->Definition = Definition;
	URPGBossThreatComponent* Threat = NewObject<URPGBossThreatComponent>(Boss);
	Threat->RegisterComponent();
	const FGameplayAbilitySpecHandle StrikeHandle = Boss->ASC->GiveAbility(FGameplayAbilitySpec(StrikeClass, 1));
	const FGameplayAbilitySpecHandle HeavyHandle = Boss->ASC->GiveAbility(FGameplayAbilitySpec(HeavyClass, 1));
	if (!TestTrue(TEXT("Saved BT starts"), Controller->RunBehaviorTree(Tree))) { return false; }
	UBehaviorTreeComponent* BT = Cast<UBehaviorTreeComponent>(Controller->BrainComponent);
	if (!TestNotNull(TEXT("Running behavior component"), BT)) { return false; }
	BT->SetComponentTickEnabled(false);
	uint64 SimulatedFrame = GFrameCounter;
	auto Step = [&]()
	{
		// TimerManager suppresses repeated ticks in the same engine frame. Scope
		// a distinct frame to this isolated world and restore the caller's counter.
		TGuardValue<uint64> FrameGuard(GFrameCounter, ++SimulatedFrame);
		World->Tick(LEVELTICK_All, .05f);
		Threat->RefreshTargets();
		BT->TickComponent(.05f, LEVELTICK_All, nullptr);
	};
	auto Advance = [&](float Seconds)
	{
		for (int32 Index = 0; Index < FMath::CeilToInt(Seconds / .05f); ++Index) { Step(); }
	};
	auto AwaitTelegraph = [&](const TCHAR* TagName)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TagName));
		for (int32 Index = 0; Index < 120; ++Index)
		{
			Step();
			const FRPGBossTelegraphState State = Boss->Boss->GetTelegraphState();
			if (State.bActive && State.ActionTag == Tag) { return true; }
		}
		return false;
	};
	Advance(.6f);
	TestFalse(TEXT("Idle tree cannot start an attack"), Boss->Boss->GetTelegraphState().bActive);
	TestEqual(TEXT("Idle leaves target health unchanged"), Target->Attributes->GetCurrentHealth(), 1000.f);
	TestTrue(TEXT("Encounter starts using saved phase data"), Boss->Boss->StartEncounter());
	TestTrue(TEXT("Authored enrage deadline is scheduled without immediate buff"),
		FMath::IsNearlyEqual(Boss->Boss->GetBossState().EnrageServerTime - World->GetTimeSeconds(), 180.0, .01) &&
		!Boss->Boss->GetBossState().bEnraged);
	TestTrue(TEXT("Threat admits target"), Threat->AddThreat(Target, 10.f, Boss->Boss->GetBossState().EncounterId));
	TestTrue(TEXT("Threat admits nearby participant"), Threat->AddThreat(NearbyParticipant, 1.f, Boss->Boss->GetBossState().EncounterId));
	for (const FGameplayAbilitySpecHandle AttackHandle : { StrikeHandle, HeavyHandle })
	{
		FGameplayAbilitySpec* Spec = Boss->ASC->FindAbilitySpecFromHandle(AttackHandle);
		UGameplayAbility* Instance = Spec ? Spec->GetPrimaryInstance() : nullptr;
		if (!TestNotNull(TEXT("Saved attack instance"), Instance)) { return false; }
		const float Reach = CastChecked<URPGBossTelegraphedStrikeAbility>(Instance)->SecurityProfile.MaximumServerHitDistance;
		FVector BoundsOrigin, BoundsExtent;
		Target->GetActorBounds(true, BoundsOrigin, BoundsExtent, false);
		Target->SetActorLocation(Boss->GetActorLocation() + FVector(Reach + BoundsExtent.Size() + 100.f, 0.f, 0.f));
		TestTrue(TEXT("Far target remains eligible for chase"), Threat->GetCurrentTarget() == Target);
		const int32 PreviousSequence = Boss->Boss->GetTelegraphState().Sequence;
		TestFalse(TEXT("Out-of-reach saved attack cannot activate"), Boss->ASC->TryActivateAbility(AttackHandle));
		TestFalse(TEXT("Rejected attack cannot publish a warning"), Boss->Boss->GetTelegraphState().bActive);
		TestEqual(TEXT("Rejected attack cannot consume a telegraph sequence"), Boss->Boss->GetTelegraphState().Sequence, PreviousSequence);
		TestFalse(TEXT("Rejected attack remains inactive"), Spec->IsActive());
		TestFalse(TEXT("Out-of-reach attack cannot consume its configured cooldown"),
			Boss->ASC->HasMatchingGameplayTag(CastChecked<URPGBossTelegraphedStrikeAbility>(Instance)->PatternCooldownTag));
		Target->SetActorLocation(Boss->GetActorLocation() + FVector(Reach, 0.f, 0.f));
		TestTrue(TEXT("Authored reach remains allowed"), Instance->CanActivateAbility(AttackHandle, Boss->ASC->AbilityActorInfo.Get()));
		Target->SetActorLocation(Boss->GetActorLocation() + FVector(Reach + BoundsExtent.Size() * .5, 0.f, 0.f));
		TestTrue(TEXT("Damage validator bounds allowance is preserved"), Instance->CanActivateAbility(AttackHandle, Boss->ASC->AbilityActorInfo.Get()));
	}
	Target->SetActorLocation(FVector(100.f, 0.f, 0.f));
	if (!TestTrue(TEXT("Phase one BT chooses standard strike"), AwaitTelegraph(TEXT("Boss.Action.SwordMan.Strike"))))
	{
		AddInfo(BT->GetDebugInfoString());
		AddInfo(Controller->GetBlackboardComponent()->GetDebugInfoString(EBlackboardDescription::Full));
		return false;
	}
	Advance(.8f);
	TestTrue(TEXT("Natural strike commits the authored GAS cooldown"), Boss->ASC->HasMatchingGameplayTag(StrikeCooldown));
	TestEqual(TEXT("Windup cannot deal early damage"), Target->Attributes->GetCurrentHealth(), 1000.f);
	Advance(.6f);
	TestEqual(TEXT("Saved standard strike applies shared damage GE once"), Target->Attributes->GetCurrentHealth(), 980.f);
	TestEqual(TEXT("Standard strike does not hit the nearby participant"), NearbyParticipant->Attributes->GetCurrentHealth(), 1000.f);
	if (!TestTrue(TEXT("Next standard attack begins"), AwaitTelegraph(TEXT("Boss.Action.SwordMan.Strike")))) { return false; }
	Boss->SetTestHealth(69.f);
	TestEqual(TEXT("Saved threshold enters phase two"), Boss->Boss->GetBossState().PhaseIndex, 1);
	TestFalse(TEXT("Phase transition cancels the old windup"), Boss->Boss->GetTelegraphState().bActive);
	if (!TestTrue(TEXT("Phase two BT chooses heavy strike"), AwaitTelegraph(TEXT("Boss.Action.SwordMan.HeavyStrike")))) { return false; }
	Advance(1.8f);
	TestTrue(TEXT("Natural heavy commits the authored GAS cooldown"), Boss->ASC->HasMatchingGameplayTag(HeavyCooldown));
	TestEqual(TEXT("Saved heavy strike applies its own damage"), Target->Attributes->GetCurrentHealth(), 945.f);
	TestEqual(TEXT("Saved heavy strike also hits the non-aggro participant"), NearbyParticipant->Attributes->GetCurrentHealth(), 965.f);
	if (!TestTrue(TEXT("Heavy cooldown selects a natural standard strike fallback"), AwaitTelegraph(TEXT("Boss.Action.SwordMan.Strike")))) { return false; }
	TestTrue(TEXT("Heavy remains on cooldown while fallback executes"), Boss->ASC->HasMatchingGameplayTag(HeavyCooldown));
	Advance(1.4f);
	TestEqual(TEXT("Phase two fallback keeps standard strike damage"), Target->Attributes->GetCurrentHealth(), 925.f);
	TestEqual(TEXT("Phase two fallback remains single-target"), NearbyParticipant->Attributes->GetCurrentHealth(), 965.f);
	if (!TestTrue(TEXT("Next heavy attack begins"), AwaitTelegraph(TEXT("Boss.Action.SwordMan.HeavyStrike")))) { return false; }
	TestTrue(TEXT("Server wipes encounter"), Boss->Boss->MarkWiped());
	TestFalse(TEXT("Wipe clears the active telegraph"), Boss->Boss->GetTelegraphState().bActive);
	Advance(2.f);
	TestEqual(TEXT("Wipe prevents delayed and repeated attacks"), Target->Attributes->GetCurrentHealth(), 925.f);
	TestTrue(TEXT("Reset begins"), Boss->Boss->BeginReset());
	Boss->SetTestHealth(100.f);
	TestTrue(TEXT("Reset completes"), Boss->Boss->CompleteReset());
	TestTrue(TEXT("Second encounter starts"), Boss->Boss->StartEncounter());
	Threat->AddThreat(Target, 10.f, Boss->Boss->GetBossState().EncounterId);
	if (!TestTrue(TEXT("New encounter returns to standard strike"), AwaitTelegraph(TEXT("Boss.Action.SwordMan.Strike")))) { return false; }
	TargetController->UnPossess();
	Advance(2.f);
	TestFalse(TEXT("Target loss cancels the active telegraph"), Boss->Boss->GetTelegraphState().bActive);
	TestEqual(TEXT("Target loss prevents stale damage"), Target->Attributes->GetCurrentHealth(), 925.f);
	BT->StopTree(EBTStopMode::Forced);
	// Regression: a destroyed target between rotation ticks must fail and return,
	// not fall through to dereferencing an invalid weak pointer.
	UBTTask_RotateToFaceTarget* Rotate = NewObject<UBTTask_RotateToFaceTarget>(Tree);
	FRotateToFaceTargetTaskMemory RotationMemory;
	RotationMemory.OwningPawn = Boss;
	RotationMemory.TargetActor = Target;
	Target->Destroy();
	Rotate->TickTask(*BT, reinterpret_cast<uint8*>(&RotationMemory), .05f);
	TestFalse(TEXT("Lost rotation target clears task memory"), RotationMemory.OwningPawn.IsValid());
	return true;
}

#endif
