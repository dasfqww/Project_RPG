#if WITH_DEV_AUTOMATION_TESTS
#include "Online/RPGDungeonDefinition.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "UI/Lobby/RPGOnlineLobbyDevelopmentProfile.h"
#include "Components/ComboBoxString.h"
#include "Engine/Engine.h"
#include "Engine/AssetManager.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/CommandLine.h"
#include "UObject/UnrealType.h"

struct FRPGDungeonSelectionTestAccess
{
	static void Configure(URPGOnlineLobbyWidget* Widget, const TArray<URPGDungeonDefinition*>& Definitions,
		const FString& Id, const FString& Difficulty = TEXT("Hard"))
	{
		Widget->DungeonDefinitions.Reset(); Widget->LoadedDungeonDefinitions.Reset(); Widget->DungeonDefinitionsLoadError.Reset();
		for (auto* Definition : Definitions)
		{
			Widget->DungeonDefinitions.Add(TSoftObjectPtr<URPGDungeonDefinition>(Definition));
			Widget->LoadedDungeonDefinitions.Add(Definition);
		}
		Widget->DungeonId = Id; Widget->Difficulty = Difficulty;
	}
	static void MissingReference(URPGOnlineLobbyWidget* Widget)
	{
		Widget->DungeonDefinitions = {TSoftObjectPtr<URPGDungeonDefinition>()};
		Widget->LoadDungeonDefinitions();
	}
	static void Refresh(URPGOnlineLobbyWidget* Widget) { Widget->RefreshPresentation(); }
	static void BindFlow(URPGOnlineLobbyWidget* Widget, URPGDungeonSessionSubsystem* Flow) { Widget->Flow = Flow; }
	static FString Preference(const URPGOnlineLobbyWidget* Widget) { return Widget->DungeonId; }
	static void Reload(URPGOnlineLobbyWidget* Widget) { Widget->LoadDungeonDefinitions(); }
	static FString Preferences(const URPGOnlineLobbyWidget* Widget)
	{
		FString Result = Widget->DungeonId + TEXT("|") + Widget->Difficulty;
		for (const auto& Reference : Widget->DungeonDefinitions) Result += TEXT("|") + Reference.ToSoftObjectPath().ToString();
		return Result;
	}
	static bool LoadedAssetIs(const URPGOnlineLobbyWidget* Widget, const URPGDungeonDefinition* Asset)
	{
		return Widget->DungeonDefinitions.Num() == 1 && Widget->LoadedDungeonDefinitions.Num() == 1
			&& Widget->DungeonDefinitions[0].Get() == Asset && Widget->LoadedDungeonDefinitions[0] == Asset;
	}
};

