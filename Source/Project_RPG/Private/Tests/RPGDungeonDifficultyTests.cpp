#if WITH_DEV_AUTOMATION_TESTS
#include "GameMode/RPGGameModeBase.h"
#include "GameInstance/RPGGameInstance.h"
#include "GameMode/RPGBossBattleGameMode.h"
#include "GameState/RPGBossBattleGameState.h"
#include "Tests/RPGBossBattleRetryTestFixture.h"
#include "Tests/RPGBossStartupTestFixture.h"
#include "Character/RPGBossCharacter.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#endif

// Exercise the same native apply path as the authenticated start callback,
// without issuing HTTP, consuming tickets, starting an encounter or granting rewards.
struct FRPGDungeonDifficultyTestAccess
{
	static bool Apply(ARPGGameModeBase* Mode, const FRPGDungeonSession& Session)
	{ return Mode->ApplyBackendDungeonConfiguration(Session, TEXT("difficulty-session"), TEXT("difficulty-server")); }
	static void FailedResponse(ARPGGameModeBase* Mode, const FRPGDungeonSession& Session)
	{ Mode->HandleDungeonSessionUpdated(Session, false); }
	static int32 Members(const ARPGGameModeBase* Mode) { return Mode->DungeonMemberCharacterIds.Num(); }
	static UClass* BossClass(const ARPGBossBattleGameMode* Mode) { return Mode->BossClass.Get(); }
};

