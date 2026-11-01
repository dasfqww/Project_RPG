#if WITH_DEV_AUTOMATION_TESTS
#include "AI/RPGBossTelegraphPresentationComponent.h"
#include "Ability/RPGBossTelegraphedStrikeAbility.h"
#include "Character/RPGBossCharacter.h"
#include "Tests/RPGBossTestFixture.h"
#include "Components/DecalComponent.h"
#include "Components/Button.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "UI/RPGBossBattleRetryBinding.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossPresentationTest, "ProjectRPG.Boss.Presentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossPresentationTest::RunTest(const FString& Parameters)
{
	// Read the runtime template, without constructing UI or firing the legacy
	// Blueprint callbacks. The controller's retry RPC binding needs this button.
	UClass* DefeatClass = LoadClass<UUserWidget>(nullptr, TEXT(
		"/Game/Blueprints/UI/GameContents/BossBattle/WBP_DefeatWidget.WBP_DefeatWidget_C"));
	const UWidgetBlueprintGeneratedClass* DefeatBP = Cast<UWidgetBlueprintGeneratedClass>(DefeatClass);
	if (!TestNotNull(TEXT("Authored defeat widget class"), DefeatBP)) { return false; }
	const UWidgetTree* DefeatTree = DefeatBP->GetWidgetTreeArchetype();
	if (!TestNotNull(TEXT("Defeat widget runtime template"), DefeatTree)) { return false; }
	UWidget* RetryTemplate = DefeatTree->FindWidget(TEXT("RetryButton"));
	if (!TestNotNull(TEXT("Native retry binding finds authored wrapper"), RetryTemplate) ||
		!TestNotNull(TEXT("Authored retry dispatcher has a supported signature"), RPGBossBattleUI::FindRetryClick(RetryTemplate)))
	{
		return false;
	}
	// Duplicate only the template. Do not initialize UI or broadcast a click/RPC.
	UWidget* RetryCopy = DuplicateObject<UWidget>(RetryTemplate, GetTransientPackage());
	ARPGPlayerController* Receiver = GetMutableDefault<ARPGPlayerController>();
	FMulticastDelegateProperty* RetryEvent = RPGBossBattleUI::FindRetryClick(RetryCopy);
	if (!TestNotNull(TEXT("Copied retry dispatcher"), RetryEvent)) { return false; }
	FScriptDelegate LegacyHandler;
	LegacyHandler.BindUFunction(DefeatClass->GetDefaultObject(),
		GET_FUNCTION_NAME_CHECKED(UWidget, RemoveFromParent));
	RetryEvent->AddDelegate(LegacyHandler, RetryCopy);
	TestTrue(TEXT("Authored wrapper accepts native retry binding"), RPGBossBattleUI::BindRetryButton(RetryCopy, Receiver));
	const FMulticastScriptDelegate* BoundEvent = RetryEvent->GetMulticastDelegate(
		RetryEvent->ContainerPtrToValuePtr<void>(RetryCopy));
	if (!TestNotNull(TEXT("Bound retry dispatcher"), BoundEvent)) { return false; }
	TestFalse(TEXT("Legacy navigation binding is removed"), BoundEvent->Contains(LegacyHandler));
	TestTrue(TEXT("Wrapper forwards to the existing controller retry entry"), BoundEvent->Contains(
		Receiver, GET_FUNCTION_NAME_CHECKED(ARPGPlayerController, RequestBossBattleRestart)));
	TestTrue(TEXT("Rebinding is idempotent"), RPGBossBattleUI::BindRetryButton(RetryCopy, Receiver));
	TestEqual(TEXT("Only one retry handler remains"), BoundEvent->GetAllObjects().Num(), 1);
	UButton* PlainButton = NewObject<UButton>();
	TestTrue(TEXT("Plain UButton remains supported"), RPGBossBattleUI::BindRetryButton(PlainButton, Receiver));
	TestFalse(TEXT("Missing retry widget is rejected"), RPGBossBattleUI::BindRetryButton(nullptr, Receiver));
	UWidget* ExitTemplate = DefeatTree->FindWidget(TEXT("ExitButton"));
	if (!TestNotNull(TEXT("Authored defeat exit wrapper"), ExitTemplate)) { return false; }
	FMulticastDelegateProperty* TemplateExitEvent = RPGBossBattleUI::FindRetryClick(ExitTemplate);
	if (!TestNotNull(TEXT("Authored exit dispatcher has a supported signature"), TemplateExitEvent)) { return false; }
	const FMulticastScriptDelegate OriginalExitBindings = *TemplateExitEvent->GetMulticastDelegate(
		TemplateExitEvent->ContainerPtrToValuePtr<void>(ExitTemplate));
	UWidget* ExitCopy = DuplicateObject<UWidget>(ExitTemplate, GetTransientPackage());
	FMulticastDelegateProperty* ExitEvent = RPGBossBattleUI::FindRetryClick(ExitCopy);
	ExitEvent->AddDelegate(LegacyHandler, ExitCopy);
	TestTrue(TEXT("Authored exit wrapper accepts guarded native return"), RPGBossBattleUI::BindReturnButton(ExitCopy, Receiver));
	const FMulticastScriptDelegate* BoundExit = ExitEvent->GetMulticastDelegate(
		ExitEvent->ContainerPtrToValuePtr<void>(ExitCopy));
	TestFalse(TEXT("Legacy direct map travel is removed from the instance"), BoundExit->Contains(LegacyHandler));
	TestTrue(TEXT("Exit forwards to the guarded return dispatcher adapter"), BoundExit->Contains(
		Receiver, GET_FUNCTION_NAME_CHECKED(ARPGPlayerController, RequestDungeonReturnFromResult)));
	TestTrue(TEXT("Return rebinding is idempotent"), RPGBossBattleUI::BindReturnButton(ExitCopy, Receiver));
	TestEqual(TEXT("Only one return handler remains"), BoundExit->GetAllObjects().Num(), 1);
	TestFalse(TEXT("A bool-return function cannot bind to a void click dispatcher"), RPGBossBattleUI::BindResultButton(
		ExitCopy, Receiver, GET_FUNCTION_NAME_CHECKED(ARPGPlayerController, RequestDungeonReturnToLobby)));
	TestTrue(TEXT("An incompatible bind cannot clear the valid handler"), BoundExit->Contains(
		Receiver, GET_FUNCTION_NAME_CHECKED(ARPGPlayerController, RequestDungeonReturnFromResult)));
	TestTrue(TEXT("Binding a copy leaves the authored template unchanged"), TemplateExitEvent->Identical(
		&OriginalExitBindings, TemplateExitEvent->ContainerPtrToValuePtr<void>(ExitTemplate), 0));
	TestTrue(TEXT("Plain UButton supports guarded return"), RPGBossBattleUI::BindReturnButton(PlainButton, Receiver));
	TestFalse(TEXT("Missing exit widget is rejected"), RPGBossBattleUI::BindReturnButton(nullptr, Receiver));
	TestFalse(TEXT("Missing exit receiver is rejected"), RPGBossBattleUI::BindReturnButton(PlainButton, nullptr));
	UClass* BossClass = LoadClass<ARPGBossCharacter>(nullptr,
		TEXT("/Game/Blueprints/Character/NPC/SwordMan/BP_RaidSwordMan.BP_RaidSwordMan_C"));
	if (!TestNotNull(TEXT("Saved raid boss"), BossClass)) { return false; }
	const URPGBossTelegraphPresentationComponent* Defaults = BossClass->GetDefaultObject<ARPGBossCharacter>()->BossTelegraphPresentation;
	if (!TestNotNull(TEXT("Boss presentation component"), Defaults) ||
		!TestEqual(TEXT("Two authored attack styles"), Defaults->Styles.Num(), 2)) { return false; }
	const TCHAR* AttackPaths[] = {
		TEXT("/Game/Blueprints/Character/NPC/SwordMan/GameplayAbility/GA_RaidSwordMan_Strike.GA_RaidSwordMan_Strike_C"),
		TEXT("/Game/Blueprints/Character/NPC/SwordMan/GameplayAbility/GA_RaidSwordMan_HeavyStrike.GA_RaidSwordMan_HeavyStrike_C")};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UClass* AttackClass = LoadClass<URPGBossTelegraphedStrikeAbility>(nullptr, AttackPaths[Index]);
		if (!TestNotNull(TEXT("Styled server attack"), AttackClass)) { return false; }
		const URPGBossTelegraphedStrikeAbility* Attack = AttackClass->GetDefaultObject<URPGBossTelegraphedStrikeAbility>();
		TestTrue(TEXT("Presentation uses server action tag"), Defaults->Styles[Index].ActionTag == Attack->ActionTag);
		TestEqual(TEXT("Presentation radius matches server query"), Defaults->Styles[Index].Radius, Attack->Radius);
		UMaterialInterface* Material = Defaults->Styles[Index].Material.LoadSynchronous();
		if (!TestNotNull(TEXT("Saved decal material"), Material)) { return false; }
		TestTrue(TEXT("Decal material domain"), Material->GetMaterial()->MaterialDomain == MD_DeferredDecal);
	}

	FRPGBossState State;
	State.EncounterId = 7;
	State.EncounterState = ERPGBossEncounterState::InCombat;
	FRPGBossTelegraphState Telegraph;
	Telegraph.bActive = true;
	Telegraph.EncounterId = 7;
	Telegraph.Sequence = 3;
	Telegraph.ActionTag = Defaults->Styles[0].ActionTag;
	Telegraph.StartServerTime = 100.0;
	Telegraph.EndServerTime = 110.0;
	float Progress = -1.f;
	TestTrue(TEXT("Late join reconstructs active windup"), URPGBossTelegraphPresentationComponent::EvaluateProgress(State, Telegraph, 105.0, Progress));
	TestEqual(TEXT("Late join starts halfway, not at zero"), Progress, .5f);
	TestTrue(TEXT("Clock correction before start is clamped"), URPGBossTelegraphPresentationComponent::EvaluateProgress(State, Telegraph, 99.0, Progress));
	TestEqual(TEXT("Progress is not negative"), Progress, 0.f);
	TestFalse(TEXT("Expired snapshot is hidden even before clear arrives"), URPGBossTelegraphPresentationComponent::EvaluateProgress(State, Telegraph, 110.0, Progress));
	State.EncounterId = 8;
	TestFalse(TEXT("Old encounter snapshot is hidden"), URPGBossTelegraphPresentationComponent::EvaluateProgress(State, Telegraph, 105.0, Progress));
	State.EncounterId = 7;
	State.EncounterState = ERPGBossEncounterState::Wiped;
	TestFalse(TEXT("Wipe hides active snapshot"), URPGBossTelegraphPresentationComponent::EvaluateProgress(State, Telegraph, 105.0, Progress));
	State.EncounterState = ERPGBossEncounterState::InCombat;
	TestFalse(TEXT("Invalid clock is rejected"), URPGBossTelegraphPresentationComponent::EvaluateProgress(State, Telegraph,
		std::numeric_limits<double>::quiet_NaN(), Progress));
	Telegraph.EndServerTime = Telegraph.StartServerTime;
	TestFalse(TEXT("Zero duration is rejected"), URPGBossTelegraphPresentationComponent::EvaluateProgress(State, Telegraph, 100.0, Progress));

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Presentation test world"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();
	ARPGBossTestPawn* Boss = World->SpawnActor<ARPGBossTestPawn>();
	Boss->Boss->StartEncounter();
	const int32 Sequence = Boss->Boss->BeginTelegraph(Defaults->Styles[0].ActionTag, FVector(100.f, 20.f, 0.f), 10.f);
	// Register after the snapshot exists, exercising the same read-on-bind path as late join.
	URPGBossTelegraphPresentationComponent* View = NewObject<URPGBossTelegraphPresentationComponent>(Boss);
	View->Styles = Defaults->Styles;
	View->RegisterComponent();
	View->RefreshPresentation();
	if (!TestTrue(TEXT("Late-bound component renders current snapshot"), View->IsIndicatorVisible())) { return false; }
	UDecalComponent* Decal = View->GetIndicator();
	TestFalse(TEXT("Indicator is local, not a replicated subobject"), Decal->GetIsReplicated());
	TestEqual(TEXT("Standard radius sets decal half-extent"), Decal->DecalSize.Y, 200.0);
	const FVector LockedLocation = Decal->GetComponentLocation();
	Boss->SetActorLocation(FVector(1000.f, 0.f, 0.f));
	TestTrue(TEXT("Boss movement cannot drag locked aim"), Decal->GetComponentLocation().Equals(LockedLocation));
	Boss->Boss->EndTelegraph(1, Sequence);
	TestFalse(TEXT("Authoritative clear immediately hides decal"), View->IsIndicatorVisible());
	Boss->Boss->BeginTelegraph(Defaults->Styles[1].ActionTag, FVector::ZeroVector, 10.f);
	TestTrue(TEXT("Second action displays"), View->IsIndicatorVisible());
	TestEqual(TEXT("Heavy radius sets decal half-extent"), Decal->DecalSize.Y, 300.0);
	Boss->SetTestHealth(69.f);
	TestFalse(TEXT("Phase transition hides old indicator"), View->IsIndicatorVisible());
	Boss->Boss->BeginTelegraph(Defaults->Styles[1].ActionTag, FVector::ZeroVector, 10.f);
	TestTrue(TEXT("Phase can display new indicator"), View->IsIndicatorVisible());
	Boss->Boss->MarkWiped();
	TestFalse(TEXT("Wipe hides indicator"), View->IsIndicatorVisible());
	View->DestroyComponent();
	TestNull(TEXT("Ending presentation destroys local decal"), View->GetIndicator());
	return true;
}
#endif
