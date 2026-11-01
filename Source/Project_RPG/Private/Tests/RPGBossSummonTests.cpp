#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/RPGBossStartupTestFixture.h"
#include "Tests/RPGBossTestFixture.h"
#include "Ability/RPGBossSummonAbility.h"
#include "AI/RPGBossThreatComponent.h"
#include "AI/RPGBossDefinition.h"
#include "AI/RPGBossSummonObjectiveComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BrainComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Controller/RPGAIController.h"
#include "AIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "RPGGameplayTags.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BossSummonTestAction, "Tests.RaidBoss.Summon.Action");

class FRPGBossSummonCommand : public IAutomationLatentCommand
{
public:
	explicit FRPGBossSummonCommand(FAutomationTestBase* InTest) : Test(InTest) {}
	virtual ~FRPGBossSummonCommand() override
	{
		if (World)
		{
			World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	}
	virtual bool Update() override
	{
		if (!World)
		{
			UWorld::InitializationValues Initialization;
			Initialization.EnableTraceCollision(true);
			World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()),
				GetTransientPackage(), true, ERHIFeatureLevel::Num, &Initialization);
			if (!Test->TestNotNull(TEXT("Summon world"), World)) { return true; }
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL()); World->CreateAISystem();
			World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
			Source = World->SpawnActor<ARPGBossStartupTestCharacter>();
			if (!Test->TestNotNull(TEXT("Summon source"), Source)) { return true; }
			Source->SpawnDefaultController(); Deadline = FPlatformTime::Seconds() + 15.0;
			return false;
		}
		if (Stage == 0)
		{
			if (!Source->IsNPCStartUpDataReady())
			{
				if (FPlatformTime::Seconds() < Deadline) { return false; }
				Test->AddError(TEXT("Summon source startup timed out")); return true;
			}
			if (!StartCases()) { return true; }
			Stage = 1; Deadline = FPlatformTime::Seconds() + 15.0;
			return false;
		}
		if (Stage >= 2) { return UpdateMechanicCases(); }
		Advance(.02f); // Yield to the real streamable manager between latent updates.
		const TArray<FRPGBossSummonRecord> Records = Registry->GetSummons();
		if (Records.Num() != 2) { Test->AddError(TEXT("Summon wave disappeared before readiness")); return true; }
		if (!CombatSummon->IsNPCStartUpDataReady() ||
			Records.ContainsByPredicate([](const FRPGBossSummonRecord& Record) { return !Record.bInitialized; }))
		{
			if (FPlatformTime::Seconds() < Deadline) { return false; }
			Test->AddError(TEXT("Summoned NPC startup timed out")); return true;
		}
		FinishCases(Records);
		// Reuse the same latent test/world; the earlier lifecycle cases leave
		// their source terminally dead, so the mechanic needs a fresh boss.
		Source = World->SpawnActor<ARPGBossStartupTestCharacter>();
		if (!Test->TestNotNull(TEXT("Mechanic source"), Source)) { return true; }
		Source->SpawnDefaultController();
		Stage = 2; Deadline = FPlatformTime::Seconds() + 15.0;
		return false;
	}
