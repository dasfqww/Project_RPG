#include "Tests/RPGBossTestFixture.h"
#include "Tests/RPGBossStartupTestFixture.h"
#include "Tests/RPGBossDamageTestFixture.h"
#include "Ability/RPGBossDashAbility.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossDashComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BossNetworkDashAction, "Tests.RaidBoss.Network.Dash");

bool ARPGBossNetworkTestGameMode::BeginDeferredDashJoin()
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("RPGBossNetworkDash"))) { return false; }
	FParse::Value(FCommandLine::Get(), TEXT("RPGBossDashDeferredJoin="), DeferredDashAddress);
	const FString GateRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/Tests")));
	if (!FParse::Value(FCommandLine::Get(), TEXT("RPGBossDashGate="), DashGateBase) ||
		!FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(DashGateBase), GateRoot))
	{
		DashStage = -1;
		UE_LOG(LogTemp, Error, TEXT("RAID_DASH_FAIL InvalidTestGate"));
	}
	// Preload a standalone client world. Only our test gate initiates travel;
	// the server must already have an active dash before the late connection.
	return !DeferredDashAddress.IsEmpty();
#else
	return false;
#endif
}

void ARPGBossNetworkTestGameMode::TickNetworkDash()
{
#if !UE_BUILD_SHIPPING
	if (DashGateBase.IsEmpty() || DashStage < 0) { return; }
	if (!DeferredDashAddress.IsEmpty())
	{
		if (!bDeferredDashReady)
		{
			bDeferredDashReady = true;
			UE_LOG(LogTemp, Display, TEXT("RAID_DASH_JOIN_READY LocalWorld=1"));
		}
		if (!bDeferredDashTravel && IFileManager::Get().FileExists(*(DashGateBase + TEXT(".active"))))
		{
			APlayerController* Local = GetWorld()->GetFirstPlayerController();
			if (Local)
			{
				bDeferredDashTravel = true;
				Local->ClientTravel(DeferredDashAddress, TRAVEL_Absolute);
			}
		}
		return;
	}
	if (!DashBoss)
	{
		DashBoss = GetWorld()->SpawnActor<ARPGBossStartupTestCharacter>(FVector(6000.f, 0.f, 1000.f), FRotator::ZeroRotator);
		DashBoss->SetActorEnableCollision(true);
		DashBoss->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		DashBoss->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		DashBoss->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		DashBoss->GetCharacterMovement()->GravityScale = 0.f;
		auto SpawnTarget = [&](float Offset)
		{
			ARPGBossTestPawn* Pawn = GetWorld()->SpawnActor<ARPGBossTestPawn>();
			AAIController* AI = GetWorld()->SpawnActor<AAIController>();
			AI->SetGenericTeamId(FGenericTeamId(0)); AI->Possess(Pawn);
			Pawn->SetActorLocation(FVector(6000.f + Offset, 0.f, 1000.f));
			Pawn->ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 20000.f);
			Pawn->SetTestHealth(20000.f);
			return Pawn;
		};
		DashFirst = SpawnTarget(200.f);
		DashSecond = SpawnTarget(350.f);
	}
	UAbilitySystemComponent* ASC = DashBoss->GetAbilitySystemComponent();
	const FRPGBossDashState Snapshot = DashBoss->BossDashComponent->GetDashState();
	const FGameplayAbilitySpec* Spec = DashCastHandle.IsValid() ? ASC->FindAbilitySpecFromHandle(DashCastHandle) : nullptr;
	if (DashStage == 0)
	{
		if (!DashBoss->IsNPCStartUpDataReady() || !IFileManager::Get().FileExists(*(DashGateBase + TEXT(".request")))) { return; }
		const int32 Encounter = DashBoss->BossAIComponent->GetBossState().EncounterId;
		DashBoss->BossThreatComponent->AddThreat(DashFirst, 1.f, Encounter);
		DashBoss->BossThreatComponent->AddThreat(DashSecond, 20.f, Encounter);
		DashCastHandle = ASC->GiveAbility(FGameplayAbilitySpec(URPGBossDashAbility::StaticClass(), 1));
		URPGBossDashAbility* Cast = CastChecked<URPGBossDashAbility>(ASC->FindAbilitySpecFromHandle(DashCastHandle)->GetPrimaryInstance());
		Cast->ActionTag = TAG_BossNetworkDashAction;
		Cast->WindupSeconds = .1f; Cast->DashDuration = 5.f; Cast->MaximumDistance = 350.f; Cast->HitRadius = 100.f;
		Cast->BaseDamage.Value = 10.f; Cast->DamageEffectClass = URPGBossTestDamageEffect::StaticClass();
		if (!ASC->TryActivateAbility(DashCastHandle))
		{
			DashStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_DASH_FAIL ActivationDenied")); return;
		}
		DashStage = 1;
	}
	else if (DashStage == 1)
	{
		if (!Snapshot.bActive)
		{
			if (!Spec || !Spec->IsActive()) { DashStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_DASH_FAIL EndedBeforeMovement")); }
			return;
		}
		int32 RemotePlayers = 0;
		for (const TWeakObjectPtr<APlayerController>& Player : ProbePlayers) { if (Player.IsValid() && !Player->IsLocalController()) { ++RemotePlayers; } }
		if (RemotePlayers != 1 || !DashBoss->BossDashComponent->IsDashActive() ||
			!FFileHelper::SaveStringToFile(TEXT("active"), *(DashGateBase + TEXT(".active"))))
		{
			DashStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_DASH_FAIL ActiveBeforeJoinGate")); return;
		}
		UE_LOG(LogTemp, Display, TEXT("RAID_DASH_ACTIVE_SERVER Active=1 BeforeLateJoin=1"));
		DashStage = 2;
	}
	else if (DashStage == 2)
	{
		if (!DashBoss->BossDashComponent->IsDashActive())
		{
			DashStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_DASH_FAIL ExpiredBeforeReset")); return;
		}
		if (!bDashMoved && DashBoss->GetActorLocation().X - Snapshot.StartLocation.X >= 50.f)
		{
			bDashMoved = true; UE_LOG(LogTemp, Display, TEXT("RAID_DASH_MOVE_SERVER Moved=1"));
		}
		const float FirstHP = DashFirst->Attributes->GetCurrentHealth();
		const float SecondHP = DashSecond->Attributes->GetCurrentHealth();
		if (FirstHP < 19990.f || SecondHP < 19990.f)
		{
			DashStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_DASH_FAIL DuplicateOrWrongDamage")); return;
		}
		if (!bDashDamaged && FirstHP == 19990.f && SecondHP == 19990.f)
		{
			bDashDamaged = true; UE_LOG(LogTemp, Display, TEXT("RAID_DASH_DAMAGE_SERVER Targets=2 OnceEach=1"));
		}
		if (!IFileManager::Get().FileExists(*(DashGateBase + TEXT(".reset")))) { return; }
		const AGameStateBase* Clock = GetWorld()->GetGameState();
		const double Now = Clock ? Clock->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
		if (!bDashLateJoin || !bDashMoved || !bDashDamaged || Snapshot.EndServerTime - Now < 1.25)
		{
			DashStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_DASH_FAIL ResetWindowOrEvidenceMissing")); return;
		}
		DashBoss->BossAIComponent->BeginReset();
		if (DashBoss->BossDashComponent->GetDashState().bActive || !Spec || Spec->IsActive() ||
			!DashBoss->GetCharacterMovement()->Velocity.IsNearlyZero() || DashBoss->GetCharacterMovement()->MovementMode != MOVE_None)
		{
			DashStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_DASH_FAIL ResetDidNotCancel")); return;
		}
		DashStoppedLocation = DashBoss->GetActorLocation();
		DashResetTime = GetWorld()->GetTimeSeconds();
		DashStage = 3;
		UE_LOG(LogTemp, Display, TEXT("RAID_DASH_RESET_SERVER Cancelled=1 VelocityZero=1 BeforeExpiry=1"));
	}
	else if (DashStage == 3 && GetWorld()->GetTimeSeconds() - DashResetTime >= 3.0)
	{
		if (!DashBoss->GetActorLocation().Equals(DashStoppedLocation, 1.f) ||
			DashFirst->Attributes->GetCurrentHealth() != 19990.f || DashSecond->Attributes->GetCurrentHealth() != 19990.f)
		{
			DashStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_DASH_FAIL MovementOrDamageAfterReset")); return;
		}
		DashStage = 4;
		UE_LOG(LogTemp, Display, TEXT("RAID_DASH_RESET_QUIET Stationary=1 Targets=2 HealthUnchanged=1"));
	}
#endif
}

