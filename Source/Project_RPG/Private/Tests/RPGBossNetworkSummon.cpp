#include "Tests/RPGBossTestFixture.h"
#include "Tests/RPGBossStartupTestFixture.h"
#include "Ability/RPGBossSummonAbility.h"
#include "AI/RPGBossThreatComponent.h"
#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BossNetworkSummonAction, "Tests.RaidBoss.Network.Summon");

bool ARPGBossNetworkTestGameMode::BeginNetworkSummon()
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("RPGBossNetworkTest")) ||
		!FParse::Param(FCommandLine::Get(), TEXT("RPGBossNetworkSummon"))) { return false; }
	FParse::Value(FCommandLine::Get(), TEXT("RPGBossSummonDeferredJoin="), DeferredSummonAddress);
	const FString TestRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/Tests")));
	if (!FParse::Value(FCommandLine::Get(), TEXT("RPGBossSummonGate="), SummonGateBase) ||
		!FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(SummonGateBase), TestRoot))
	{
		SummonStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_SUMMON_FAIL InvalidTestGate"));
	}
	return true; // Isolate the opt-in summon scenario from other boss probes.
#else
	return false;
#endif
}

void ARPGBossNetworkTestGameMode::TickNetworkSummon()
{
#if !UE_BUILD_SHIPPING
	if (SummonStage < 0 || SummonGateBase.IsEmpty()) { return; }
	if (!DeferredSummonAddress.IsEmpty())
	{
		if (!bSummonDeferredReady && GetWorld()->GetFirstPlayerController())
		{
			bSummonDeferredReady = true;
			UE_LOG(LogTemp, Display, TEXT("RAID_SUMMON_JOIN_READY LocalWorld=1"));
		}
		if (!bSummonDeferredTravel && IFileManager::Get().FileExists(*(SummonGateBase + TEXT(".active"))))
		{
			if (APlayerController* Local = GetWorld()->GetFirstPlayerController())
			{
				bSummonDeferredTravel = true; Local->ClientTravel(DeferredSummonAddress, TRAVEL_Absolute);
			}
		}
		return;
	}
	if (!SummonBoss)
	{
		SummonBoss = GetWorld()->SpawnActor<ARPGBossStartupTestCharacter>(FVector(9000.f, 0.f, 1000.f), FRotator::ZeroRotator);
		SummonTarget = GetWorld()->SpawnActor<ARPGBossTestPawn>();
		AAIController* TargetAI = GetWorld()->SpawnActor<AAIController>();
		if (!SummonBoss || !SummonTarget || !TargetAI)
		{
			SummonStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_SUMMON_FAIL FixtureSpawn")); return;
		}
		TargetAI->SetGenericTeamId(FGenericTeamId(0)); TargetAI->Possess(SummonTarget);
		SummonTarget->SetActorLocation(FVector(9600.f, 0.f, 1000.f));
	}
	UAbilitySystemComponent* ASC = SummonBoss->GetAbilitySystemComponent();
	URPGBossSummonComponent* Registry = SummonBoss->BossSummonComponent;
	const auto OwnedActorsGone = [this]()
	{
		for (const auto& Pawn : SummonMembers) { if (Pawn.IsValid() && !Pawn->IsActorBeingDestroyed()) { return false; } }
		for (const auto& AI : SummonControllers) { if (AI.IsValid() && !AI->IsActorBeingDestroyed()) { return false; } }
		return true;
	};
	if (SummonStage == 0)
	{
		if (!SummonBoss->IsNPCStartUpDataReady()) { return; }
		SummonStage = 1;
		UE_LOG(LogTemp, Display, TEXT("RAID_SUMMON_READY Startup=1"));
	}
	if (SummonStage == 1)
	{
		if (!IFileManager::Get().FileExists(*(SummonGateBase + TEXT(".request")))) { return; }
		const int32 Encounter = SummonBoss->BossAIComponent->GetBossState().EncounterId;
		if (!SummonBoss->BossThreatComponent->AddThreat(SummonTarget, 20.f, Encounter))
		{
			SummonStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_SUMMON_FAIL TargetDenied")); return;
		}
		SummonCastHandle = ASC->GiveAbility(FGameplayAbilitySpec(URPGBossSummonAbility::StaticClass(), 1));
		URPGBossSummonAbility* Cast = CastChecked<URPGBossSummonAbility>(ASC->FindAbilitySpecFromHandle(SummonCastHandle)->GetPrimaryInstance());
		Cast->ActionTag = TAG_BossNetworkSummonAction;
		Cast->SummonClass = ARPGBossStartupTestCharacter::StaticClass();
		Cast->LocalSpawnOffsets = {FVector(200.f, 0.f, 0.f), FVector(200.f, 200.f, 0.f)};
		Cast->WindupSeconds = .1f; Cast->SummonLifetime = 120.f; Cast->StartupTimeout = 10.f;
		Cast->MaximumOwnedSummons = 2; Cast->bRequireNavigation = false;
		if (!ASC->TryActivateAbility(SummonCastHandle))
		{
			SummonStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_SUMMON_FAIL ActivationDenied")); return;
		}
		SummonStage = 2;
	}
	else if (SummonStage == 2)
	{
		const auto Records = Registry->GetSummons();
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(SummonCastHandle);
		if (Records.Num() != 2 || Records.ContainsByPredicate([](const FRPGBossSummonRecord& Record) { return !Record.bInitialized; })) { return; }
		int32 Remotes = 0;
		for (const auto& Player : ProbePlayers) { if (Player.IsValid() && !Player->IsLocalController()) { ++Remotes; } }
		if (!Spec || Spec->IsActive() || Remotes != 1)
		{
			SummonStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_SUMMON_FAIL CastOrJoinOrdering")); return;
		}
		const AAIController* SourceAI = Cast<AAIController>(SummonBoss->GetController());
		for (const auto& Record : Records)
		{
			ARPGNonPlayerCharacter* Pawn = Record.Pawn;
			const AAIController* AI = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
			if (!Pawn || !AI || !SourceAI || !Pawn->IsNPCStartUpDataReady() || !Pawn->AreDeathItemDropsSuppressed() ||
				AI->GetGenericTeamId() != SourceAI->GetGenericTeamId() || Pawn->GetOwner() != SummonBoss || Pawn->GetInstigator() != SummonBoss)
			{
				SummonStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_SUMMON_FAIL NPCInitialization")); return;
			}
			Pawn->GetAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 80.f + Record.Slot * 10.f);
			SummonMembers.Add(Pawn); SummonControllers.Add(Pawn->GetController()); Pawn->ForceNetUpdate();
			SummonExpiry = Record.EndServerTime;
		}
		SummonStage = 3;
		if (!FFileHelper::SaveStringToFile(TEXT("active"), *(SummonGateBase + TEXT(".active"))))
		{
			SummonStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_SUMMON_FAIL ActiveGate")); return;
		}
		UE_LOG(LogTemp, Display, TEXT("RAID_SUMMON_ACTIVE_SERVER NPCs=2 Ready=1 CastEnded=1 BeforeLateJoin=1"));
	}
	else if (SummonStage == 3)
	{
		if (Registry->GetSummons().Num() != 2)
		{
			SummonStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_SUMMON_FAIL RemovedBeforeReset")); return;
		}
		if (!IFileManager::Get().FileExists(*(SummonGateBase + TEXT(".reset")))) { return; }
		if (!bSummonLateJoin || GetWorld()->GetTimeSeconds() >= SummonExpiry - 5.0)
		{
			SummonStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_SUMMON_FAIL ResetWindowOrLateJoin")); return;
		}
		SummonBoss->BossAIComponent->BeginReset();
		if (!Registry->GetSummons().IsEmpty() || !OwnedActorsGone())
		{
			SummonStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_SUMMON_FAIL ResetOwnershipCleanup")); return;
		}
		SummonStage = 4; SummonResetTime = GetWorld()->GetTimeSeconds();
		UE_LOG(LogTemp, Display, TEXT("RAID_SUMMON_RESET_SERVER NPCsRemoved=2 OwnedAIRemoved=2 BeforeExpiry=1"));
	}
	else if (SummonStage == 4 && GetWorld()->GetTimeSeconds() - SummonResetTime >= 3.0)
	{
		if (!Registry->GetSummons().IsEmpty() || !OwnedActorsGone() ||
			SummonBoss->BossAIComponent->GetBossState().EncounterState != ERPGBossEncounterState::Resetting)
		{
			SummonStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_SUMMON_FAIL StaleSummonsAfterReset")); return;
		}
		SummonStage = 5;
		UE_LOG(LogTemp, Display, TEXT("RAID_SUMMON_RESET_QUIET NPCs=0 OwnedAI=0"));
	}
