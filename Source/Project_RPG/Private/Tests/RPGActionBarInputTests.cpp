#if WITH_DEV_AUTOMATION_TESTS
#include "UI/ActionBar/RPGActionBarContext.h"
#include "UI/ActionBar/RPGActionBarWidget.h"
#include "UI/RPGWindowInputState.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Component/UI/RPGSkillWindowComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace RPGActionBarInputTests
{
struct FProvider : IRPGActionBarProvider
{
    TWeakObjectPtr<APawn> Pawn;
    TStrongObjectPtr<URPGActionBarProfile> Profile{NewObject<URPGActionBarProfile>()};
    TFunction<void(FName)> OnRead;
    TArray<FName> Presses, Releases;
    explicit FProvider(APawn* InPawn) : Pawn(InPawn)
    {
        Profile->ProfileId = TEXT("InputBoundary");
        FRPGActionBarEntry Dash; Dash.ActionId = TEXT("Dash"); Dash.SkillSlot = 0;
        FRPGActionBarEntry Emote; Emote.ActionId = TEXT("Emote"); Emote.SkillSlot = 1;
        Profile->Actions = {Dash, Emote};
    }
    virtual APawn* GetActionBarPawn() const override { return Pawn.Get(); }
    virtual const URPGActionBarProfile* GetActionBarProfile() const override { return Profile.Get(); }
    virtual FRPGActionBarActionView ReadAction(FName Id) const override
    {
        const auto Callback = OnRead;
        if (Callback) Callback(Id);
        FRPGActionBarActionView View; View.bAvailable = true; return View;
    }
    virtual void PressAction(FName Id) override { Presses.Add(Id); }
    virtual void ReleaseAction(FName Id) override { Releases.Add(Id); }
};
struct FFixture
{
    TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>(GEngine)};
    TSharedRef<FRPGWindowInputState> Windows = MakeShared<FRPGWindowInputState>();
    UWorld* World = nullptr;
    UGameViewportClient* Viewport = nullptr;
    APlayerController* Controller = nullptr;
    APawn* Pawn = nullptr;
    UQuickSlotComponent* Slots = nullptr;
    bool Initialize()
    {
        Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/ActionBarInput_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
        World = Instance->GetWorld(); if (!World) return false;
#if UE_SUPPORT_FOR_ACTOR_TICK_DISABLE
        World->EnableActorTickAndUserCallbacks(true);
#endif
        World->InitializeActorsForPlay(FURL());
        Viewport = NewObject<UGameViewportClient>(GEngine);
        Instance->GetWorldContext()->GameViewport = Viewport;
        Viewport->Init(*Instance->GetWorldContext(), Instance.Get(), false);
        FString Error;
        auto* LocalPlayer = Instance->CreateLocalPlayer(0, Error, false);
        Controller = World->SpawnActor<APlayerController>(); Pawn = World->SpawnActor<APawn>();
        if (!LocalPlayer || !Controller || !Pawn) return false;
        Controller->SetPlayer(LocalPlayer); World->AddController(Controller); Controller->Possess(Pawn);
        Slots = NewObject<UQuickSlotComponent>(Pawn); Slots->RegisterComponent();
        auto* Component = NewObject<URPGSkillWindowComponent>(Controller);
        Component->SetWindowInputState(Windows); Component->RegisterComponent();
        return Pawn->IsLocallyControlled();
    }
    ~FFixture()
    {
        if (Slots) Slots->GetActionBarContext()->Shutdown();
        Windows->Reset();
        Instance->Shutdown();
        if (Viewport) Viewport->DetachViewportClient();
        if (World) { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionBarReadBoundary, "ProjectRPG.UI.ActionBar.InputBoundary.OwnershipAndWindows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionBarReadBoundary::RunTest(const FString&)
{
    using namespace RPGActionBarInputTests;
    FFixture F; if (!TestTrue(TEXT("Local fixture"), F.Initialize())) return false;
    auto* C = F.Slots->GetActionBarContext(); auto P = MakeShared<FProvider>(F.Pawn); C->Activate(P);
    P->OnRead = [&F](FName) { F.Controller->UnPossess(); };
    TestFalse(TEXT("Ownership lost during read retires the view"), C->ReadAction(TEXT("Dash")).bAvailable);
    F.Controller->Possess(F.Pawn);
    C->PressSlot(0);
    TestTrue(TEXT("Ownership loss cannot dispatch a pending press"), P->Presses.IsEmpty());
    C->ReleaseSlot(0); F.Controller->Possess(F.Pawn);
    TStrongObjectPtr<URPGActionBarWidget> Window(CreateWidget<URPGActionBarWidget>(F.Controller));
    if (!TestTrue(TEXT("Actual window instance"), Window.IsValid())) return false;
    P->OnRead = [&F, &Window](FName) { F.Windows->BeginWindow(*F.Controller, *Window.Get()); };
    C->PressSlot(0);
    TestTrue(TEXT("Window opened during read blocks dispatch"), P->Presses.IsEmpty());
    TestTrue(TEXT("Real window state is open"), F.Windows->HasOpenWindows());
    P->OnRead = nullptr; F.Windows->EndWindow(*Window.Get()); C->ReleaseSlot(0); C->PressSlot(0);
    TestEqual(TEXT("A fresh press after the boundary is valid"), P->Presses.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionBarInputCancellation, "ProjectRPG.UI.ActionBar.InputBoundary.Cancellation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionBarInputCancellation::RunTest(const FString&)
{
    using namespace RPGActionBarInputTests;
    FFixture F; if (!TestTrue(TEXT("Local fixture"), F.Initialize())) return false;
    auto* C = F.Slots->GetActionBarContext(); auto P = MakeShared<FProvider>(F.Pawn); C->Activate(P);
    for (bool bKeepPhysicalState : {false, true})
    {
        P->OnRead = [C, bKeepPhysicalState](FName) { C->ReleaseAll(bKeepPhysicalState); };
        C->PressSlot(0);
        TestTrue(TEXT("Cancellation during read cannot resurrect a press"), P->Presses.IsEmpty());
        P->OnRead = nullptr;
        if (bKeepPhysicalState)
        {
            C->PressSlot(0);
            TestTrue(TEXT("Preserved held key still needs release"), P->Presses.IsEmpty());
        }
        C->ReleaseSlot(0);
    }
    C->PressSlot(0); C->ReleaseSlot(0);
    TestEqual(TEXT("Fresh input dispatches exactly once"), P->Presses.Num(), 1);
    TestEqual(TEXT("Fresh input releases exactly once"), P->Releases.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionBarSourceReuse, "ProjectRPG.UI.ActionBar.InputBoundary.SourceReuse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionBarSourceReuse::RunTest(const FString&)
{
    using namespace RPGActionBarInputTests;
    FFixture F; if (!TestTrue(TEXT("Local fixture"), F.Initialize())) return false;
    auto* C = F.Slots->GetActionBarContext(); auto P = MakeShared<FProvider>(F.Pawn); C->Activate(P);
    const FName Source(TEXT("External.Pointer"));
    P->OnRead = [C, P, Source](FName Id) {
        if (Id != TEXT("Dash")) return;
        P->OnRead = nullptr;
        C->ReleaseInput(Source);
        C->PressAction(Source, TEXT("Emote"), C->GetRevision());
    };
    C->PressAction(Source, TEXT("Dash"), C->GetRevision());
    TestTrue(TEXT("Only replacement input dispatches"), P->Presses == TArray<FName>{TEXT("Emote")});
    C->ReleaseInput(Source);
    TestTrue(TEXT("Original read cannot overwrite replacement release ownership"), P->Releases == TArray<FName>{TEXT("Emote")});
    return true;
}
#endif
