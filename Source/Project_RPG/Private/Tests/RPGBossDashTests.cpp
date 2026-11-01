#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/RPGBossStartupTestFixture.h"
#include "Tests/RPGBossTestFixture.h"
#include "Tests/RPGBossDamageTestFixture.h"
#include "Ability/RPGBossDashAbility.h"
#include "AI/RPGBossDashComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "RPGGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BossDashTestAction, "Tests.RaidBoss.Dash.Action");

class FRPGBossDashCommand : public IAutomationLatentCommand
{
public:
	explicit FRPGBossDashCommand(FAutomationTestBase* InTest) : Test(InTest) {}
	virtual ~FRPGBossDashCommand() override
	{
		if (World)
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
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
			if (!Test->TestNotNull(TEXT("Dash world"), World)) { return true; }
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL()); World->CreateAISystem();
			World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
			Boss = World->SpawnActor<ARPGBossStartupTestCharacter>();
			if (!Test->TestNotNull(TEXT("Dash character"), Boss)) { return true; }
			Boss->SpawnDefaultController();
			Deadline = FPlatformTime::Seconds() + 15.0;
			return false;
		}
		if (!Boss->IsNPCStartUpDataReady())
		{
			if (FPlatformTime::Seconds() < Deadline) { return false; }
			Test->AddError(TEXT("Dash test NPC startup timed out")); return true;
		}
		RunCases();
		return true;
	}
