#if WITH_DEV_AUTOMATION_TESTS
#include "Online/RPGDungeonServerDefinition.h"
#include "Online/RPGDungeonDefinition.h"
#include "Economy/RPGDungeonRewardDefinition.h"
#include "GameMode/RPGGameModeBase.h"
#include "GameInstance/RPGGameInstance.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"

struct FRPGDungeonServerDefinitionTestAccess
{
	static void Configure(ARPGGameModeBase* Mode, const TArray<TObjectPtr<URPGDungeonServerDefinition>>& Definitions)
	{ Mode->DungeonServerDefinitions = Definitions; }
	static bool Apply(ARPGGameModeBase* Mode, const FRPGDungeonSession& Session)
	{ return Mode->ApplyBackendDungeonConfiguration(Session, TEXT("content-session"), TEXT("content-server")); }
	static int32 Members(const ARPGGameModeBase* Mode) { return Mode->DungeonMemberCharacterIds.Num(); }
	static int64 Gold(const ARPGGameModeBase* Mode)
	{ return Mode->ConfirmedDungeonCurrencyChanges.IsEmpty() ? 0 : Mode->ConfirmedDungeonCurrencyChanges[0].Delta; }
	static const auto& Definitions(const ARPGGameModeBase* Mode) { return Mode->DungeonServerDefinitions; }
	static const auto& LegacyRewards(const ARPGGameModeBase* Mode) { return Mode->DungeonClearRewardsByDifficulty; }
};

