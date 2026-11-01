#include "Tests/RPGPlayerHUDLifecycleTestWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "GameFramework/Pawn.h"
#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "UI/HUD/RPGHUD.h"
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

#endif
TFunction<void(UUserWidget*)> URPGPlayerHUDLifecycleTestWidget::OnInitialize;
TFunction<void(UUserWidget*)> URPGPlayerHUDLifecycleTestWidget::OnConstruct;
TFunction<void(UUserWidget*)> URPGPlayerHUDLifecycleTestWidget::OnDestruct;
void URPGPlayerHUDLifecycleTestWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    InitialPawn = GetOwningPlayerPawn();
    auto Callback = MoveTemp(OnInitialize);
    if (Callback) Callback(this);
}

void URPGPlayerHUDLifecycleTestWidget::NativeConstruct()
{
    Super::NativeConstruct();
    auto Callback = MoveTemp(OnConstruct);
    if (Callback) Callback(this);
}
void URPGPlayerHUDLifecycleTestWidget::NativeDestruct()
{
    Super::NativeDestruct();
    auto Callback = MoveTemp(OnDestruct);
    if (Callback) Callback(this);
}
TSharedRef<SWidget> URPGPlayerHUDLifecycleTestWidget::RebuildWidget()
{
    return SNew(SBorder);
}
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
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
			Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/PlayerHUDLifecycle_%s"),
				*FGuid::NewGuid().ToString(EGuidFormats::Digits))));
			bInitialized = true;
			World = Instance->GetWorld();
			if (!World) return false;
			// Minimal RPC worlds disable actor callbacks; this case exercises real BeginPlay/EndPlay.
#if UE_SUPPORT_FOR_ACTOR_TICK_DISABLE
			World->EnableActorTickAndUserCallbacks(true);