void ARPGBossNetworkTestGameMode::RecordDashLateJoin()
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("RPGBossNetworkDash"))) { return; }
	int32 Remotes = 0;
	for (const TWeakObjectPtr<APlayerController>& Player : ProbePlayers) { if (Player.IsValid() && !Player->IsLocalController()) { ++Remotes; } }
	if (Remotes != 2) { return; }
	if (DashStage != 2 || !DashBoss || !DashBoss->BossDashComponent->IsDashActive())
	{
		DashStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_DASH_FAIL LateJoinWithoutActiveDash")); return;
	}
	bDashLateJoin = true;
	UE_LOG(LogTemp, Display, TEXT("RAID_DASH_LATE_JOIN_SERVER ActiveAtLogin=1"));
#endif
}

void ARPGBossStartupTestCharacter::TickNetworkDash()
{
#if !UE_BUILD_SHIPPING
	if (HasAuthority() || !FParse::Param(FCommandLine::Get(), TEXT("RPGBossNetworkDash"))) { return; }
	const FRPGBossDashState Snapshot = BossDashComponent->GetDashState();
	if (Snapshot.ActionTag != TAG_BossNetworkDashAction) { return; }
	if (!bSawDashState && BossDashComponent->IsDashActive() && Snapshot.EncounterId == 1 && Snapshot.HitRadius == 100.f &&
		Snapshot.EndLocation.Equals(FVector(6350.f, 0.f, 1000.f), 1.f))
	{
		const bool bDenied = !BossDashComponent->EndDash(Snapshot.EncounterId, Snapshot.Sequence);
		if (!bDenied || !BossDashComponent->GetDashState().bActive) { UE_LOG(LogTemp, Error, TEXT("RAID_DASH_FAIL ClientMutatedState")); return; }
		bSawDashState = true;
		FirstDashLocation = GetActorLocation();
		UE_LOG(LogTemp, Display, TEXT("RAID_DASH_STATE_REPLICATED Active=1 EndpointLocked=1 AuthorityDenied=1"));
	}
	if (bSawDashState && !bSawDashMove && BossDashComponent->IsDashActive() && GetActorLocation().X - FirstDashLocation.X >= 30.f)
	{
		bSawDashMove = true; UE_LOG(LogTemp, Display, TEXT("RAID_DASH_MOVE_REPLICATED TwoPositions=1"));
	}
	if (bSawDashState && !bSawDashDamage)
	{
		int32 Damaged = 0;
		for (TActorIterator<ARPGBossTestPawn> Target(GetWorld()); Target; ++Target)
		{
			if (Target->Attributes->GetMaxHealth() == 20000.f && Target->Attributes->GetCurrentHealth() == 19990.f &&
				FVector::Dist(Target->GetActorLocation(), Snapshot.StartLocation) <= 400.f) { ++Damaged; }
		}
		if (Damaged == 2) { bSawDashDamage = true; UE_LOG(LogTemp, Display, TEXT("RAID_DASH_DAMAGE_REPLICATED Targets=2 OnceEach=1")); }
	}
	if (bSawDashState && !bSawDashClear && !Snapshot.bActive && BossAIComponent->GetBossState().EncounterState == ERPGBossEncounterState::Resetting)
	{
		bSawDashClear = true;
		DashInactiveObserveTime = GetWorld()->GetTimeSeconds();
		DashStopProbeLocation = GetActorLocation();
		UE_LOG(LogTemp, Display, TEXT("RAID_DASH_CLEARED_REPLICATED Active=0"));
	}
	if (bSawDashClear && !bSawDashQuiet)
	{
		const double Elapsed = GetWorld()->GetTimeSeconds() - DashInactiveObserveTime;
		if (Elapsed <= .25) { DashStopProbeLocation = GetActorLocation(); }
		else if (Elapsed >= .75)
		{
			const AGameStateBase* Clock = GetWorld()->GetGameState();
			if (!GetActorLocation().Equals(DashStopProbeLocation, 5.f) || GetCharacterMovement()->MovementMode != MOVE_None ||
				!Clock || Clock->GetServerWorldTimeSeconds() >= Snapshot.EndServerTime)
			{
				UE_LOG(LogTemp, Error, TEXT("RAID_DASH_FAIL ClientResetDidNotStopBeforeExpiry")); return;
			}
			bSawDashQuiet = true;
			UE_LOG(LogTemp, Display, TEXT("RAID_DASH_RESET_REPLICATED Stationary=1 BeforeExpiry=1"));
		}
	}
#endif
}