private:
	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	ARPGBossStartupTestCharacter* Boss = nullptr;
	double Deadline = 0.0;
	void RunCases()
	{
		AAIController* Controller = Cast<AAIController>(Boss->GetController());
		if (!Test->TestNotNull(TEXT("Dash AI controller"), Controller)) { return; }
		Controller->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
		UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent();
		UCharacterMovementComponent* Movement = Boss->GetCharacterMovement();
		Boss->SetActorEnableCollision(true);
		Boss->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Boss->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);
		Boss->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Movement->SetMovementMode(MOVE_Flying); // No floor/navmesh or content asset is needed.
		Movement->GravityScale = 0.f;
		const auto SpawnTarget = [&](FVector Location, uint8 Team)
		{
			ARPGBossTestPawn* Pawn = World->SpawnActor<ARPGBossTestPawn>();
			AAIController* AI = World->SpawnActor<AAIController>();
			AI->SetGenericTeamId(FGenericTeamId(Team)); AI->Possess(Pawn);
			Pawn->SetActorLocation(Location);
			Pawn->ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 1000.f);
			Pawn->SetTestHealth(1000.f);
			return Pawn;
		};
		ARPGBossTestPawn* Aim = SpawnTarget(FVector(300.f, 0.f, 0.f), 0);
		ARPGBossTestPawn* Second = SpawnTarget(FVector(150.f, 0.f, 0.f), 0);
		ARPGBossTestPawn* Outsider = SpawnTarget(FVector(200.f, 0.f, 0.f), 0);
		ARPGBossTestPawn* Friendly = SpawnTarget(FVector(220.f, 0.f, 0.f), 1);
		const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(FGameplayAbilitySpec(URPGBossDashAbility::StaticClass(), 1));
		URPGBossDashAbility* Ability = CastChecked<URPGBossDashAbility>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
		Ability->ActionTag = TAG_BossDashTestAction;
		Ability->DamageEffectClass = URPGBossTestDamageEffect::StaticClass();
		Ability->BaseDamage.Value = 10.f;
		Ability->WindupSeconds = .1f;
		Ability->MaximumDistance = 300.f;
		Ability->DashDuration = .6f;
		Ability->HitRadius = 70.f;
		uint64 Frame = GFrameCounter;
		const auto Advance = [&](float Seconds)
		{
			for (int32 Index = 0; Index < FMath::CeilToInt(Seconds / .02f); ++Index)
			{
				TGuardValue<uint64> FrameGuard(GFrameCounter, ++Frame);
				World->Tick(LEVELTICK_All, .02f); Boss->BossThreatComponent->RefreshTargets();
			}
		};
		const auto Restore = [&]()
		{
			ASC->CancelAbilityHandle(Handle);
			Boss->BossAIComponent->BeginReset(); Boss->BossAIComponent->CompleteReset();
			ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100.f);
			Movement->SetMovementMode(MOVE_Flying);
			Boss->SetActorLocation(FVector::ZeroVector, false, nullptr, ETeleportType::TeleportPhysics);
			Aim->SetActorLocation(FVector(300.f, 0.f, 0.f)); Second->SetActorLocation(FVector(150.f, 0.f, 0.f));
			Aim->SetTestHealth(1000.f); Second->SetTestHealth(1000.f);
			Boss->BossAIComponent->StartEncounter();
			Advance(.04f); // Remove the previous task's marked root-motion source before retrying.
			Boss->BossThreatComponent->ClearThreat();
			Boss->BossThreatComponent->AddThreat(Aim, 20.f, Boss->BossAIComponent->GetBossState().EncounterId);
			Boss->BossThreatComponent->AddThreat(Second, 1.f, Boss->BossAIComponent->GetBossState().EncounterId);
			Boss->BossThreatComponent->RefreshTargets();
		};
		Restore();
		Boss->SetRoleForTest(ROLE_SimulatedProxy);
		Test->TestFalse(TEXT("Client cannot cast dash"), ASC->TryActivateAbility(Handle));
		Test->TestFalse(TEXT("Client cannot publish dash state"), Boss->BossDashComponent->BeginDash(FRPGBossDashState()));
		Boss->SetRoleForTest(ROLE_Authority);
		Test->TestTrue(TEXT("Dash cast starts"), ASC->TryActivateAbility(Handle));
		Advance(.06f);
		Test->TestTrue(TEXT("Windup does not move the boss"), Boss->GetActorLocation().IsNearlyZero());
		Test->TestEqual(TEXT("Windup does not damage targets"), Second->Attributes->GetCurrentHealth(), 1000.f);
		Advance(.12f);
		Test->TestTrue(TEXT("Dash state is active and replicated"), Boss->BossDashComponent->IsDashActive() && Boss->BossDashComponent->GetIsReplicated());
		Test->TestEqual(TEXT("Aim endpoint is locked"), Boss->BossDashComponent->GetDashState().EndLocation, FVector(300.f, 0.f, 0.f));
		Ability->BaseDamage.Value = 999.f; Ability->HitRadius = 1.f;
		Advance(.8f);
		Test->TestTrue(TEXT("Server root motion moved toward the locked endpoint"), Boss->GetActorLocation().X > 250.f && FMath::Abs(Boss->GetActorLocation().Y) < 1.f);
		Test->TestEqual(TEXT("Aim is hit once with frozen damage"), Aim->Attributes->GetCurrentHealth(), 990.f);
		Test->TestEqual(TEXT("Second participant is hit once across multiple segments"), Second->Attributes->GetCurrentHealth(), 990.f);
		Test->TestEqual(TEXT("Unregistered hostile is excluded"), Outsider->Attributes->GetCurrentHealth(), 1000.f);
		Test->TestEqual(TEXT("Friendly is excluded"), Friendly->Attributes->GetCurrentHealth(), 1000.f);
		Test->TestFalse(TEXT("Duration clears dash state"), Boss->BossDashComponent->GetDashState().bActive);
		Test->TestFalse(TEXT("Duration ends the GAS cast"), Ability->IsActive());
		Test->TestTrue(TEXT("Completed dash leaves no velocity"), Movement->Velocity.IsNearlyZero());
		Ability->BaseDamage.Value = 10.f; Ability->HitRadius = 70.f;
		Restore(); ASC->TryActivateAbility(Handle); Advance(.04f);
		Aim->SetActorLocation(FVector(300.f, 600.f, 0.f)); Advance(.14f);
		Test->TestEqual(TEXT("Target movement does not retarget the telegraphed endpoint"), Boss->BossDashComponent->GetDashState().EndLocation, FVector(300.f, 0.f, 0.f));
		Advance(.8f);
		Test->TestTrue(TEXT("Dash does not home toward a moving target"), FMath::Abs(Boss->GetActorLocation().Y) < 1.f);
		Test->TestEqual(TEXT("A target outside the traveled path is not hit"), Aim->Attributes->GetCurrentHealth(), 1000.f);
		Restore(); ASC->TryActivateAbility(Handle); Advance(.04f); ASC->CancelAbilityHandle(Handle); Advance(.8f);
		Test->TestTrue(TEXT("Canceled windup never moves"), Boss->GetActorLocation().IsNearlyZero());
		Test->TestEqual(TEXT("Canceled windup never hits"), Second->Attributes->GetCurrentHealth(), 1000.f);
		Restore(); ASC->TryActivateAbility(Handle); Advance(.2f);
		Boss->BossAIComponent->MarkWiped();
		const FVector WipedLocation = Boss->GetActorLocation(); Advance(.5f);
		Test->TestFalse(TEXT("Wipe ends cast and state"), Ability->IsActive() || Boss->BossDashComponent->GetDashState().bActive);
		Test->TestTrue(TEXT("Wipe does not restore disabled movement"), Movement->MovementMode == MOVE_None && Boss->GetActorLocation().Equals(WipedLocation, .1f));
		Test->TestEqual(TEXT("Wipe stops future segment damage"), Aim->Attributes->GetCurrentHealth(), 1000.f);
		Restore(); ASC->TryActivateAbility(Handle); Advance(.2f);
		Boss->BossAIComponent->BeginReset();
		const FVector ResetLocation = Boss->GetActorLocation(); Advance(.5f);
		Test->TestFalse(TEXT("Reset clears cast and replicated action"), Ability->IsActive() || Boss->BossDashComponent->GetDashState().bActive);
		Test->TestTrue(TEXT("Reset leaves movement disabled and stationary"), Movement->MovementMode == MOVE_None && Boss->GetActorLocation().Equals(ResetLocation, .1f));
		Test->TestEqual(TEXT("Reset prevents later path damage"), Aim->Attributes->GetCurrentHealth(), 1000.f);
		Restore(); ASC->TryActivateAbility(Handle); Advance(.2f);
		ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 69.f);
		Test->TestFalse(TEXT("Phase transition cancels dash"), Ability->IsActive() || Boss->BossDashComponent->GetDashState().bActive);
		Restore(); ASC->TryActivateAbility(Handle); Advance(.2f); Controller->UnPossess(); Advance(.3f);
		Test->TestFalse(TEXT("Source controller loss cancels dash"), Ability->IsActive() || Boss->BossDashComponent->GetDashState().bActive);
		Test->TestEqual(TEXT("Controller loss prevents future damage"), Aim->Attributes->GetCurrentHealth(), 1000.f);
		Controller->Possess(Boss);
		Restore(); ASC->TryActivateAbility(Handle); Advance(.2f);
		AController* AimController = Aim->GetController(); AimController->UnPossess(); Advance(.5f);
		Test->TestFalse(TEXT("Aim controller loss cancels dash immediately"), Ability->IsActive() || Boss->BossDashComponent->GetDashState().bActive);
		Test->TestEqual(TEXT("Disconnected aim does not receive a future hit"), Aim->Attributes->GetCurrentHealth(), 1000.f);
		AimController->Possess(Aim);
		Restore();
		const FDelegateHandle ResetDuringHit = Second->ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
			[&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle) { Boss->BossAIComponent->BeginReset(); });
		ASC->TryActivateAbility(Handle); Advance(.8f);
		Second->ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(ResetDuringHit);
		Test->TestFalse(TEXT("Reset inside a damage callback ends the dash"), Ability->IsActive() || Boss->BossDashComponent->GetDashState().bActive);
		Test->TestEqual(TEXT("Damage callback reset prevents later participants being hit"), Aim->Attributes->GetCurrentHealth(), 1000.f);
		Restore();
		AActor* Wall = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
		Wall->SetRootComponent(Box); Wall->AddInstanceComponent(Box);
		Box->SetBoxExtent(FVector(20.f, 200.f, 200.f));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionObjectType(ECC_WorldStatic); Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent(); Wall->SetActorLocation(FVector(180.f, 0.f, 0.f));
		ASC->TryActivateAbility(Handle); Advance(1.f);
		Test->TestTrue(TEXT("Character collision stops dash at a wall"), Boss->GetActorLocation().X < 160.f);
		Test->TestEqual(TEXT("Wall blocks damage to the target beyond it"), Aim->Attributes->GetCurrentHealth(), 1000.f);
		Wall->Destroy();
		Restore();
		Ability->DashDuration = 0.f;
		Test->TestFalse(TEXT("Invalid duration cannot start or commit"), ASC->TryActivateAbility(Handle));
		Ability->DashDuration = .6f;
		ASC->TryActivateAbility(Handle); Advance(.2f);
		ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
		Test->TestFalse(TEXT("Boss death ends the dash and its state"), Ability->IsActive() || Boss->BossDashComponent->GetDashState().bActive);
		Test->TestTrue(TEXT("Dash cleanup cannot restore a dead boss's movement"), Movement->MovementMode == MOVE_None && Movement->Velocity.IsNearlyZero());
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossDashTest, "ProjectRPG.Boss.Dash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGBossDashTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FRPGBossDashCommand(this));
	return true;
}
#endif