#endif
			// Actor ProcessEvent suppresses dynamic delegates before actors are initialized.
			World->InitializeActorsForPlay(FURL());
			if (!World->AreActorsInitialized()) return false;
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
		ARPGHUD* CreateHUD(TFunction<void(ARPGHUD&)> Configure = {})
		{
			FActorSpawnParameters Parameters; Parameters.Owner = Controller;
			HUD = World->SpawnActor<ARPGHUD>(Parameters);
			if (HUD) { HUD->PostInitializeComponents(); if (Configure) Configure(*HUD); HUD->DispatchBeginPlay(); }
			return HUD;
		}
		TArray<URPGPlayerHUDLifecycleTestWidget*> Views() const
		{
			TArray<URPGPlayerHUDLifecycleTestWidget*> Result;
			for (TObjectIterator<URPGPlayerHUDLifecycleTestWidget> It; It; ++It)
				if (It->GetWorld() == World && It->GetOwningPlayer() == Controller && It->IsInViewport()) Result.Add(*It);
			return Result;
		}
		~FViewportFixture()
		{
			URPGPlayerHUDLifecycleTestWidget::OnInitialize = {};
			URPGPlayerHUDLifecycleTestWidget::OnConstruct = {};
			URPGPlayerHUDLifecycleTestWidget::OnDestruct = {};
			if (HUD && !HUD->IsActorBeingDestroyed()) HUD->Destroy();
			if (Viewport) Viewport->RemoveAllViewportWidgets();
			if (Window && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterVirtualWindow(Window.ToSharedRef());
			if (bInitialized) Instance->Shutdown();
			if (Viewport) Viewport->DetachViewportClient();
			if (World) { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
		}
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPlayerHUDLifecycleTest,
    "ProjectRPG.UI.PlayerHUD.PossessionLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPlayerHUDLifecycleTest::RunTest(const FString&)
{
    FViewportFixture F;
    if (!TestTrue(TEXT("Local player viewport"), F.Initialize())) return false;
    auto Configure = [](ARPGHUD& HUD) { HUD.PlayerHUDWidgetClass = URPGPlayerHUDLifecycleTestWidget::StaticClass(); };
    if (!TestNotNull(TEXT("HUD before possession"), F.CreateHUD(Configure))) return false;
    TestNull(TEXT("No Pawn creates no root"), F.HUD->GetPlayerHUDWidget());
    APawn* A = F.World->SpawnActor<APawn>();
    APawn* B = F.World->SpawnActor<APawn>();
    F.Controller->Possess(A);
    auto* FirstView = Cast<URPGPlayerHUDLifecycleTestWidget>(F.HUD->GetPlayerHUDWidget());
    if (!TestNotNull(TEXT("Possession creates authored root"), FirstView)) return false;
    TStrongObjectPtr<UUserWidget> First(FirstView);
    TestEqual(TEXT("Explicit owning player"), First->GetOwningPlayer(), F.Controller);
    TestTrue(TEXT("Initialized against current Pawn"), FirstView->InitialPawn == A);
    TestTrue(TEXT("Real player Slate layer"), ContainsWidget(F.Window, First->GetCachedWidget()));
    F.Controller->OnPossessedPawnChanged.Broadcast(nullptr, A);
    TestEqual(TEXT("Duplicate event keeps instance"), F.HUD->GetPlayerHUDWidget(), First.Get());
    F.Controller->Possess(B);
    auto* Second = Cast<URPGPlayerHUDLifecycleTestWidget>(F.HUD->GetPlayerHUDWidget());
    TestTrue(TEXT("Pawn replacement creates fresh bindings"), Second && Second != First.Get() && Second->InitialPawn == B);
    TestFalse(TEXT("Old root removed from player layer"), First->IsInViewport() || ContainsWidget(F.Window, First->GetCachedWidget()));
    TestEqual(TEXT("Exactly one root"), F.Views().Num(), 1);
    F.Controller->UnPossess();
    TestNull(TEXT("Unpossess clears root"), F.HUD->GetPlayerHUDWidget());
    TestEqual(TEXT("Unpossess leaves no roots"), F.Views().Num(), 0);
    F.Controller->Possess(A);
    TStrongObjectPtr<UUserWidget> Last(F.HUD->GetPlayerHUDWidget());
    F.HUD->Destroy(); F.HUD = nullptr;
    TestTrue(TEXT("HUD EndPlay removes root"), Last.IsValid() && !Last->IsInViewport() && !ContainsWidget(F.Window, Last->GetCachedWidget()));
    F.Controller->Possess(B);
    TestEqual(TEXT("Destroyed HUD cannot recreate"), F.Views().Num(), 0);
    F.CreateHUD();
    TestNull(TEXT("Null class disables visual root"), F.HUD->GetPlayerHUDWidget());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPlayerHUDReentrantTest,
    "ProjectRPG.UI.PlayerHUD.AuthoredCallbackLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPlayerHUDReentrantTest::RunTest(const FString&)
{
    FViewportFixture F;
    if (!TestTrue(TEXT("Local player viewport"), F.Initialize())) return false;
    auto Configure = [](ARPGHUD& HUD) { HUD.PlayerHUDWidgetClass = URPGPlayerHUDLifecycleTestWidget::StaticClass(); };
    APawn* A = F.World->SpawnActor<APawn>();
    APawn* B = F.World->SpawnActor<APawn>();
    F.Controller->Possess(A);
    URPGPlayerHUDLifecycleTestWidget::OnInitialize = [&](UUserWidget*) { F.Controller->Possess(B); };
    F.CreateHUD(Configure);
    auto* Current = Cast<URPGPlayerHUDLifecycleTestWidget>(F.HUD->GetPlayerHUDWidget());
    TestTrue(TEXT("Initialization repossession preserves newest root"), Current && Current->InitialPawn == B);
    TestEqual(TEXT("Superseded initialized tree never enters viewport"), F.Views().Num(), 1);
    URPGPlayerHUDLifecycleTestWidget::OnDestruct = [&](UUserWidget*) { F.Controller->Possess(B); };
    F.Controller->Possess(A);
    Current = Cast<URPGPlayerHUDLifecycleTestWidget>(F.HUD->GetPlayerHUDWidget());
    TestTrue(TEXT("Destruct repossession preserves actual current Pawn"), Current && Current->InitialPawn == F.Controller->GetPawn());
    TestEqual(TEXT("Destruct reentry leaves one root"), F.Views().Num(), 1);
    F.HUD->Destroy(); F.HUD = nullptr;
    URPGPlayerHUDLifecycleTestWidget::OnConstruct = [&](UUserWidget*) { F.HUD->Destroy(); };
    F.CreateHUD(Configure);
    TestNull(TEXT("Construct EndPlay clears ownership"), F.HUD->GetPlayerHUDWidget());
    TestEqual(TEXT("Construct EndPlay never registers late Slate entry"), F.Views().Num(), 0);
    return true;
}
#endif
