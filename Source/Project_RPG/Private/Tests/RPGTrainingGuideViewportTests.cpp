#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "UI/Content/RPGTrainingGuideWidget.h"
#include "UI/Content/RPGDungeonResultActionsWidget.h"
#include "UI/HUD/RPGHUD.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Layout/Children.h"
#include "Slate/SGameLayerManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SVirtualWindow.h"

namespace
{
	bool ContainsWidget(const TSharedPtr<SWidget>& Root, const TSharedPtr<SWidget>& Target)
	{
		if (!Root || !Target) return false;
		if (Root == Target) return true;
		if (auto* Children = Root->GetChildren())
			for (int32 Index = 0; Index < Children->Num(); ++Index)
				if (ContainsWidget(Children->GetChildAt(Index), Target)) return true;
		return false;
	}
	struct FViewportFixture
	{
		TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>(GEngine)};
		UWorld* World = nullptr;
		UGameViewportClient* Viewport = nullptr;
		APlayerController* Controller = nullptr;
		ARPGHUD* HUD = nullptr;
		TSharedPtr<SVirtualWindow> Window;
		bool bInitialized = false;
		bool Initialize()
		{
			if (!FSlateApplication::IsInitialized()) return false;
			Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/TrainingGuide_%s"),
				*FGuid::NewGuid().ToString(EGuidFormats::Digits))));
			bInitialized = true;
			World = Instance->GetWorld();
			if (!World) return false;
			// Minimal RPC worlds disable actor callbacks; this case exercises real BeginPlay/EndPlay.
#if UE_SUPPORT_FOR_ACTOR_TICK_DISABLE
			World->EnableActorTickAndUserCallbacks(true);