private:
	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	ARPGBossStartupTestCharacter* Source = nullptr;
	ARPGBossTestPawn* Target = nullptr;
	AAIController* SourceAI = nullptr;
	UAbilitySystemComponent* ASC = nullptr;
	URPGBossSummonComponent* Registry = nullptr;
	URPGBossSummonAbility* Ability = nullptr;
	FGameplayAbilitySpecHandle Handle;
	ARPGNonPlayerCharacter* CombatSummon = nullptr;
	ARPGAIController* CombatAI = nullptr;
	UBlackboardComponent* CombatBlackboard = nullptr;
	TStrongObjectPtr<UDataAsset_StartUpDataBase> CombatData;
	int32 Stage = 0;
	uint64 Frame = 0;
	double Deadline = 0.0;
	bool StartMechanicCases()
	{
		SourceAI = Cast<AAIController>(Source->GetController());
		if (!Test->TestNotNull(TEXT("Mechanic controller"), SourceAI)) { return false; }
		SourceAI->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
		ASC = Source->GetAbilitySystemComponent(); Registry = Source->BossSummonComponent;
		URPGBossDefinition* Saved = LoadObject<URPGBossDefinition>(nullptr,
			TEXT("/Game/Blueprints/Character/NPC/Raid/SwordManSummonMechanic/DA_RaidSwordManSummonMechanic.DA_RaidSwordManSummonMechanic"));
		UBehaviorTree* Tree = LoadObject<UBehaviorTree>(nullptr,
			TEXT("/Game/Blueprints/Character/NPC/Raid/SwordManSummonMechanic/BT_RaidSwordManSummonMechanic.BT_RaidSwordManSummonMechanic"));
		if (!Test->TestNotNull(TEXT("Saved mechanic definition"), Saved) || !Test->TestNotNull(TEXT("Saved mechanic BT"), Tree) ||
			!Test->TestEqual(TEXT("Saved mechanic has three phases"), Saved->Phases.Num(), 3)) { return false; }
		Source->BossAIComponent->Definition = DuplicateObject<URPGBossDefinition>(Saved, Source);
		const TSubclassOf<URPGBossGameplayAbility> EntryClass = Saved->Phases[1].PhaseEntryAbility;
		if (!Test->TestNotNull(TEXT("Saved phase entry class"), EntryClass.Get())) { return false; }
		Handle = ASC->GiveAbility(FGameplayAbilitySpec(EntryClass, 1));
		const FGameplayAbilitySpec* EntrySpec = ASC->FindAbilitySpecFromHandle(Handle);
		Ability = EntrySpec ? Cast<URPGBossSummonAbility>(EntrySpec->GetPrimaryInstance()) : nullptr;
		if (!Test->TestNotNull(TEXT("Saved phase entry summon instance"), Ability)) { return false; }
		// Keep the saved phase/entry/objective wiring, but use the existing native
		// startup fixture and no navmesh in this isolated authority world. This
		// is not a proof of authored Guardian navigation or multiplayer combat.
		Ability->SummonClass = ARPGBossStartupTestCharacter::StaticClass();
		Ability->bRequireNavigation = false; Ability->WindupSeconds = .1f;
		Test->TestFalse(TEXT("Saved mechanic summon is target independent"), Ability->bRequireThreatTarget);
		Source->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		// Disabling sight does not erase already queued/cached perceptions. Make
		// the sole hostile fixture temporarily friendly so rescans cannot silently
		// reintroduce a target during the target-independent activation check.
		AAIController* TargetAI = CastChecked<AAIController>(Target->GetController());
		TargetAI->SetGenericTeamId(SourceAI->GetGenericTeamId());
		SourceAI->GetPerceptionComponent()->ForgetAll();
		if (!Test->TestTrue(TEXT("Mechanic BT starts"), SourceAI->RunBehaviorTree(Tree)) ||
			!Test->TestTrue(TEXT("Mechanic encounter starts"), Source->BossAIComponent->StartEncounter())) { return false; }
		Source->BossThreatComponent->ClearThreat();
		ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 69.f);
		Test->TestEqual(TEXT("HP enters mechanic phase"), Source->BossAIComponent->GetBossState().PhaseIndex, 1);
		Test->TestTrue(TEXT("Mechanic grants GAS invincibility"), ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Invincible));
		Test->TestFalse(TEXT("Mechanic stops normal BT"), SourceAI->GetBrainComponent()->IsRunning());
		Test->TestTrue(TEXT("Mechanic disables movement"), Source->GetCharacterMovement()->MovementMode == MOVE_None);
		Advance(.04f);
		Test->TestTrue(TEXT("Phase entry casts without a threat target"), Ability->IsActive() && Ability->IsBossExecutionValid());
		Test->TestNull(TEXT("Target-independent cast does not capture a target"), Ability->GetBossExecutionTarget());
		Test->TestNull(TEXT("Activation scenario really has no threat target"), Source->BossThreatComponent->GetCurrentTarget());
		TargetAI->SetGenericTeamId(FGenericTeamId(0));
		Source->BossThreatComponent->AddThreat(Target, 20.f, Source->BossAIComponent->GetBossState().EncounterId);
		Source->BossThreatComponent->RefreshTargets();
		Test->TestTrue(TEXT("Target-independent windup survives new aggro"), Ability->IsBossExecutionValid() && !Ability->GetBossExecutionTarget());
		Source->BossThreatComponent->ClearThreat();
		return true;
	}
	bool UpdateMechanicCases()
	{
		Advance(.02f);
		if (Stage == 2)
		{
			if (!Source->IsNPCStartUpDataReady())
			{
				if (FPlatformTime::Seconds() < Deadline) { return false; }
				Test->AddError(TEXT("Mechanic source startup timed out")); return true;
			}
			if (!StartMechanicCases()) { return true; }
			Stage = 3; Deadline = FPlatformTime::Seconds() + 15.0;
			return false;
		}
		const TArray<FRPGBossSummonRecord> Records = Registry->GetSummons();
		if (Records.Num() != 2 || Records.ContainsByPredicate([](const FRPGBossSummonRecord& Record) { return !Record.bInitialized; }))
		{
			if (FPlatformTime::Seconds() < Deadline && Source->BossAIComponent->GetBossState().EncounterState == ERPGBossEncounterState::InCombat) { return false; }
			Test->AddError(TEXT("Mechanic phase entry wave did not become ready")); return true;
		}
		URPGBossSummonObjectiveComponent* Objective = Source->BossSummonObjectiveComponent;
		Test->TestEqual(TEXT("Wave arms objective with two members"), Objective->GetObjectiveState().RequiredCount, 2);
		if (Stage == 3)
		{
			Records[0].Pawn->GetAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
			Test->TestEqual(TEXT("First real defeat counts once"), Objective->GetObjectiveState().DefeatedCount, 1);
			Advance(.12f);
			Test->TestEqual(TEXT("Polling dead slot does not double-count"), Objective->GetObjectiveState().DefeatedCount, 1);
			Test->TestEqual(TEXT("Partial defeat cannot advance phase"), Source->BossAIComponent->GetBossState().PhaseIndex, 1);
			Records[1].Pawn->GetAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
			Advance(.06f);
			Test->TestEqual(TEXT("All defeats advance to explicit combat phase"), Source->BossAIComponent->GetBossState().PhaseIndex, 2);
			Test->TestFalse(TEXT("Successful mechanic removes owned invincibility"), ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Invincible));
			Test->TestTrue(TEXT("Successful mechanic restarts stopped BT"), SourceAI->GetBrainComponent()->IsRunning());
			Test->TestTrue(TEXT("Successful mechanic restores saved movement"), Source->GetCharacterMovement()->MovementMode == MOVE_Flying);
			Test->TestFalse(TEXT("Completed objective is inactive"), Objective->GetObjectiveState().bActive);
			CheckRemoved(Records, TEXT("Successful phase transition retires its summons"));
			const int32 PreviousEncounter = Source->BossAIComponent->GetBossState().EncounterId;
			if (!Test->TestTrue(TEXT("Mechanic can begin reset"), Source->BossAIComponent->BeginReset())) { return true; }
			ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100.f);
			if (!Test->TestTrue(TEXT("Mechanic reset completes"), Source->BossAIComponent->CompleteReset()) ||
				!Test->TestTrue(TEXT("Mechanic encounter restarts"), Source->BossAIComponent->StartEncounter())) { return true; }
			Test->TestTrue(TEXT("Restart has a new encounter identity"), Source->BossAIComponent->GetBossState().EncounterId > PreviousEncounter);
			ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 69.f);
			Test->TestEqual(TEXT("Restarted objective has no old kills"), Objective->GetObjectiveState().DefeatedCount, 0);
			Stage = 4; Deadline = FPlatformTime::Seconds() + 15.0;
			return false;
		}
		Records[0].Pawn->Destroy();
		Advance(.12f);
		Test->TestEqual(TEXT("Live despawn is not a defeat"), Objective->GetObjectiveState().DefeatedCount, 0);
		Test->TestEqual(TEXT("Live despawn cannot bypass the mechanic"), Source->BossAIComponent->GetBossState().PhaseIndex, 1);
		Advance(30.1f);
		const FRPGBossState Expired = Source->BossAIComponent->GetBossState();
		Test->TestTrue(TEXT("Uncompleted mechanic times out to Wiped"), Expired.EncounterState == ERPGBossEncounterState::Wiped && Expired.bPhaseTimedOut);
		Test->TestFalse(TEXT("Timeout clears objective"), Objective->GetObjectiveState().bActive);
		Test->TestFalse(TEXT("Timeout clears owned invincibility"), ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Invincible));
		Test->TestFalse(TEXT("Timeout leaves combat BT stopped"), SourceAI->GetBrainComponent()->IsRunning());
		CheckRemoved(Records, TEXT("Timeout retires surviving summons"));
		return true;
	}
	void Advance(float Seconds)
	{
		for (int32 Index = 0; Index < FMath::CeilToInt(Seconds / .02f); ++Index)
		{
			Frame = FMath::Max(Frame, GFrameCounter) + 1;
			TGuardValue<uint64> FrameGuard(GFrameCounter, Frame);
			World->Tick(LEVELTICK_All, .02f); Source->BossThreatComponent->RefreshTargets();
		}
	}
	void Restore()
	{
		ASC->CancelAbilityHandle(Handle);
		Source->BossAIComponent->BeginReset(); Source->BossAIComponent->CompleteReset();
		ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100.f);
		Source->BossAIComponent->StartEncounter();
		Source->BossThreatComponent->ClearThreat();
		Source->BossThreatComponent->AddThreat(Target, 20.f, Source->BossAIComponent->GetBossState().EncounterId);
		Source->BossThreatComponent->RefreshTargets();
	}
	TArray<FRPGBossSummonRecord> CastWave()
	{
		Test->TestTrue(TEXT("Summon cast starts"), ASC->TryActivateAbility(Handle));
		Advance(.16f);
		const auto Records = Registry->GetSummons();
		Test->TestEqual(TEXT("Wave contains two NPCs"), Records.Num(), 2);
		return Records;
	}
	void CheckRemoved(const TArray<FRPGBossSummonRecord>& Records, const TCHAR* Label)
	{
		Test->TestTrue(Label, Registry->GetSummons().IsEmpty());
		for (const auto& Record : Records)
		{
			Test->TestTrue(TEXT("Owned pawn destroyed"), !IsValid(Record.Pawn) || Record.Pawn->IsActorBeingDestroyed());
			Test->TestTrue(TEXT("Owned AI destroyed"), !Record.Controller.IsValid() || Record.Controller->IsActorBeingDestroyed());
		}
	}
	bool StartCases()
	{
		SourceAI = Cast<AAIController>(Source->GetController());
		if (!Test->TestNotNull(TEXT("Summon source controller"), SourceAI)) { return false; }
		SourceAI->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
		ASC = Source->GetAbilitySystemComponent(); Registry = Source->BossSummonComponent;
		Target = World->SpawnActor<ARPGBossTestPawn>();
		AAIController* TargetAI = World->SpawnActor<AAIController>();
		TargetAI->SetGenericTeamId(FGenericTeamId(0)); TargetAI->Possess(Target);
		Target->SetActorLocation(FVector(600.f, 0.f, 0.f)); Target->SetTestHealth(1000.f);
		// A plain NPC exercises the production controller path without a new fixture class.
		CombatData.Reset(NewObject<UDataAsset_StartUpDataBase>(Source));
		CombatSummon = World->SpawnActorDeferred<ARPGNonPlayerCharacter>(ARPGNonPlayerCharacter::StaticClass(),
			FTransform(FVector(0.f, 400.f, 0.f)), Source, Source, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Test->TestNotNull(TEXT("Ordinary encounter NPC"), CombatSummon)) { return false; }
		FSoftObjectProperty* StartupProperty = FindFProperty<FSoftObjectProperty>(ARPGNonPlayerCharacter::StaticClass(), TEXT("CharacterStartUpData"));
		if (!Test->TestNotNull(TEXT("Existing NPC startup property"), StartupProperty)) { return false; }
		StartupProperty->SetObjectPropertyValue_InContainer(CombatSummon, CombatData.Get());
		CombatSummon->AIControllerClass = ARPGAIController::StaticClass();
		CombatSummon->ConfigureEncounterSummon(SourceAI->GetGenericTeamId().GetId());
		Test->TestNull(TEXT("Deferred summon has no controller yet"), CombatSummon->GetController());
		Test->TestEqual(TEXT("Summon exposes its configured team without a controller"),
			CombatSummon->GetGenericTeamId().GetId(), SourceAI->GetGenericTeamId().GetId());
		const FProperty* TeamProperty = FindFProperty<FProperty>(ARPGNonPlayerCharacter::StaticClass(), TEXT("EncounterSummonTeam"));
		Test->TestTrue(TEXT("Encounter team is a replicated Pawn property"), TeamProperty && TeamProperty->HasAnyPropertyFlags(CPF_Net));
		CombatSummon->SetActorEnableCollision(false);
		CombatSummon->FinishSpawning(FTransform(FVector(0.f, 400.f, 0.f)));
		CombatAI = Cast<ARPGAIController>(CombatSummon->GetController());
		if (!Test->TestNotNull(TEXT("Ordinary summon controller"), CombatAI)) { return false; }
		CombatSummon->SetOwner(Source); // Possession normally assigns ownership to the controller.
		CombatAI->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
		CombatSummon->GetAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 100.f);
		CombatSummon->GetAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100.f);
		UBlackboardData* Data = NewObject<UBlackboardData>(CombatAI);
		FBlackboardEntry TargetKey;
		TargetKey.EntryName = TEXT("TargetActor");
		UBlackboardKeyType_Object* ObjectKey = NewObject<UBlackboardKeyType_Object>(Data);
		ObjectKey->BaseClass = APawn::StaticClass(); TargetKey.KeyType = ObjectKey; Data->Keys.Add(TargetKey);
		if (!Test->TestTrue(TEXT("Summon target blackboard"), CombatAI->UseBlackboard(Data, CombatBlackboard))) { return false; }
		Handle = ASC->GiveAbility(FGameplayAbilitySpec(URPGBossSummonAbility::StaticClass(), 1));
		Ability = CastChecked<URPGBossSummonAbility>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
		Ability->ActionTag = TAG_BossSummonTestAction;
		Ability->LocalSpawnOffsets = {FVector(200.f, 0.f, 0.f), FVector(200.f, 200.f, 0.f)};
		Ability->WindupSeconds = .1f; Ability->SummonLifetime = 2.f;
		Ability->StartupTimeout = 1.f; Ability->MaximumOwnedSummons = 2; Ability->bRequireNavigation = false;
		Restore();
		Test->TestFalse(TEXT("Missing summon class rejected"), ASC->TryActivateAbility(Handle));
		Ability->SummonClass = ARPGBossStartupTestCharacter::StaticClass();
		Ability->ActionTag = FGameplayTag();
		Test->TestFalse(TEXT("Missing action tag rejected"), ASC->TryActivateAbility(Handle));
		Ability->ActionTag = TAG_BossSummonTestAction;
		Ability->LocalSpawnOffsets.Reset();
		Test->TestFalse(TEXT("Empty wave rejected"), ASC->TryActivateAbility(Handle));
		Ability->LocalSpawnOffsets = {FVector(200.f, 0.f, 0.f), FVector(200.f, 200.f, 0.f)};
		Ability->StartupTimeout = 3.f;
		Test->TestFalse(TEXT("Startup timeout beyond lifetime rejected"), ASC->TryActivateAbility(Handle));
		Ability->StartupTimeout = 1.f;
		Source->SetRoleForTest(ROLE_SimulatedProxy);
		Test->TestFalse(TEXT("Client cannot cast summon"), ASC->TryActivateAbility(Handle));
		Test->TestFalse(TEXT("Client cannot reserve summon capacity"), Registry->HasCapacity(1, 2));
		Registry->ClearSummons();
		Source->SetRoleForTest(ROLE_Authority);
		Test->TestTrue(TEXT("Initial summon cast starts"), ASC->TryActivateAbility(Handle));
		Advance(.04f);
		Test->TestTrue(TEXT("Windup does not spawn NPCs"), Registry->GetSummons().IsEmpty());
		Ability->SummonClass = nullptr; Ability->LocalSpawnOffsets.Reset(); Ability->SummonLifetime = 99.f;
		Advance(.12f);
		Test->TestFalse(TEXT("Successful cast releases the BT while summons persist"), Ability->IsActive());
		Test->TestEqual(TEXT("Frozen plan survives authoring changes"), Registry->GetSummons().Num(), 2);
		Ability->SummonClass = ARPGBossStartupTestCharacter::StaticClass();
		Ability->LocalSpawnOffsets = {FVector(200.f, 0.f, 0.f), FVector(200.f, 200.f, 0.f)};
		Ability->SummonLifetime = 2.f;
		return Registry->GetSummons().Num() == 2;
	}
	void FinishCases(const TArray<FRPGBossSummonRecord>& Ready)
	{
		CombatAI->Tick(.02f);
		Test->TestTrue(TEXT("Ready summon acquires encounter target without a sight callback"),
			CombatBlackboard->GetValueAsObject(TEXT("TargetActor")) == Target);
		ARPGBossTestPawn* Other = World->SpawnActor<ARPGBossTestPawn>();
		AAIController* OtherAI = World->SpawnActor<AAIController>();
		OtherAI->SetGenericTeamId(FGenericTeamId(0)); OtherAI->Possess(Other);
		Other->SetActorLocation(FVector(600.f, 100.f, 0.f)); Other->SetTestHealth(1000.f);
		Source->BossThreatComponent->AddThreat(Other, 5.f, Source->BossAIComponent->GetBossState().EncounterId);
		CombatBlackboard->SetValueAsObject(TEXT("TargetActor"), Other); CombatAI->Tick(.02f);
		Test->TestTrue(TEXT("Valid independent participant is not overwritten by boss aggro"),
			CombatBlackboard->GetValueAsObject(TEXT("TargetActor")) == Other);
		OtherAI->UnPossess(); CombatAI->Tick(.02f);
		Test->TestTrue(TEXT("Unpossessed target falls back to the encounter target"),
			CombatBlackboard->GetValueAsObject(TEXT("TargetActor")) == Target);
		OtherAI->Possess(Other);
		Source->BossThreatComponent->AddThreat(Other, 5.f, Source->BossAIComponent->GetBossState().EncounterId);
		Target->SetTestHealth(0.f); Source->BossThreatComponent->RefreshTargets(); CombatAI->Tick(.02f);
		Test->TestTrue(TEXT("Dead target is replaced with a living participant"),
			CombatBlackboard->GetValueAsObject(TEXT("TargetActor")) == Other);
		Source->BossThreatComponent->ClearThreat(); CombatAI->Tick(.02f);
		Test->TestNull(TEXT("No valid participant clears the summon target"), CombatBlackboard->GetValueAsObject(TEXT("TargetActor")));
		Target->SetTestHealth(1000.f);
		Source->BossThreatComponent->AddThreat(Target, 20.f, Source->BossAIComponent->GetBossState().EncounterId);
		CombatAI->Tick(.02f);
		Test->TestTrue(TEXT("Target can be acquired again"), CombatBlackboard->GetValueAsObject(TEXT("TargetActor")) == Target);
		CombatSummon->GetAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
		CombatAI->Tick(.02f);
		Test->TestNull(TEXT("Dead summon cannot retain a combat target"), CombatBlackboard->GetValueAsObject(TEXT("TargetActor")));
		CombatSummon->GetAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100.f);
		CombatAI->Tick(.02f);
		CombatSummon->SetOwner(nullptr); CombatAI->Tick(.02f);
		Test->TestNull(TEXT("Lost encounter ownership clears the target"), CombatBlackboard->GetValueAsObject(TEXT("TargetActor")));
		CombatSummon->SetOwner(Source); CombatAI->Tick(.02f);
		CombatSummon->RetireEncounterSummon(); CombatAI->Tick(.02f);
		Test->TestNull(TEXT("Retiring summon cannot reacquire a target"), CombatBlackboard->GetValueAsObject(TEXT("TargetActor")));
		CombatAI->UnPossess(); CombatSummon->Destroy(); CombatAI->Destroy(); Other->Destroy(); OtherAI->Destroy();
		CombatData.Reset();
		Test->TestTrue(TEXT("Registry is replicated"), Registry->GetIsReplicated());
		Source->SetRoleForTest(ROLE_SimulatedProxy);
		Registry->ClearSummons(); Registry->RemoveWave(Ready[0].EncounterId, Ready[0].Sequence);
		Test->TestEqual(TEXT("Client cannot remove existing summons"), Registry->GetSummons().Num(), 2);
		Source->SetRoleForTest(ROLE_Authority);
		Test->TestTrue(TEXT("Distinct slots share one cast"), Ready[0].Slot != Ready[1].Slot && Ready[0].Sequence == Ready[1].Sequence);
		for (const auto& Record : Ready)
		{
			Test->TestEqual(TEXT("Encounter identity retained"), Record.EncounterId, Source->BossAIComponent->GetBossState().EncounterId);
			Test->TestTrue(TEXT("Lifetime frozen at commit"), FMath::IsNearlyEqual(Record.EndServerTime - Record.StartServerTime, 2.0));
			Test->TestTrue(TEXT("Own replicated NPC ASC is ready"), Record.Pawn->GetIsReplicated() && Record.Pawn->IsNPCStartUpDataReady() &&
				Record.Pawn->GetAbilitySystemComponent()->GetAvatarActor() == Record.Pawn);
			Test->TestTrue(TEXT("Encounter ownership and instigator retained"), Record.Pawn->GetOwner() == Source && Record.Pawn->GetInstigator() == Source);
			Test->TestEqual(TEXT("Summon inherits source team"), CastChecked<AAIController>(Record.Controller.Get())->GetGenericTeamId().GetId(), SourceAI->GetGenericTeamId().GetId());
			Test->TestTrue(TEXT("Summon cannot farm death drops"), Record.Pawn->AreDeathItemDropsSuppressed());
		}
		Test->TestFalse(TEXT("Live summons consume shared capacity"), ASC->TryActivateAbility(Handle));
		Ready[0].Pawn->GetAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
		Ready[0].Pawn->GetAbilitySystemComponent()->AddLooseGameplayTag(RPGGameplayTags::Shared_Status_Death);
		Test->TestFalse(TEXT("Dead summon cannot claim a drop"), Ready[0].Pawn->TryBeginDeathItemDrop());
		Test->TestFalse(TEXT("Corpse still consumes capacity"), ASC->TryActivateAbility(Handle));
		Advance(2.1f); CheckRemoved(Ready, TEXT("Lifetime removes summons and corpses"));
		Restore(); ASC->TryActivateAbility(Handle); Advance(.04f); ASC->CancelAbilityHandle(Handle); Advance(.3f);
		Test->TestTrue(TEXT("Canceled windup never spawns"), Registry->GetSummons().IsEmpty());
		Restore(); auto Records = CastWave(); Source->BossAIComponent->BeginReset(); Advance(.06f);
		CheckRemoved(Records, TEXT("Reset retires initializing summons"));
		Restore(); Records = CastWave(); Source->BossAIComponent->MarkWiped(); Advance(.06f);
		CheckRemoved(Records, TEXT("Wipe removes summons"));
		Restore(); Records = CastWave(); ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 69.f); Advance(.06f);
		CheckRemoved(Records, TEXT("Phase transition removes summons"));
		Restore(); Records = CastWave(); SourceAI->UnPossess(); Advance(.06f);
		CheckRemoved(Records, TEXT("Source controller loss removes summons")); SourceAI->Possess(Source);
		Restore(); Records = CastWave();
		if (Records.Num() == 2)
		{
			AAIController* Replacement = World->SpawnActor<AAIController>();
			Replacement->Possess(Records[0].Pawn); Advance(.12f);
			Test->TestTrue(TEXT("Replacement AI survives member retirement"), IsValid(Replacement) && !Replacement->IsActorBeingDestroyed() && !Replacement->GetPawn());
			Test->TestEqual(TEXT("Possession change removes only the affected summon"), Registry->GetSummons().Num(), 1);
			Registry->ClearSummons(); Advance(.06f); CheckRemoved(Records, TEXT("Clear removes remaining owned AI"));
		}
		Restore(); Ability->StartupTimeout = .1f; Records = CastWave(); Advance(.3f);
		CheckRemoved(Records, TEXT("Readiness deadline removes uninitialized NPCs")); Ability->StartupTimeout = 1.f;
		Restore();
		bool bResetDuringSpawn = false;
		TWeakObjectPtr<AActor> InterruptedAI;
		const FDelegateHandle SpawnHook = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* Actor)
		{
			if (!bResetDuringSpawn && Actor->IsA<AAIController>())
			{
				bResetDuringSpawn = true; InterruptedAI = Actor; Source->BossAIComponent->BeginReset();
			}
		}));
		ASC->TryActivateAbility(Handle); Advance(.3f); World->RemoveOnActorSpawnedHandler(SpawnHook);
		Test->TestTrue(TEXT("Reset inside controller spawn was exercised"), bResetDuringSpawn);
		Test->TestTrue(TEXT("Interrupted spawn leaves no wave or active cast"), Registry->GetSummons().IsEmpty() && !Ability->IsActive());
		Test->TestTrue(TEXT("Interrupted controller is not leaked"), !InterruptedAI.IsValid() || InterruptedAI->IsActorBeingDestroyed());
		Restore(); Records = CastWave(); ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f); Advance(.06f);
		CheckRemoved(Records, TEXT("Source death removes summons"));
		ASC->AddLooseGameplayTag(RPGGameplayTags::Shared_Status_Death);
		Test->TestTrue(TEXT("Ordinary NPC death drop policy is unchanged"), Source->CanProcessDeathItemDrop());
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossSummonTest, "ProjectRPG.Boss.Summon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGBossSummonTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FRPGBossSummonCommand(this));
	return true;
}
#endif
