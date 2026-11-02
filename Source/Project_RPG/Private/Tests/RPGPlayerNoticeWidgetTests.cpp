#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "UI/Notice/RPGPlayerNoticeWidget.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

struct FRPGPlayerNoticeWidgetTestAccess
{
    static void Detach(URPGPlayerNoticeWidget& W) { W.NativeDestruct(); }
    static void Duration(URPGPlayerNoticeWidget& W, float Value) { W.DisplayDuration = Value; }
};
namespace
{
struct FNoticeFixture
{
    TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>(GEngine)};
    UWorld* World = nullptr;
    URPGPlayerNoticeWidget* Widget = nullptr;
    TSharedPtr<SWidget> Slate;
    bool Initialize()
    {
        Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/PlayerNotice_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
        World = Instance->GetWorld();
        if (!World) return false;
#if UE_SUPPORT_FOR_ACTOR_TICK_DISABLE
        World->EnableActorTickAndUserCallbacks(true);
#endif
        World->InitializeActorsForPlay(FURL());
        World->GetTimerManager().Tick(0.f);
        Widget = NewObject<URPGPlayerNoticeWidget>(World);
        if (!Widget->Initialize()) return false;
        Slate = Widget->TakeWidget();
        return true;
    }
    UPlayerUIComponent* Source()
    {
        auto* Pawn = World->SpawnActor<APawn>();
        auto* UI = NewObject<UPlayerUIComponent>(Pawn);
        Pawn->AddInstanceComponent(UI); UI->RegisterComponent(); Pawn->DispatchBeginPlay();
        return UI;
    }
    void Advance(float Seconds) { ++GFrameCounter; World->GetTimerManager().Tick(Seconds); }
    ~FNoticeFixture()
    {
        if (Widget) { FRPGPlayerNoticeWidgetTestAccess::Detach(*Widget); Widget->ReleaseSlateResources(true); }
        Slate.Reset(); Instance->Shutdown(); RPGTestWorld::Destroy(World);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNoticeExpiryTest, "ProjectRPG.UI.Notice.ReplacementAndExpiry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGNoticeExpiryTest::RunTest(const FString&)
{
    FNoticeFixture F;
    if (!TestTrue(TEXT("World and widget"), F.Initialize())) return false;
    auto* UI = F.Source(); F.Widget->SetNoticeSource(UI);
    TestTrue(TEXT("No template placeholder"), F.Widget->GetDisplayedNotice().IsEmpty());
    UI->OnNoticeTextChanged.Broadcast(FText::FromString(TEXT("First")));
    F.Advance(2.f);
    UI->OnNoticeTextChanged.Broadcast(FText::FromString(TEXT("Newest")));
    F.Advance(1.1f);
    TestEqual(TEXT("Old expiry cannot hide a newer notice"), F.Widget->GetDisplayedNotice().ToString(), FString(TEXT("Newest")));
    F.Advance(2.f);
    TestTrue(TEXT("Newest notice gets its own full duration"), F.Widget->GetDisplayedNotice().IsEmpty());
    TestEqual(TEXT("Expiry collapses presentation"), F.Widget->GetVisibility(), ESlateVisibility::Collapsed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNoticeSourceTest, "ProjectRPG.UI.Notice.SourceAndDestructLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGNoticeSourceTest::RunTest(const FString&)
{
    FNoticeFixture F;
    if (!TestTrue(TEXT("World and widget"), F.Initialize())) return false;
    auto* A = F.Source(); auto* B = F.Source();
    F.Widget->SetNoticeSource(A); A->OnNoticeTextChanged.Broadcast(FText::FromString(TEXT("A")));
    F.Widget->SetNoticeSource(B);
    TestTrue(TEXT("Source replacement clears old message"), F.Widget->GetDisplayedNotice().IsEmpty());
    A->OnNoticeTextChanged.Broadcast(FText::FromString(TEXT("Stale")));
    TestTrue(TEXT("Old source cannot publish"), F.Widget->GetDisplayedNotice().IsEmpty());
    B->OnNoticeTextChanged.Broadcast(FText::FromString(TEXT("B")));
    TestEqual(TEXT("New source publishes"), F.Widget->GetDisplayedNotice().ToString(), FString(TEXT("B")));
    B->GetOwner()->Destroy();
    TestTrue(TEXT("Owner EndPlay clears message and timer"), F.Widget->GetDisplayedNotice().IsEmpty());
    F.Widget->SetNoticeSource(A); A->OnNoticeTextChanged.Broadcast(FText::FromString(TEXT("Before removal")));
    FRPGPlayerNoticeWidgetTestAccess::Detach(*F.Widget);
    A->OnNoticeTextChanged.Broadcast(FText::FromString(TEXT("After removal")));
    F.Advance(4.f);
    TestTrue(TEXT("Destruct releases source and timer"), F.Widget->GetDisplayedNotice().IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNoticeInvalidTest, "ProjectRPG.UI.Notice.InvalidTextAndDuration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGNoticeInvalidTest::RunTest(const FString&)
{
    FNoticeFixture F;
    if (!TestTrue(TEXT("World and widget"), F.Initialize())) return false;
    F.Widget->ShowNotice(FText::FromString(TEXT("  ")));
    TestTrue(TEXT("Whitespace hides"), F.Widget->GetDisplayedNotice().IsEmpty());
    for (float Duration : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN()})
    {
        FRPGPlayerNoticeWidgetTestAccess::Duration(*F.Widget, Duration);
        F.Widget->ShowNotice(FText::FromString(TEXT("Invalid")));
        TestTrue(TEXT("Invalid lifetime cannot stick on screen"), F.Widget->GetDisplayedNotice().IsEmpty());
    }
    return true;
}
#endif