namespace
{
	URPGDungeonRewardDefinition* Reward(int64 Amount = 500)
	{
		auto* Value = NewObject<URPGDungeonRewardDefinition>();
		Value->RewardVersion = TEXT("content_hard_v1");
		auto& Currency = Value->CurrencyChanges.AddDefaulted_GetRef();
		Currency.CurrencyCode = TEXT("RosterGold"); Currency.Delta = Amount;
		return Value;
	}
	URPGDungeonServerDefinition* Definition(const FString& Id = TEXT("Dungeon.Content"))
	{
		auto* Value = NewObject<URPGDungeonServerDefinition>();
		Value->DungeonId = Id;
		Value->DungeonMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/bossmap.bossmap")));
		Value->ClearRewardsByDifficulty.Add(ERPGGameDifficulty::Hard, Reward());
		return Value;
	}
	struct FContentWorld
	{
		UWorld* World = nullptr;
		ARPGGameModeBase* Mode = nullptr;
		FContentWorld()
		{
			// Unsaved fixture package on a normal content mount, not the read-only /Temp root.
			const FString PackageName = TEXT("/Game/__RPGDungeonContentTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(*FPackageName::GetShortName(PackageName)), CreatePackage(*PackageName));
			if (!World) return;
			auto* Instance = NewObject<URPGGameInstance>();
			auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World); Context.OwningGameInstance = Instance; World->SetGameInstance(Instance);
			FURL Url; Url.AddOption(TEXT("game=/Script/Project_RPG.RPGGameModeBase"));
			if (!World->SetGameMode(Url)) return;
			Mode = Cast<ARPGGameModeBase>(World->GetAuthGameMode());
			World->InitializeActorsForPlay(Url);
		}
		~FContentWorld() { if (World) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); } }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonServerDefinitionTest, "ProjectRPG.Online.DungeonServerContent.Definition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonServerDefinitionTest::RunTest(const FString&)
{
	auto* Value = Definition(); FString Error;
	TestTrue(TEXT("Trusted map/reward mapping validates"), Value->ValidateDefinition(Error));
	Value->DungeonId = TEXT(" Dungeon.Content");
	TestFalse(TEXT("IDs are not silently normalized"), Value->ValidateDefinition(Error));
	Value->DungeonId = TEXT("Dungeon.Content");
	Value->DungeonMap.Reset();
	TestFalse(TEXT("No map rejected"), Value->ValidateDefinition(Error));
	Value->DungeonMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/bossmap.Other")));
	TestFalse(TEXT("Map object must match its world package"), Value->ValidateDefinition(Error));
	Value->DungeonMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/bossmap.bossmap")));
	Value->ClearRewardsByDifficulty.Add(static_cast<ERPGGameDifficulty>(255), Reward());
	TestFalse(TEXT("Unknown difficulty rejected"), Value->ValidateDefinition(Error));
	Value->ClearRewardsByDifficulty.Remove(static_cast<ERPGGameDifficulty>(255));
	Value->ClearRewardsByDifficulty[ERPGGameDifficulty::Hard] = nullptr;
	TestFalse(TEXT("Missing reward never falls back"), Value->ValidateDefinition(Error));
	Value->ClearRewardsByDifficulty[ERPGGameDifficulty::Hard] = Reward();
	Value->ClearRewardsByDifficulty[ERPGGameDifficulty::Hard]->RewardVersion = TEXT("invalid version");
	TestFalse(TEXT("Invalid settlement payload rejected before admission"), Value->ValidateDefinition(Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonServerCatalogTest, "ProjectRPG.Online.DungeonServerContent.Catalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonServerCatalogTest::RunTest(const FString&)
{
	auto* A = Definition(); auto* B = Definition(TEXT("Dungeon.Variant"));
	B->ClearRewardsByDifficulty[ERPGGameDifficulty::Hard] = Reward(700);
	TArray<TObjectPtr<URPGDungeonServerDefinition>> Catalog = {A, B};
	FRPGDungeonServerSelection Selected; FString Error;
	TestTrue(TEXT("ID selects a different authored reward on the same map"), URPGDungeonServerDefinition::BuildAssignedSelection(
		Catalog, B->DungeonId, TEXT("/Game/Maps/bossmap"), ERPGGameDifficulty::Hard, Selected, Error));
	TestEqual(TEXT("Correct variant, not first row"), Selected.CurrencyChanges[0].Delta, int64(700));
	TestTrue(TEXT("PIE world package normalization only"), URPGDungeonServerDefinition::BuildAssignedSelection(
		Catalog, B->DungeonId, TEXT("/Game/Maps/UEDPIE_0_bossmap"), ERPGGameDifficulty::Hard, Selected, Error));
	for (int32 Case = 0; Case < 4; ++Case)
	{
		TestFalse(TEXT("Wrong ID/map/difficulty is never a legacy fallback"), URPGDungeonServerDefinition::BuildAssignedSelection(Catalog,
			Case == 0 ? TEXT("Dungeon.Unknown") : Case == 1 ? TEXT("dungeon.Variant") : B->DungeonId,
			Case == 2 ? TEXT("/Game/Maps/Other") : TEXT("/Game/Maps/bossmap"),
			Case == 3 ? ERPGGameDifficulty::Easy : ERPGGameDifficulty::Hard, Selected, Error));
		TestTrue(TEXT("Failure cannot retain a previous authoritative payload"), !Selected.Definition && Selected.CurrencyChanges.IsEmpty());
	}
	B->DungeonId = A->DungeonId;
	TestFalse(TEXT("Duplicate exact IDs reject entire server catalog"), URPGDungeonServerDefinition::ValidateCatalog(Catalog, Error));
	B->DungeonId = TEXT("Dungeon.Variant"); B->ClearRewardsByDifficulty.Reset();
	TestFalse(TEXT("An invalid unselected entry cannot be silently skipped"), URPGDungeonServerDefinition::BuildAssignedSelection(
		Catalog, A->DungeonId, TEXT("/Game/Maps/bossmap"), ERPGGameDifficulty::Hard, Selected, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonServerGameModeTest, "ProjectRPG.Online.DungeonServerContent.GameMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonServerGameModeTest::RunTest(const FString&)
{
	FContentWorld F;
	if (!TestNotNull(TEXT("Authoritative native mode"), F.Mode)) return false;
	auto* Asset = Definition(); Asset->DungeonMap = F.World;
	FRPGDungeonServerDefinitionTestAccess::Configure(F.Mode, {Asset});
	FRPGDungeonSession Session;
	Session.DungeonSessionId = TEXT("content-session"); Session.ServerId = TEXT("content-server");
	Session.DungeonId = Asset->DungeonId; Session.Difficulty = TEXT("Hard"); Session.State = TEXT("InProgress");
	Session.Members.AddDefaulted_GetRef().CharacterId = TEXT("original-member");
	auto Wrong = Session; Wrong.DungeonId = TEXT("Dungeon.Unknown");
	TestFalse(TEXT("Mismatched content cannot initialize admission"), FRPGDungeonServerDefinitionTestAccess::Apply(F.Mode, Wrong));
	TestFalse(TEXT("Wrong content leaves startup unready"), F.Mode->IsBackendDungeonDifficultyReady());
	TestEqual(TEXT("Wrong content does not alter members"), FRPGDungeonServerDefinitionTestAccess::Members(F.Mode), 0);
	Wrong = Session; Wrong.ServerId = TEXT("other-server");
	TestFalse(TEXT("Content does not bypass assigned server"), FRPGDungeonServerDefinitionTestAccess::Apply(F.Mode, Wrong));
	TestTrue(TEXT("Exact assignment/map/content/difficulty prepares admission"), FRPGDungeonServerDefinitionTestAccess::Apply(F.Mode, Session));
	TestEqual(TEXT("Content identity pinned"), F.Mode->GetConfirmedDungeonContentId(), Asset->DungeonId);
	TestEqual(TEXT("Payload pinned before gameplay"), FRPGDungeonServerDefinitionTestAccess::Gold(F.Mode), int64(500));
	auto* Hard = Asset->ClearRewardsByDifficulty[ERPGGameDifficulty::Hard].Get();
	Hard->CurrencyChanges[0].Delta = 999;
	TestTrue(TEXT("Same valid heartbeat retains original payload"), FRPGDungeonServerDefinitionTestAccess::Apply(F.Mode, Session));
	TestEqual(TEXT("In-place late content edits cannot retune reward"), FRPGDungeonServerDefinitionTestAccess::Gold(F.Mode), int64(500));
	Wrong = Session; Wrong.DungeonId = TEXT("Dungeon.Unknown"); Wrong.Members.AddDefaulted_GetRef().CharacterId = TEXT("foreign-member");
	TestFalse(TEXT("Later content conflict rejected"), FRPGDungeonServerDefinitionTestAccess::Apply(F.Mode, Wrong));
	TestEqual(TEXT("Conflict preserves original members"), FRPGDungeonServerDefinitionTestAccess::Members(F.Mode), 1);
	Asset->ClearRewardsByDifficulty[ERPGGameDifficulty::Hard] = Reward(200);
	TestFalse(TEXT("Replacement reward asset cannot retune a running session"), FRPGDungeonServerDefinitionTestAccess::Apply(F.Mode, Session));
	Asset->ClearRewardsByDifficulty[ERPGGameDifficulty::Hard] = Hard;
	Hard->RewardVersion = TEXT("changed_version");
	TestFalse(TEXT("Changed version rejected"), FRPGDungeonServerDefinitionTestAccess::Apply(F.Mode, Session));
	FRPGDungeonServerDefinitionTestAccess::Configure(F.Mode, {});
	TestFalse(TEXT("Removing trusted catalog cannot downgrade to legacy mid-run"), FRPGDungeonServerDefinitionTestAccess::Apply(F.Mode, Session));
	TestEqual(TEXT("All conflicts preserve the original reward"), FRPGDungeonServerDefinitionTestAccess::Gold(F.Mode), int64(500));
	F.Mode->EndPlay(EEndPlayReason::Quit);
	TestTrue(TEXT("EndPlay clears content snapshot"), F.Mode->GetConfirmedDungeonContentId().IsEmpty());
	TestEqual(TEXT("EndPlay clears reward snapshot"), FRPGDungeonServerDefinitionTestAccess::Gold(F.Mode), int64(0));
	TestFalse(TEXT("Late response cannot revive an ended scope"), FRPGDungeonServerDefinitionTestAccess::Apply(F.Mode, Session));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonServerAuthoredTest, "ProjectRPG.Online.DungeonServerContent.AuthoredDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonServerAuthoredTest::RunTest(const FString&)
{
	const auto* Asset = LoadObject<URPGDungeonServerDefinition>(nullptr,
		TEXT("/Game/System/Dungeons/DA_DungeonServer_RaidSwordMan.DA_DungeonServer_RaidSwordMan"));
	const auto* BP = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Blueprints/GameMode/BP_BossBattleGameMode.BP_BossBattleGameMode"));
	if (!TestTrue(TEXT("Saved default content and existing BP"), Asset && BP && BP->GeneratedClass)) return false;
	const auto* Mode = Cast<ARPGGameModeBase>(BP->GeneratedClass->GetDefaultObject());
	if (!TestNotNull(TEXT("Existing authoritative GameMode"), Mode)) return false;
	const auto& Definitions = FRPGDungeonServerDefinitionTestAccess::Definitions(Mode);
	TestTrue(TEXT("Existing GameMode bound to server-owned definition"), Definitions.Num() == 1 && Definitions[0] == Asset);
	FString Error;
	TestTrue(TEXT("Real definition validates"), Asset->ValidateDefinition(Error));
	TestEqual(TEXT("Real content ID"), Asset->DungeonId, FString(TEXT("Dungeon.RaidSwordMan")));
	TestEqual(TEXT("Existing boss map retained"), Asset->DungeonMap.ToSoftObjectPath().GetLongPackageName(), FString(TEXT("/Game/Maps/bossmap")));
	const auto* Client = LoadObject<URPGDungeonDefinition>(nullptr,
		TEXT("/Game/System/Dungeons/DA_Dungeon_RaidSwordMan.DA_Dungeon_RaidSwordMan"));
	if (!TestNotNull(TEXT("Separate client selection definition"), Client)) return false;
	TestEqual(TEXT("Separate authorities share identity"), Client->DungeonId, Asset->DungeonId);
	for (const auto Difficulty : Client->Difficulties)
	{
		const auto* RewardValue = Asset->ClearRewardsByDifficulty.Find(Difficulty);
		const auto* Legacy = FRPGDungeonServerDefinitionTestAccess::LegacyRewards(Mode).Find(Difficulty);
		TestTrue(TEXT("Existing authored difficulty rewards preserved, not regenerated"), RewardValue && Legacy && *RewardValue == *Legacy);
	}
	return true;
}
#endif