namespace
{
	URPGDungeonDefinition* Definition(const FString& Id, TArray<ERPGGameDifficulty> Difficulties =
		{ERPGGameDifficulty::Hard, ERPGGameDifficulty::Hell}, ERPGGameDifficulty Default = ERPGGameDifficulty::Hard)
	{
		auto* Value = NewObject<URPGDungeonDefinition>();
		Value->DungeonId = Id; Value->DisplayName = FText::FromString(TEXT("Same display name"));
		Value->Difficulties = MoveTemp(Difficulties); Value->DefaultDifficulty = Default;
		return Value;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonDefinitionValidationTest, "ProjectRPG.Online.DungeonSelection.Definition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonDefinitionValidationTest::RunTest(const FString&)
{
	FString Error;
	auto* Value = Definition(TEXT("Dungeon.RaidSwordMan"));
	TestTrue(TEXT("Authored selection validates without HTTP"), Value->ValidateSelection(Error));
	for (const FString& Id : {FString(TEXT("")), FString(TEXT(" Dungeon.A")), FString(TEXT("Dungeon.A ")),
		FString(TEXT("Dungeon/Map")), FString(TEXT("Dungeon?ticket=value")), FString(TEXT("던전")), FString::ChrN(65, 'a')})
	{
		Value->DungeonId = Id;
		TestFalse(TEXT("Malformed IDs never normalize to a valid default"), Value->ValidateSelection(Error));
	}
	Value->DungeonId = FString::ChrN(64, 'a');
	TestTrue(TEXT("Maximum backend identifier length"), Value->ValidateSelection(Error));
	Value->DisplayName = FText::FromString(TEXT("  "));
	TestFalse(TEXT("Empty authored label rejected"), Value->ValidateSelection(Error));
	Value->DisplayName = FText::FromString(TEXT("Dungeon"));
	Value->Difficulties.Reset();
	TestFalse(TEXT("No supported difficulties rejected"), Value->ValidateSelection(Error));
	Value->Difficulties = {ERPGGameDifficulty::Hard, ERPGGameDifficulty::Hard};
	TestFalse(TEXT("Duplicate difficulties rejected"), Value->ValidateSelection(Error));
	Value->Difficulties = {static_cast<ERPGGameDifficulty>(255)};
	TestFalse(TEXT("Unknown enum rejected"), Value->ValidateSelection(Error));
	Value->Difficulties = {ERPGGameDifficulty::Easy};
	TestFalse(TEXT("Default must belong to this dungeon"), Value->ValidateSelection(Error));
	Value->DefaultDifficulty = ERPGGameDifficulty::Easy;
	TestTrue(TEXT("Single-difficulty variation valid"), Value->ValidateSelection(Error));
	TestTrue(TEXT("Successful validation clears stale errors"), Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonSelectionCatalogTest, "ProjectRPG.Online.DungeonSelection.Catalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonSelectionCatalogTest::RunTest(const FString&)
{
	auto* Widget = NewObject<URPGOnlineLobbyWidget>();
	auto* A = Definition(TEXT("Dungeon.A"));
	auto* B = Definition(TEXT("Dungeon.B"), {ERPGGameDifficulty::Easy, ERPGGameDifficulty::Normal}, ERPGGameDifficulty::Normal);
	FRPGDungeonSelectionTestAccess::Configure(Widget, {A, B}, A->DungeonId);
	TestEqual(TEXT("Two variations share one frontend"), Widget->GetDungeonOptions().Num(), 2);
	TestTrue(TEXT("Options are exact IDs, not ambiguous display names"), Widget->GetDungeonOptions()[1].Equals(B->DungeonId, ESearchCase::CaseSensitive));
	TestTrue(TEXT("Authored difficulty restrictions"), Widget->GetAvailableDungeonDifficultyOptions() == A->Difficulties);
	FString SelectedId;
	TestTrue(TEXT("Exact preference resolves"), Widget->GetSelectedDungeonId(SelectedId) && SelectedId == A->DungeonId);
	FRPGDungeonSelectionTestAccess::Configure(Widget, {A, B}, TEXT("dungeon.A"));
	TestFalse(TEXT("Case differences cannot select another ID"), Widget->GetSelectedDungeonId(SelectedId));
	TestTrue(TEXT("Invalid getter clears prior output"), SelectedId.IsEmpty());
	FRPGDungeonSelectionTestAccess::Configure(Widget, {A, B}, B->DungeonId);
	ERPGGameDifficulty Difficulty = ERPGGameDifficulty::Hell;
	TestFalse(TEXT("Another dungeon's unsupported Hard is not silently accepted"), Widget->GetSelectedDungeonDifficulty(Difficulty));
	TestTrue(TEXT("Rejected output preserves previous enum"), Difficulty == ERPGGameDifficulty::Hell);
	FRPGDungeonSelectionTestAccess::Configure(Widget, {A, B}, B->DungeonId, TEXT("Normal"));
	TestTrue(TEXT("Authored supported choice resolves"), Widget->GetSelectedDungeonDifficulty(Difficulty) && Difficulty == ERPGGameDifficulty::Normal);
	B->DungeonId = A->DungeonId;
	TestTrue(TEXT("Duplicate IDs invalidate the entire configured catalog"), Widget->GetDungeonOptions().IsEmpty());
	TestFalse(TEXT("Duplicate IDs cannot fall back to legacy preference"), Widget->GetSelectedDungeonId(SelectedId));
	B->DungeonId = TEXT("Dungeon.B"); B->Difficulties.Reset();
	TestTrue(TEXT("One invalid definition invalidates all options"), Widget->GetDungeonOptions().IsEmpty());
	FRPGDungeonSelectionTestAccess::MissingReference(Widget);
	TestTrue(TEXT("Unavailable configured reference never falls back"), Widget->GetDungeonOptions().IsEmpty());
	FRPGDungeonSelectionTestAccess::Configure(Widget, {}, TEXT("Dungeon.Legacy"));
	TestEqual(TEXT("Empty configuration preserves one legacy dungeon"), Widget->GetDungeonOptions().Num(), 1);
	TestEqual(TEXT("Legacy supports original four difficulties"), Widget->GetAvailableDungeonDifficultyOptions().Num(), 4);
	TestFalse(TEXT("No authentication/context means no selection mutation"), Widget->SelectDungeon(TEXT("Dungeon.Legacy")));
	TestFalse(TEXT("No context means no create HTTP"), Widget->CreateSelectedDungeonSession());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonSelectionWidgetTest, "ProjectRPG.Online.DungeonSelection.Widget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonSelectionWidgetTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Selection world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Widget = CreateWidget<URPGOnlineLobbyWidget>(World, URPGOnlineLobbyWidget::StaticClass());
	if (!TestNotNull(TEXT("Native selection widget"), Widget)) return false;
	UComboBoxString* Dungeons = nullptr;
	{
		const auto Slate = Widget->TakeWidget();
		auto* A = Definition(TEXT("Dungeon.A"));
		auto* B = Definition(TEXT("Dungeon.B"), {ERPGGameDifficulty::Easy}, ERPGGameDifficulty::Easy);
		FRPGDungeonSelectionTestAccess::Configure(Widget, {A, B}, A->DungeonId);
		FRPGDungeonSelectionTestAccess::Refresh(Widget);
		Dungeons = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("DungeonSelectionOptions")));
		auto* Difficulties = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("DungeonDifficulty")));
		if (!TestTrue(TEXT("Actual selectors constructed"), Dungeons && Difficulties)) return false;
		TestEqual(TEXT("Both same-named definitions remain distinct"), Dungeons->GetOptionCount(), 2);
		TestEqual(TEXT("Only current dungeon's difficulties are offered"), Difficulties->GetOptionCount(), 2);
		const FString Before = Dungeons->GetSelectedOption();
		Dungeons->SetSelectedOption(Dungeons->GetOptionAtIndex(1));
		TestEqual(TEXT("Forced disabled dispatcher restores trusted selection"), Dungeons->GetSelectedOption(), Before);
		TestFalse(TEXT("No local auth/roster context keeps selector locked"), Dungeons->GetIsEnabled());
		auto* Instance = NewObject<UGameInstance>();
		auto* Flow = NewObject<URPGDungeonSessionSubsystem>(Instance);
		FRPGDungeonSelectionTestAccess::BindFlow(Widget, Flow);
		const auto* Property = FindFProperty<FStructProperty>(Flow->GetClass(), TEXT("CurrentDungeonSession"));
		if (!TestNotNull(TEXT("Existing isolated session fixture state"), Property)) return false;
		auto& Session = *Property->ContainerPtrToValuePtr<FRPGDungeonSession>(Flow);
		Session.DungeonSessionId = TEXT("fixture-session"); Session.DungeonId = TEXT("Dungeon.ServerOnly"); Session.Difficulty = TEXT("Normal");
		for (const FString State : {TEXT("Waiting"), TEXT("Loading"), TEXT("InProgress"), TEXT("SettlementPending"), TEXT("Unknown")})
		{
			Session.State = State;
			Dungeons->OnSelectionChanged.Broadcast(Dungeons->GetOptionAtIndex(0), ESelectInfo::Direct);
			FString Id;
			TestTrue(TEXT("Unknown local content still displays exact backend dungeon"), Widget->GetSelectedDungeonId(Id) && Id == Session.DungeonId);
			TestEqual(TEXT("Server difficulty displayed outside local definition"), Difficulties->GetSelectedOption(), Session.Difficulty);
			TestFalse(TEXT("Session is not editable through BP"), Widget->SelectDungeon(B->DungeonId));
			TestEqual(TEXT("Original next-session preference retained"), FRPGDungeonSelectionTestAccess::Preference(Widget), A->DungeonId);
		}
		FRPGDungeonSelectionTestAccess::MissingReference(Widget);
		FRPGDungeonSelectionTestAccess::Refresh(Widget);
		FString Id;
		TestTrue(TEXT("Broken catalog cannot erase existing server identity"), Widget->GetSelectedDungeonId(Id) && Id == Session.DungeonId);
		Session = FRPGDungeonSession();
		FRPGDungeonSelectionTestAccess::Refresh(Widget);
		TestEqual(TEXT("Invalid catalog offers no selectable replacement"), Dungeons->GetOptionCount(), 0);
		TestEqual(TEXT("Invalid catalog offers no default difficulty"), Difficulties->GetOptionCount(), 0);
	}
	TestFalse(TEXT("Destruction unbinds selector delegate"), Dungeons->OnSelectionChanged.IsBound());
	{
		const auto Slate = Widget->TakeWidget();
		TestTrue(TEXT("Reconstruction binds selector once"), Dungeons->OnSelectionChanged.IsBound());
	}
	TestFalse(TEXT("Repeated teardown releases selector"), Dungeons->OnSelectionChanged.IsBound());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonSelectionBlueprintTest, "ProjectRPG.Online.DungeonSelection.BlueprintContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonSelectionBlueprintTest::RunTest(const FString&)
{
	for (const FName Name : {TEXT("GetDungeonOptions"), TEXT("GetDungeonDisplayName"), TEXT("GetSelectedDungeonId"),
		TEXT("CanSelectDungeon"), TEXT("GetAvailableDungeonDifficultyOptions")})
	{
		const UFunction* Function = URPGOnlineLobbyWidget::StaticClass()->FindFunctionByName(Name);
		TestTrue(TEXT("Read-only BP API does not issue HTTP"), Function && Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
	}
	const UFunction* Select = URPGOnlineLobbyWidget::StaticClass()->FindFunctionByName(TEXT("SelectDungeon"));
	TestTrue(TEXT("Explicit guarded BP selection"), Select && Select->HasAnyFunctionFlags(FUNC_BlueprintCallable));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonSelectionRegisteredAssetTest, "ProjectRPG.Online.DungeonSelection.RegisteredAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonSelectionRegisteredAssetTest::RunTest(const FString&)
{
	const FString Id = TEXT("Dungeon.RaidSwordMan");
	const auto* Asset = LoadObject<URPGDungeonDefinition>(nullptr,
		TEXT("/Game/System/Dungeons/DA_Dungeon_RaidSwordMan.DA_Dungeon_RaidSwordMan"));
	if (!TestNotNull(TEXT("Real saved default definition"), Asset)) return false;
	FString Error;
	TestTrue(TEXT("Saved definition passes the production validator"), Asset->ValidateSelection(Error));
	TestEqual(TEXT("Existing dungeon identity is unchanged"), Asset->DungeonId, Id);
	TestFalse(TEXT("Authored display name is not a fallback ID"), Asset->DisplayName.ToString().Equals(Id, ESearchCase::CaseSensitive));
	TestEqual(TEXT("Existing four difficulties preserved"), Asset->Difficulties.Num(), 4);
	TestTrue(TEXT("Existing default preserved"), Asset->DefaultDifficulty == ERPGGameDifficulty::Hard);
	const FPrimaryAssetId PrimaryId = Asset->GetPrimaryAssetId();
	FAssetData Data;
	TestEqual(TEXT("Primary asset type"), PrimaryId.PrimaryAssetType.ToString(), FString(TEXT("RPGDungeonDefinition")));
	TestTrue(TEXT("Saved definition discovered by the actual Asset Manager rule"), UAssetManager::Get().GetPrimaryAssetData(PrimaryId, Data));
	TestTrue(TEXT("Cook rule configured, not a packaged-cook execution"), UAssetManager::Get().GetPrimaryAssetRules(PrimaryId).CookRule == EPrimaryAssetCookRule::AlwaysCook);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Configured lobby fixture world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Widget = CreateWidget<URPGOnlineLobbyWidget>(World, URPGOnlineLobbyWidget::StaticClass());
	if (!TestNotNull(TEXT("Configured native lobby"), Widget)) return false;
	const auto Slate = Widget->TakeWidget(); // production NativeConstruct loads the actual Config soft reference
	TestTrue(TEXT("No transient fixture injection; Config loaded the saved object"), FRPGDungeonSelectionTestAccess::LoadedAssetIs(Widget, Asset));
	FString SelectedId;
	TestEqual(TEXT("One real default, no invented extra dungeon"), Widget->GetDungeonOptions().Num(), 1);
	TestTrue(TEXT("Default selection resolves from registered definition"), Widget->GetSelectedDungeonId(SelectedId) && SelectedId == Id);
	TestTrue(TEXT("Actual widget uses authored label"), Widget->GetDungeonDisplayName(Id).EqualTo(Asset->DisplayName));
	TestTrue(TEXT("Actual widget uses authored difficulties"), Widget->GetAvailableDungeonDifficultyOptions() == Asset->Difficulties);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonDevelopmentProfilePolicyTest,
	"ProjectRPG.Online.DungeonDevelopmentProfile.Policy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonDevelopmentProfilePolicyTest::RunTest(const FString&)
{
	using namespace RPGOnlineLobbyDevelopmentProfile;
	TestTrue(TEXT("No opt-in preserves normal selection"), !Resolve(TEXT("-nosteam"), false).bTraining
		&& Resolve(TEXT("-nosteam"), false).Error.IsEmpty());
	TestTrue(TEXT("Exact training profile accepted behind development gate"), Resolve(TEXT("-RPGDungeonProfile=Training"), true).bTraining);
	TestTrue(TEXT("Quoted operator value accepted"), Resolve(TEXT("-RPGDungeonProfile=\"Training\""), true).bTraining);
	for (const TCHAR* Command : {TEXT("-RPGDungeonProfile"), TEXT("-RPGDungeonProfile="),
		TEXT("-RPGDungeonProfile=training"), TEXT("-RPGDungeonProfile=../Training"),
		TEXT("-RPGDungeonProfile=Intro1"), TEXT("-RPGDungeonProfile=Training -RPGDungeonProfile=Training")})
	{
		const auto Invalid = Resolve(Command, true);
		TestTrue(TEXT("Malformed, unknown or repeated opt-in never falls back"), !Invalid.bTraining && !Invalid.Error.IsEmpty());
	}
	TestTrue(TEXT("Shipping/non-development gate rejects even a valid profile"),
		!Resolve(TEXT("-RPGDungeonProfile=Training"), false).Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonDevelopmentProfileWidgetTest,
	"ProjectRPG.Online.DungeonDevelopmentProfile.Widget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonDevelopmentProfileWidgetTest::RunTest(const FString&)
{
	const FString OriginalCommand = FCommandLine::Get();
	ON_SCOPE_EXIT { FCommandLine::Set(*OriginalCommand); };
	const FString DefaultBefore = FRPGDungeonSelectionTestAccess::Preferences(GetDefault<URPGOnlineLobbyWidget>());
	FCommandLine::Set(TEXT("-RPGDungeonProfile=Training -RPGDevelopmentLogin -nosteam"));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Development profile world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Widget = CreateWidget<URPGOnlineLobbyWidget>(World, URPGOnlineLobbyWidget::StaticClass());
	if (!TestNotNull(TEXT("Actual native training lobby"), Widget)) return false;
	const auto Slate = Widget->TakeWidget(); // real NativeConstruct and configured asset loading
	FString Id;
	TestTrue(TEXT("Only saved Training definition is offered"), Widget->GetDungeonOptions() == TArray<FString>{TEXT("Progression.Training")});
	TestTrue(TEXT("Training is the instance's valid creation preference"), Widget->GetSelectedDungeonId(Id) && Id == TEXT("Progression.Training"));
	TestTrue(TEXT("Only Normal is offered"), Widget->GetAvailableDungeonDifficultyOptions() == TArray<ERPGGameDifficulty>{ERPGGameDifficulty::Normal});
	auto* Dungeons = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("DungeonSelectionOptions")));
	auto* Difficulties = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("DungeonDifficulty")));
	if (!TestTrue(TEXT("Native selectors exist"), Dungeons && Difficulties)) return false;
	TestEqual(TEXT("Actual training list has one option"), Dungeons->GetOptionCount(), 1);
	TestEqual(TEXT("Actual difficulty is Normal"), Difficulties->GetSelectedOption(), FString(TEXT("Normal")));
	TestFalse(TEXT("A profile does not supply authentication or issue create HTTP"), Widget->CreateSelectedDungeonSession());
	const FString InstanceBefore = FRPGDungeonSelectionTestAccess::Preferences(Widget);
	FRPGDungeonSelectionTestAccess::Reload(Widget);
	TestEqual(TEXT("Reload is idempotent"), FRPGDungeonSelectionTestAccess::Preferences(Widget), InstanceBefore);
	auto* Instance = NewObject<UGameInstance>();
	auto* Flow = NewObject<URPGDungeonSessionSubsystem>(Instance);
	FRPGDungeonSelectionTestAccess::BindFlow(Widget, Flow);
	const auto* Property = FindFProperty<FStructProperty>(Flow->GetClass(), TEXT("CurrentDungeonSession"));
	if (!TestNotNull(TEXT("Existing isolated membership fixture"), Property)) return false;
	auto& Session = *Property->ContainerPtrToValuePtr<FRPGDungeonSession>(Flow);
	Session.DungeonSessionId = TEXT("profile-fixture-session"); Session.DungeonId = TEXT("Dungeon.Existing");
	Session.Difficulty = TEXT("Hard"); Session.State = TEXT("InProgress");
	FRPGDungeonSelectionTestAccess::Reload(Widget);
	TestTrue(TEXT("Unfinished backend session remains the displayed exact selection"), Widget->GetSelectedDungeonId(Id) && Id == Session.DungeonId);
	TestEqual(TEXT("Profile never rewrites an existing session's difficulty"), Session.Difficulty, FString(TEXT("Hard")));
	TestFalse(TEXT("An unfinished session is not editable via the profile"), Widget->SelectDungeon(TEXT("Progression.Training")));
	FRPGDungeonSelectionTestAccess::BindFlow(Widget, nullptr);
	FCommandLine::Set(TEXT("-RPGDungeonProfile=Unknown -RPGDevelopmentLogin -nosteam"));
	FRPGDungeonSelectionTestAccess::Reload(Widget);
	TestTrue(TEXT("Bad profile cannot fall back to legacy or prior Training options"), Widget->GetDungeonOptions().IsEmpty());
	FCommandLine::Set(TEXT("-RPGDungeonProfile=Training -nosteam"));
	FRPGDungeonSelectionTestAccess::Reload(Widget);
	TestTrue(TEXT("Missing development login gate disables the profile"), Widget->GetDungeonOptions().IsEmpty());
	FCommandLine::Set(TEXT("-nosteam"));
	auto* Normal = NewObject<URPGOnlineLobbyWidget>();
	FRPGDungeonSelectionTestAccess::Reload(Normal);
	TestTrue(TEXT("Unselected profile retains the saved default boss catalog"), Normal->GetDungeonOptions() == TArray<FString>{TEXT("Dungeon.RaidSwordMan")});
	TestEqual(TEXT("CDO and serialized config defaults are untouched"),
		FRPGDungeonSelectionTestAccess::Preferences(GetDefault<URPGOnlineLobbyWidget>()), DefaultBefore);
	return true;
}
#endif