#endif
			Viewport = NewObject<UGameViewportClient>(GEngine);
			Instance->GetWorldContext()->GameViewport = Viewport;
			Viewport->Init(*Instance->GetWorldContext(), Instance.Get(), false);
			Window = SNew(SVirtualWindow).Size(FVector2D(1280.f, 720.f));
			auto Overlay = SNew(SOverlay);
			auto Layers = SNew(SGameLayerManager);
			Overlay->AddSlot()[Layers];
			Window->SetContent(Overlay);
			FSlateApplication::Get().RegisterVirtualWindow(Window.ToSharedRef());
			// This headless adapter owns a virtual window rather than binding an OS scene viewport.
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			Viewport->SetViewportOverlayWidget(Window, Overlay);
			Viewport->SetGameLayerManager(Layers);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
			FString Error;
			auto* Player = Instance->CreateLocalPlayer(0, Error, false);
			if (!Player) return false;
			Controller = World->SpawnActor<APlayerController>();
			if (!Controller) return false;
			Controller->SetPlayer(Player); World->AddController(Controller);
			if (Player->GetPlayerController(World) != Controller) return false;
			return true;
		}
		ARPGHUD* CreateHUD()
		{
			FActorSpawnParameters Parameters; Parameters.Owner = Controller;
			HUD = World->SpawnActor<ARPGHUD>(Parameters);
			if (HUD) { HUD->PostInitializeComponents(); HUD->DispatchBeginPlay(); }
			return HUD;
		}
		TArray<URPGTrainingGuideWidget*> Guides(const TCHAR* ContentId = TEXT("Progression.Training")) const
		{
			TArray<URPGTrainingGuideWidget*> Result;
			for (TObjectIterator<URPGTrainingGuideWidget> It; It; ++It)
				if (It->GetWorld() == World && It->GetOwningPlayer() == Controller && It->IsInViewport()
					&& FCString::Strcmp(It->GetPracticeContentId(), ContentId) == 0) Result.Add(*It);
			return Result;
		}
		~FViewportFixture()
		{
			if (HUD && !HUD->IsActorBeingDestroyed()) HUD->Destroy();
			if (Viewport) Viewport->RemoveAllViewportWidgets();
			if (Window && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterVirtualWindow(Window.ToSharedRef());
			if (bInitialized) Instance->Shutdown();
			if (Viewport) Viewport->DetachViewportClient();
			if (World) { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTrainingGuideViewportTest,
	"ProjectRPG.Progression.TrainingGuide.NativeViewportLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTrainingGuideViewportTest::RunTest(const FString&)
{
	FViewportFixture F;
	if (!TestTrue(TEXT("Owned virtual viewport and local player ready"), F.Initialize())) return false;
	if (!TestTrue(TEXT("Local controller owns a local player"), F.Controller->IsLocalController() && F.Controller->GetLocalPlayer())) return false;
	TestEqual(TEXT("No guide is injected by this test"), F.Guides().Num(), 0);
	TestEqual(TEXT("No Intro1 guide is injected by this test"), F.Guides(TEXT("Progression.Intro1")).Num(), 0);
	if (!TestNotNull(TEXT("Production HUD begins in the local viewport"), F.CreateHUD())) return false;
	if (!TestEqual(TEXT("HUD owner is the registered controller"), F.HUD->GetOwningPlayerController(), F.Controller)) return false;
	auto Guides = F.Guides();
	if (!TestEqual(TEXT("HUD creates exactly one guide"), Guides.Num(), 1)) return false;
	TStrongObjectPtr<URPGTrainingGuideWidget> First(Guides[0]);
	auto IntroGuides = F.Guides(TEXT("Progression.Intro1"));
	if (!TestEqual(TEXT("HUD creates exactly one separate Intro1 guide"), IntroGuides.Num(), 1)) return false;
	TStrongObjectPtr<URPGTrainingGuideWidget> FirstIntro(IntroGuides[0]);
	TestTrue(TEXT("Intro1 guide is attached to the actual player Slate layer"), ContainsWidget(F.Window, FirstIntro->GetCachedWidget()));
	TestFalse(TEXT("Intro1 guide never takes focus"), FirstIntro->IsFocusable());
	TestEqual(TEXT("Intro1 guide does not intercept gameplay input"), FirstIntro->GetVisibility(), ESlateVisibility::HitTestInvisible);
	auto* IntroCard = Cast<UBorder>(FirstIntro->WidgetTree->FindWidget(TEXT("TrainingGuideCard")));
	if (!TestNotNull(TEXT("Intro1 native guide tree is constructed by AddToPlayerScreen"), IntroCard)) return false;
	TestEqual(TEXT("Ordinary world starts without an Intro1 prompt"), IntroCard->GetVisibility(), ESlateVisibility::Collapsed);
	TestTrue(TEXT("Guide is attached to the actual player Slate layer"), ContainsWidget(F.Window, First->GetCachedWidget()));
	TestFalse(TEXT("Guide never takes focus"), First->IsFocusable());
	TestEqual(TEXT("Guide does not intercept gameplay input"), First->GetVisibility(), ESlateVisibility::HitTestInvisible);
	auto* Card = Cast<UBorder>(First->WidgetTree->FindWidget(TEXT("TrainingGuideCard")));
	if (!TestNotNull(TEXT("Native guide tree was constructed by AddToPlayerScreen"), Card)) return false;
	TestEqual(TEXT("Ordinary world starts without a training prompt"), Card->GetVisibility(), ESlateVisibility::Collapsed);
	// HUD creation/ownership needs no game-specific controller or injected progress.
	// The separate guide tests cover active progress and the RPG input context.
	// Replacing the real HUD must reclaim the first view before the next one is shown.
	F.HUD->Destroy(); F.HUD = nullptr;
	TestFalse(TEXT("HUD EndPlay removes its guide from the viewport"), First->IsInViewport());
	TestFalse(TEXT("Removed guide is absent from the Slate window"), ContainsWidget(F.Window, First->GetCachedWidget()));
	TestNull(TEXT("Removed guide releases the displayed Pawn"), First->GetDisplayedPawn());
	TestFalse(TEXT("HUD EndPlay removes its Intro1 guide from the viewport"), FirstIntro->IsInViewport());
	TestFalse(TEXT("Removed Intro1 guide is absent from the Slate window"), ContainsWidget(F.Window, FirstIntro->GetCachedWidget()));
	TestNull(TEXT("Removed Intro1 guide releases the displayed Pawn"), FirstIntro->GetDisplayedPawn());
	if (!TestNotNull(TEXT("Replacement production HUD"), F.CreateHUD())) return false;
	Guides = F.Guides();
	if (!TestEqual(TEXT("Replacement does not accumulate guides"), Guides.Num(), 1)) return false;
	TestTrue(TEXT("Replacement is a fresh widget"), Guides[0] != First.Get());
	TStrongObjectPtr<URPGTrainingGuideWidget> Replacement(Guides[0]);
	TestTrue(TEXT("Replacement is in the player Slate layer"), ContainsWidget(F.Window, Replacement->GetCachedWidget()));
	auto* ReplacementCard = Cast<UBorder>(Replacement->WidgetTree->FindWidget(TEXT("TrainingGuideCard")));
	TestTrue(TEXT("Replacement without training context has no stale prompt"), ReplacementCard && ReplacementCard->GetVisibility() == ESlateVisibility::Collapsed);
	TestNull(TEXT("Generic local controller cannot invent a training target"), Replacement->GetDisplayedPawn());
	IntroGuides = F.Guides(TEXT("Progression.Intro1"));
	if (!TestEqual(TEXT("Replacement does not accumulate Intro1 guides"), IntroGuides.Num(), 1)) return false;
	TStrongObjectPtr<URPGTrainingGuideWidget> IntroReplacement(IntroGuides[0]);
	TestTrue(TEXT("Replacement Intro1 guide is a fresh widget"), IntroReplacement.Get() != FirstIntro.Get());
	TestTrue(TEXT("Replacement Intro1 guide is in the player Slate layer"), ContainsWidget(F.Window, IntroReplacement->GetCachedWidget()));
	auto* IntroReplacementCard = Cast<UBorder>(IntroReplacement->WidgetTree->FindWidget(TEXT("TrainingGuideCard")));
	TestTrue(TEXT("Replacement without Intro1 context has no stale prompt"), IntroReplacementCard && IntroReplacementCard->GetVisibility() == ESlateVisibility::Collapsed);
	TestNull(TEXT("Generic local controller cannot invent an Intro1 target"), IntroReplacement->GetDisplayedPawn());
	for (TObjectIterator<URPGDungeonResultActionsWidget> It; It; ++It)
		if (It->GetWorld() == F.World && It->GetOwningPlayer() == F.Controller)
			TestFalse(TEXT("An ordinary HUD never creates an authenticated completion overlay"), It->IsInViewport());
	F.HUD->Destroy(); F.HUD = nullptr;
	TestEqual(TEXT("No guide remains in the player viewport after HUD destruction"), F.Guides().Num(), 0);
	TestNull(TEXT("Replacement also clears the observed Pawn"), Replacement->GetDisplayedPawn());
	TestEqual(TEXT("No Intro1 guide remains after HUD destruction"), F.Guides(TEXT("Progression.Intro1")).Num(), 0);
	TestNull(TEXT("Intro1 replacement also clears the observed Pawn"), IntroReplacement->GetDisplayedPawn());
	return true;
}
#endif