namespace
{
	struct FDifficultyWorld
	{
		UWorld* World = nullptr;
		URPGGameInstance* Instance = nullptr;
		ARPGGameModeBase* Mode = nullptr;
		explicit FDifficultyWorld(const TCHAR* GameModePath = TEXT("/Script/Project_RPG.RPGGameModeBase"))
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
			if (!World) return;
			Instance = NewObject<URPGGameInstance>();
			auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World); Context.OwningGameInstance = Instance;
			World->SetGameInstance(Instance);
			FURL Url;
			Url.AddOption(*(FString(TEXT("game=")) + GameModePath));
			if (!World->SetGameMode(Url)) return;
			Mode = Cast<ARPGGameModeBase>(World->GetAuthGameMode());
			World->InitializeActorsForPlay(Url);
		}
		~FDifficultyWorld()
		{
			if (World) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
		}
	};
	FRPGDungeonSession Snapshot()
	{
		FRPGDungeonSession Session;
		Session.DungeonSessionId = TEXT("difficulty-session");
		Session.ServerId = TEXT("difficulty-server");
		Session.State = TEXT("InProgress");
		Session.Difficulty = TEXT("Hard");
		Session.Members.AddDefaulted_GetRef().CharacterId = TEXT("remaining-member");
		return Session;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonDifficultyCodecTest, "ProjectRPG.Online.DungeonDifficulty.Codec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonDifficultyCodecTest::RunTest(const FString&)
{
	ERPGGameDifficulty Value = ERPGGameDifficulty::Hard;
	for (const auto Expected : {ERPGGameDifficulty::Easy, ERPGGameDifficulty::Normal, ERPGGameDifficulty::Hard, ERPGGameDifficulty::Hell})
	{
		const FString Text = StaticEnum<ERPGGameDifficulty>()->GetNameStringByValue(static_cast<int64>(Expected));
		TestTrue(TEXT("Only exact authored difficulty names decode"), ARPGGameModeBase::TryParseBackendDungeonDifficulty(Text, Value));
		TestTrue(TEXT("Correct enum is selected"), Value == Expected);
	}
	for (const FString Invalid : {TEXT(""), TEXT("hard"), TEXT("Hard "), TEXT(" Hard"), TEXT("Unknown"), TEXT("3"), TEXT("ERPGGameDifficulty::Hard")})
	{
		Value = ERPGGameDifficulty::Hell;
		TestFalse(TEXT("Malformed/unknown strings never select a default"), ARPGGameModeBase::TryParseBackendDungeonDifficulty(Invalid, Value));
		TestTrue(TEXT("Rejected value preserves prior output"), Value == ERPGGameDifficulty::Hell);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonDifficultyConfigurationTest, "ProjectRPG.Online.DungeonDifficulty.Configuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonDifficultyConfigurationTest::RunTest(const FString&)
{
	FDifficultyWorld Fixture;
	if (!TestNotNull(TEXT("Authoritative GameMode"), Fixture.Mode)) return false;
	auto* Mode = Fixture.Mode;
	Mode->SetGameDifficulty(ERPGGameDifficulty::Easy);
	Fixture.Instance->SetPendingGameDifficulty(ERPGGameDifficulty::Normal);
	TestFalse(TEXT("Offline/default difficulty is not backend readiness"), Mode->IsBackendDungeonDifficultyReady());
	FRPGDungeonSession Session = Snapshot();
	FRPGDungeonDifficultyTestAccess::FailedResponse(Mode, Session);
	TestFalse(TEXT("Failed HTTP callback cannot mark readiness"), Mode->IsBackendDungeonDifficultyReady());
	for (int32 Case = 0; Case < 6; ++Case)
	{
		auto Invalid = Session;
		switch (Case)
		{
		case 0: Invalid.DungeonSessionId = TEXT("other-session"); break;
		case 1: Invalid.ServerId = TEXT("Difficulty-server"); break;
		case 2: Invalid.ServerId.Reset(); break;
		case 3: Invalid.State = TEXT("Loading"); break;
		case 4: Invalid.Difficulty = TEXT("hard"); break;
		case 5: Invalid.Difficulty = TEXT("Nightmare"); break;
		}
		TestFalse(TEXT("Wrong assignment or invalid state/difficulty cannot apply"), FRPGDungeonDifficultyTestAccess::Apply(Mode, Invalid));
		TestFalse(TEXT("Invalid configuration keeps admission unready"), Mode->IsBackendDungeonDifficultyReady());
		TestTrue(TEXT("Invalid configuration preserves offline enum"), Mode->GetCurrentGameDifficulty() == ERPGGameDifficulty::Easy);
	}
	TestTrue(TEXT("Exact backend start applies configuration"), FRPGDungeonDifficultyTestAccess::Apply(Mode, Session));
	TestTrue(TEXT("Backend assignment replaces default difficulty"), Mode->GetCurrentGameDifficulty() == ERPGGameDifficulty::Hard);
	TestTrue(TEXT("GameInstance BP reads see the confirmed value"), Fixture.Instance->GetPendingGameDifficulty() == ERPGGameDifficulty::Hard);
	TestTrue(TEXT("Admission readiness follows application"), Mode->IsBackendDungeonDifficultyReady());
	TestEqual(TEXT("Reward-member snapshot remains populated"), FRPGDungeonDifficultyTestAccess::Members(Mode), 1);
	Mode->SetGameDifficulty(ERPGGameDifficulty::Hell);
	TestTrue(TEXT("Public/BP setter cannot override online difficulty"), Mode->GetCurrentGameDifficulty() == ERPGGameDifficulty::Hard);
	TestTrue(TEXT("Same-difficulty heartbeat remains valid"), FRPGDungeonDifficultyTestAccess::Apply(Mode, Session));
	Session.Difficulty = TEXT("Hell"); Session.Members.Reset();
	TestFalse(TEXT("A later changed difficulty cannot retune a running encounter"), FRPGDungeonDifficultyTestAccess::Apply(Mode, Session));
	TestTrue(TEXT("Conflict preserves pinned difficulty"), Mode->GetCurrentGameDifficulty() == ERPGGameDifficulty::Hard);
	TestEqual(TEXT("Conflict preserves remaining-party reward snapshot"), FRPGDungeonDifficultyTestAccess::Members(Mode), 1);
	Mode->EndPlay(EEndPlayReason::Quit);
	TestFalse(TEXT("EndPlay clears readiness"), Mode->IsBackendDungeonDifficultyReady());
	TestFalse(TEXT("Late confirmation cannot revive an ended scope"), FRPGDungeonDifficultyTestAccess::Apply(Mode, Snapshot()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonDifficultyBlueprintTest, "ProjectRPG.Online.DungeonDifficulty.BlueprintContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonDifficultyBlueprintTest::RunTest(const FString&)
{
	const UClass* Class = ARPGGameModeBase::StaticClass();
	const auto* Event = FindFProperty<FMulticastDelegateProperty>(Class, TEXT("OnBackendDungeonDifficultyConfirmed"));
	TestTrue(TEXT("Server BP has an assignable confirmation event"), Event && Event->HasAnyPropertyFlags(CPF_BlueprintAssignable));
	for (const FName Name : {TEXT("IsBackendDungeonDifficultyReady"), TEXT("GetCurrentGameDifficulty")})
	{
		const UFunction* Function = Class->FindFunctionByName(Name);
		TestTrue(TEXT("BP can read readiness and confirmed difficulty"), Function && Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
	}
	const UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr,
		TEXT("/Game/Blueprints/GameMode/BP_BossBattleGameMode.BP_BossBattleGameMode"));
	if (TestNotNull(TEXT("Existing authored boss GameMode Blueprint"), Blueprint)
		&& TestNotNull(TEXT("Compiled boss GameMode class"), Blueprint->GeneratedClass.Get()))
	{
		const auto* Default = Cast<ARPGBossBattleGameMode>(Blueprint->GeneratedClass->GetDefaultObject());
		if (TestNotNull(TEXT("Authored BP inherits the guarded native boss lifecycle"), Default))
		{
			const UClass* BossClass = FRPGDungeonDifficultyTestAccess::BossClass(Default);
			TestTrue(TEXT("Existing boss BP is a native startup/difficulty consumer"),
				BossClass && BossClass->IsChildOf(ARPGBossCharacter::StaticClass()));
			if (BossClass) AddInfo(FString::Printf(TEXT("Backend boss Blueprint native path: %s -> %s"),
				*Blueprint->GetPathName(), *BossClass->GetPathName()));
		}
#if WITH_EDITOR
		// Ensure the existing authored BP does not also create actors outside the
		// shared native spawn gate. Read only: do not compile or save this asset.
		int32 SpawnActorNodes = 0;
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (const auto* Graph : Graphs)
			if (Graph) for (const UEdGraphNode* Node : Graph->Nodes)
				if (Node && Node->GetClass()->GetName().Contains(TEXT("SpawnActor"))) ++SpawnActorNodes;
		TestEqual(TEXT("Existing boss BP does not bypass its native creation path with a direct SpawnActor"), SpawnActorNodes, 0);
#endif
	}
	return true;
}

class FRPGDifficultyBossSpawnCommand final : public IAutomationLatentCommand
{
public:
	explicit FRPGDifficultyBossSpawnCommand(FAutomationTestBase* InTest) : Test(InTest) {}
	virtual bool Update() override
	{
		if (!Fixture)
		{
			Fixture = MakeUnique<FDifficultyWorld>(TEXT("/Script/Project_RPG.RPGBossRetryTestGameMode"));
			Mode = Cast<ARPGBossRetryTestGameMode>(Fixture->Mode);
			if (!Test->TestNotNull(TEXT("Production boss lifecycle fixture"), Mode)) return true;
			Mode->bOnlineDungeon = true; // only backend availability is simulated, not spawn/initialization
			Mode->SetGameDifficulty(ERPGGameDifficulty::Easy);
			Fixture->World->SpawnActor<ATargetPoint>();
			Fixture->World->CreateAISystem();
			Fixture->World->BeginPlay();
			Fixture->World->GetWorldSettings()->NotifyBeginPlay();
			Battle = Mode->GetGameState<ARPGBossBattleGameState>();
			if (!Test->TestNotNull(TEXT("Production battle state"), Battle)) return true;
			Test->TestNull(TEXT("No boss is spawned from the offline/default difficulty"), Battle->GetBattleSnapshot().Boss.Get());
			Test->TestEqual(TEXT("Battle remains waiting before configuration"), Mode->GetCurrentBossBattleState(), EBossBattleState::Waiting);
			Mode->OnBackendDungeonDifficultyConfirmed.Broadcast(ERPGGameDifficulty::Easy);
			Mode->SpawnBoss();
			Test->TestNull(TEXT("Unconfirmed event/direct spawn cannot bypass the gate"), Battle->GetBattleSnapshot().Boss.Get());
			auto Invalid = Snapshot(); Invalid.ServerId = TEXT("foreign-server");
			Test->TestFalse(TEXT("Foreign server cannot unblock the boss"), FRPGDungeonDifficultyTestAccess::Apply(Mode, Invalid));
			Test->TestNull(TEXT("Invalid assignment leaves boss absent"), Battle->GetBattleSnapshot().Boss.Get());
			if (!Test->TestTrue(TEXT("Confirmed assignment initializes production boss lifecycle"),
				FRPGDungeonDifficultyTestAccess::Apply(Mode, Snapshot()))) return true;
			Test->TestNull(TEXT("Server BP confirmation handlers finish before spawn"), Battle->GetBattleSnapshot().Boss.Get());
			Deadline = FPlatformTime::Seconds() + 15.0;
			return false;
		}
		if (FPlatformTime::Seconds() > Deadline)
		{
			Test->AddError(TEXT("Deferred difficulty boss startup timed out"));
			return true;
		}
		if (!Boss)
		{
			Fixture->World->GetTimerManager().Tick(.016f);
			Boss = Cast<ARPGBossStartupTestCharacter>(Battle->GetBattleSnapshot().Boss.Get());
			if (!Boss) return false;
			Boss->SpawnDefaultController(); // the reused test character deliberately disables auto-possession
			if (!Test->TestNotNull(TEXT("Real startup controller"), Boss->GetController())) return true;
		}
		if (!Boss->IsNPCStartUpDataReady()) return false;
		Test->TestEqual(TEXT("Hard difficulty reaches the actual NPC GAS grant as level 3"), Boss->TestData->LastApplyLevel, 3);
		Test->TestEqual(TEXT("Startup data is granted once"), Boss->TestData->GrantCount, 1);
		Test->TestEqual(TEXT("Ready notification is emitted once"), Boss->ReadyCount, 1);
		Test->TestTrue(TEXT("Duplicate same-difficulty response remains valid"), FRPGDungeonDifficultyTestAccess::Apply(Mode, Snapshot()));
		Mode->OnBackendDungeonDifficultyConfirmed.Broadcast(ERPGGameDifficulty::Hard);
		Mode->OnBackendDungeonDifficultyConfirmed.Broadcast(ERPGGameDifficulty::Hell);
		Test->TestTrue(TEXT("Duplicate/mismatched events preserve the same boss"), Battle->GetBattleSnapshot().Boss.Get() == Boss);
		Test->TestEqual(TEXT("Duplicate events never re-grant GAS startup"), Boss->TestData->GrantCount, 1);
		Test->TestEqual(TEXT("No player means no early combat start"), Mode->GetCurrentBossBattleState(), EBossBattleState::Waiting);
		Mode->EndPlay(EEndPlayReason::Quit);
		Test->TestFalse(TEXT("EndPlay releases the difficulty subscription"), Mode->OnBackendDungeonDifficultyConfirmed.IsBound());
		Mode->OnBackendDungeonDifficultyConfirmed.Broadcast(ERPGGameDifficulty::Hard);
		Mode->SpawnBoss();
		Test->TestTrue(TEXT("Late event/direct spawn after EndPlay cannot replace the boss"), Battle->GetBattleSnapshot().Boss.Get() == Boss);
		return true;
	}
private:
	FAutomationTestBase* Test;
	TUniquePtr<FDifficultyWorld> Fixture;
	ARPGBossRetryTestGameMode* Mode = nullptr;
	ARPGBossBattleGameState* Battle = nullptr;
	ARPGBossStartupTestCharacter* Boss = nullptr;
	double Deadline = 0.0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonDifficultyBossSpawnTest, "ProjectRPG.Online.DungeonDifficulty.BossSpawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonDifficultyBossSpawnTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FRPGDifficultyBossSpawnCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonDifficultyBossSpawnCancellationTest,
	"ProjectRPG.Online.DungeonDifficulty.BossSpawnCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonDifficultyBossSpawnCancellationTest::RunTest(const FString&)
{
	FDifficultyWorld Fixture(TEXT("/Script/Project_RPG.RPGBossRetryTestGameMode"));
	auto* Mode = Cast<ARPGBossRetryTestGameMode>(Fixture.Mode);
	if (!TestNotNull(TEXT("Cancellation boss lifecycle fixture"), Mode)) return false;
	Mode->bOnlineDungeon = true;
	Fixture.World->SpawnActor<ATargetPoint>();
	Fixture.World->BeginPlay(); Fixture.World->GetWorldSettings()->NotifyBeginPlay();
	auto* Battle = Mode->GetGameState<ARPGBossBattleGameState>();
	if (!TestNotNull(TEXT("Cancellation battle state"), Battle)) return false;
	TestTrue(TEXT("Valid confirmation queues the spawn"), FRPGDungeonDifficultyTestAccess::Apply(Mode, Snapshot()));
	TestNull(TEXT("Spawn is still deferred"), Battle->GetBattleSnapshot().Boss.Get());
	Mode->EndPlay(EEndPlayReason::Quit);
	Fixture.World->GetTimerManager().Tick(.016f);
	TestNull(TEXT("Ended scope cancels the queued spawn"), Battle->GetBattleSnapshot().Boss.Get());
	return true;
}
#endif