#endif
}

void ARPGBossNetworkTestGameMode::RecordSummonLateJoin()
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("RPGBossNetworkSummon"))) { return; }
	int32 Remotes = 0;
	for (const auto& Player : ProbePlayers) { if (Player.IsValid() && !Player->IsLocalController()) { ++Remotes; } }
	if (Remotes == 1) { UE_LOG(LogTemp, Display, TEXT("RAID_SUMMON_FIRST_CONNECTED RemotePlayers=1")); return; }
	if (Remotes != 2) { return; }
	if (SummonStage != 3 || !SummonBoss || SummonBoss->BossSummonComponent->GetSummons().Num() != 2)
	{
		SummonStage = -1; UE_LOG(LogTemp, Error, TEXT("RAID_SUMMON_FAIL LateLoginWithoutSummons")); return;
	}
	bSummonLateJoin = true;
	UE_LOG(LogTemp, Display, TEXT("RAID_SUMMON_LATE_JOIN_SERVER ExistingNPCsAtLogin=2"));
#endif
}

void ARPGBossStartupTestCharacter::TickNetworkSummon()
{
#if !UE_BUILD_SHIPPING
	if (HasAuthority() || !FParse::Param(FCommandLine::Get(), TEXT("RPGBossNetworkSummon"))) { return; }
	const auto Records = BossSummonComponent->GetSummons();
	if (!bSawSummonState && Records.Num() == 2)
	{
		const FRPGBossState Context = BossAIComponent->GetBossState();
		int32 Slots = 0;
		for (const auto& Record : Records)
		{
			const ARPGNonPlayerCharacter* Pawn = Record.Pawn;
			const UAbilitySystemComponent* ASC = Pawn ? Pawn->GetAbilitySystemComponent() : nullptr;
			const FVector Expected(9200.f, Record.Slot * 200.f, 1000.f);
			if (!IsValid(Pawn) || !Record.bInitialized || Record.Slot < 0 || Record.Slot > 1 ||
				Record.EncounterId != Context.EncounterId || Record.PhaseIndex != Context.PhaseIndex || Record.Sequence <= 0 ||
				!FMath::IsNearlyEqual(Record.EndServerTime - Record.StartServerTime, 120.0) || Pawn->GetInstigator() != this ||
				!Pawn->GetActorLocation().Equals(Expected, 1.f) || Pawn->GetController() || !ASC || ASC->GetAvatarActor() != Pawn ||
				ASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute()) != 100.f ||
				ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) != 80.f + Record.Slot * 10.f) { return; }
			Slots |= 1 << Record.Slot;
		}
		if (Slots != 3 || Records[0].Sequence != Records[1].Sequence) { return; }
		BossSummonComponent->ClearSummons();
		BossSummonComponent->RemoveWave(Records[0].EncounterId, Records[0].Sequence);
		if (BossSummonComponent->GetSummons().Num() != 2 || BossSummonComponent->HasCapacity(1, 2))
		{
			UE_LOG(LogTemp, Error, TEXT("RAID_SUMMON_FAIL ClientChangedOwnership")); return;
		}
		bSawSummonState = true; ObservedSummonExpiry = Records[0].EndServerTime;
		for (const auto& Record : Records) { ObservedSummons.Add(Record.Pawn.Get()); }
		UE_LOG(LogTemp, Display, TEXT("RAID_SUMMON_STATE_REPLICATED NPCs=2 Metadata=1 Position=1 GASHealth=1 AuthorityDenied=1"));
	}
	if (bSawSummonState && !bSawSummonClear && Records.IsEmpty() &&
		BossAIComponent->GetBossState().EncounterState == ERPGBossEncounterState::Resetting)
	{
		for (const auto& Pawn : ObservedSummons) { if (Pawn.IsValid() && !Pawn->IsActorBeingDestroyed()) { return; } }
		const AGameStateBase* Clock = GetWorld()->GetGameState();
		if (!Clock || Clock->GetServerWorldTimeSeconds() >= ObservedSummonExpiry)
		{
			UE_LOG(LogTemp, Error, TEXT("RAID_SUMMON_FAIL ClientRemovedOnlyAtExpiry")); return;
		}
		bSawSummonClear = true;
		UE_LOG(LogTemp, Display, TEXT("RAID_SUMMON_CLEARED_REPLICATED RegistryEmpty=1 NPCsDestroyed=2 BeforeExpiry=1"));
	}
#endif
}
