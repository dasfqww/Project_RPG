#if WITH_DEV_AUTOMATION_TESTS
#include "UI/ActionBar/RPGActionBarContext.h"
#include "UI/ActionBar/RPGActionBarHUDPresenter.h"
#include "UI/ActionBar/RPGActionBarWidget.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "RPGInputTags.h"
#include "UObject/StrongObjectPtr.h"

namespace RPGActionBarTests
{
struct FProvider : IRPGActionBarProvider
{
    TWeakObjectPtr<APawn> Pawn;
    TStrongObjectPtr<URPGActionBarProfile> Profile{NewObject<URPGActionBarProfile>()};
    TArray<FName> Presses, Releases;
    TFunction<void()> OnPress, OnRelease;
    bool bAvailable = true;
    FProvider(APawn* InPawn) : Pawn(InPawn)
    {
        Profile->ProfileId = TEXT("Riding");
        FRPGActionBarEntry Entry;
        Entry.ActionId = TEXT("Dash"); Entry.Label = FText::FromString(TEXT("Dash")); Entry.SkillSlot = 0;
        Profile->Actions.Add(Entry);
    }
    virtual APawn* GetActionBarPawn() const override { return Pawn.Get(); }
    virtual const URPGActionBarProfile* GetActionBarProfile() const override { return Profile.Get(); }
    virtual FRPGActionBarActionView ReadAction(FName) const override
    {
        FRPGActionBarActionView View;
        View.bAvailable = bAvailable; View.CooldownRemaining = 3.f; View.CooldownDuration = 8.f;
        return View;
    }
    virtual void PressAction(FName Id) override { Presses.Add(Id); if (OnPress) OnPress(); }
    virtual void ReleaseAction(FName Id) override { Releases.Add(Id); if (OnRelease) OnRelease(); }
};
struct FFixture
{
    TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>(GEngine)};
    UWorld* World = nullptr;
    UGameViewportClient* Viewport = nullptr;
    APawn* Pawn = nullptr;
    APlayerController* Controller = nullptr;
    UQuickSlotComponent* Slots = nullptr;
    bool Initialize()
    {
        Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/ActionBar_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
        World = Instance->GetWorld();
        if (!World) return false;
#if UE_SUPPORT_FOR_ACTOR_TICK_DISABLE
        World->EnableActorTickAndUserCallbacks(true);
#endif
        World->InitializeActorsForPlay(FURL());
        Viewport = NewObject<UGameViewportClient>(GEngine);
        Instance->GetWorldContext()->GameViewport = Viewport;
        Viewport->Init(*Instance->GetWorldContext(), Instance.Get(), false);
        FString Error;
        auto* LocalPlayer = Instance->CreateLocalPlayer(0, Error, false);
        Controller = World->SpawnActor<APlayerController>();
        Pawn = World->SpawnActor<APawn>();
        if (!LocalPlayer || !Controller || !Pawn) return false;
        Controller->SetPlayer(LocalPlayer); World->AddController(Controller); Controller->Possess(Pawn);
        Slots = NewObject<UQuickSlotComponent>(Pawn);
        Slots->RegisterComponent();
        return true;
    }
    ~FFixture()
    {
        if (Slots) Slots->GetActionBarContext()->Shutdown();
        Instance->Shutdown();
        if (Viewport) Viewport->DetachViewportClient();
        if (World) { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionBarTransitionTest, "ProjectRPG.UI.ActionBar.TransitionAndInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionBarTransitionTest::RunTest(const FString&)
{
    using namespace RPGActionBarTests;
    FFixture F;
    if (!TestTrue(TEXT("Pawn fixture"), F.Initialize())) return false;
    auto* C = F.Slots->GetActionBarContext();
    auto A = MakeShared<FProvider>(F.Pawn);
    auto B = MakeShared<FProvider>(F.Pawn);
    // Seed an isolated fixture without invoking production restore/ownership RPCs.
    const_cast<FRPGQuickSlotContent*>(F.Slots->GetSkillSlotContent(0))->AbilityTag = RPGGameplayTags::InputTag_Skill1;
    const_cast<FRPGQuickSlotContent*>(F.Slots->GetItemSlotContent(0))->ItemId = FGuid::NewGuid();
    const FRPGQuickSlotContent Before = *F.Slots->GetSkillSlotContent(0);
    const FRPGQuickSlotContent ItemBefore = *F.Slots->GetItemSlotContent(0);
    F.Slots->BeginUseSkillSlot(0);
    TestTrue(TEXT("Approved provider activates"), C->Activate(A));
    F.Slots->BeginUseSkillSlot(0);
    TestEqual(TEXT("Held base key does not trigger mounted action"), A->Presses.Num(), 0);
    F.Slots->EndUseSkillSlot(0);
    F.Slots->BeginUseSkillSlot(0);
    TestEqual(TEXT("Fresh key reaches feature"), A->Presses.Num(), 1);
    const uint64 OldRevision = C->GetRevision();
    TestTrue(TEXT("Same provider is idempotent"), C->Activate(A));
    TestEqual(TEXT("Idempotent revision"), C->GetRevision(), OldRevision);
    C->PressAction(TEXT("Pointer"), TEXT("Dash"), OldRevision);
    TestEqual(TEXT("Two inputs share one action press"), A->Presses.Num(), 1);
    TestTrue(TEXT("Replacement"), C->Activate(B));
    TestEqual(TEXT("Old action released once"), A->Releases.Num(), 1);
    TestFalse(TEXT("Stale removal cannot clear replacement"), C->Deactivate(A));
    C->PressAction(TEXT("OldWidget"), TEXT("Dash"), OldRevision);
    F.Slots->BeginUseSkillSlot(0);
    TestEqual(TEXT("Old click/held key cannot execute replacement"), B->Presses.Num(), 0);
    C->ReleaseInput(TEXT("Pointer"));
    F.Slots->EndUseSkillSlot(0);
    F.Slots->BeginUseSkillSlot(0);
    TestEqual(TEXT("Fresh key executes replacement"), B->Presses.Num(), 1);
    TestTrue(TEXT("Dismount restores base"), C->Deactivate(B));
    TestEqual(TEXT("Dismount releases held action"), B->Releases.Num(), 1);
    TestFalse(TEXT("Base mode"), C->HasOverride());
    TestEqual(TEXT("Class slot tag preserved"), F.Slots->GetSkillSlotContent(0)->AbilityTag, Before.AbilityTag);
    TestEqual(TEXT("Item ID preserved"), F.Slots->GetItemSlotContent(0)->ItemId, ItemBefore.ItemId);
    F.Slots->EndUseSkillSlot(0);
    TestFalse(TEXT("Base direct ability input passes through"), C->PressTag(RPGGameplayTags::InputTag_Skill1));
    C->Activate(A);
    TestTrue(TEXT("Held direct input consumed"), C->PressTag(RPGGameplayTags::InputTag_Skill1));
    C->ReleaseTag(RPGGameplayTags::InputTag_Skill1);
    C->Deactivate(A);
    TestFalse(TEXT("Fresh base direct input restored"), C->PressTag(RPGGameplayTags::InputTag_Skill1));
    C->ReleaseTag(RPGGameplayTags::InputTag_Skill1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionBarBoundaryTest, "ProjectRPG.UI.ActionBar.OwnershipAndReentrancy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionBarBoundaryTest::RunTest(const FString&)
{
    using namespace RPGActionBarTests;
    FFixture F;
    if (!TestTrue(TEXT("Pawn fixture"), F.Initialize())) return false;
    auto* C = F.Slots->GetActionBarContext();
    auto A = MakeShared<FProvider>(F.Pawn);
    auto B = MakeShared<FProvider>(F.Pawn);
    auto Other = MakeShared<FProvider>(F.World->SpawnActor<APawn>());
    TestFalse(TEXT("Other Pawn rejected"), C->Activate(Other));
    const auto Duplicate = A->Profile->Actions[0];
    A->Profile->Actions.Add(Duplicate);
    TestFalse(TEXT("Duplicate actions rejected"), C->Activate(A));
    A->Profile->Actions.SetNum(1);
    FRPGActionBarEntry ProviderOnly;
    ProviderOnly.ActionId = TEXT("Emote");
    A->Profile->Actions.Add(ProviderOnly);
    C->Activate(A);
    C->PressSlot(INDEX_NONE);
    C->PressTag(FGameplayTag());
    TestEqual(TEXT("Invalid input cannot select provider-only actions"), A->Presses.Num(), 0);
    A->bAvailable = false;
    C->PressSlot(0);
    TestEqual(TEXT("Unavailable action rejected"), A->Presses.Num(), 0);
    A->bAvailable = true;
    C->PressSlot(0);
    TestEqual(TEXT("Unblocking does not replay a held input"), A->Presses.Num(), 0);
    C->ReleaseSlot(0);
    A->OnPress = [C, B] { C->Activate(B); };
    C->PressSlot(0);
    TestTrue(TEXT("Synchronous press may switch profile"), C->GetProfile() == B->Profile.Get());
    TestEqual(TEXT("Reentrant switch releases original"), A->Releases.Num(), 1);
    C->ReleaseSlot(0);
    B->OnRelease = [C] { C->Shutdown(); };
    C->PressSlot(0);
    C->Deactivate(B);
    TestFalse(TEXT("Shutdown during release cannot resurrect provider"), C->HasOverride());
    TestFalse(TEXT("Shutdown rejects later activation"), C->Activate(A));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionBarHUDTest, "ProjectRPG.UI.ActionBar.HUDRegions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionBarHUDTest::RunTest(const FString&)
{
    using namespace RPGActionBarTests;
    FFixture F;
    if (!TestTrue(TEXT("Pawn fixture"), F.Initialize())) return false;
    auto* Root = CreateWidget<URPGActionBarWidget>(F.Controller);
    if (!TestNotNull(TEXT("Root"), Root)) return false;
    Root->WidgetTree = NewObject<UWidgetTree>(Root);
    auto* Canvas = Root->WidgetTree->ConstructWidget<UCanvasPanel>();
    Root->WidgetTree->RootWidget = Canvas;
    auto* Left = Root->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PlayerSkillBar"));
    auto* Center = Root->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PlayerIdentity"));
    auto* Right = Root->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Utility"));
    auto* LeftSlot = Canvas->AddChildToCanvas(Left);
    LeftSlot->SetPosition(FVector2D(-420.f, -100.f));
    LeftSlot->SetSize(FVector2D(320.f, 90.f));
    Canvas->AddChildToCanvas(Center); Canvas->AddChildToCanvas(Right);
    Left->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Center->SetVisibility(ESlateVisibility::HitTestInvisible);
    const auto RightVisibility = Right->GetVisibility();
    const TSharedRef<SWidget> PreparedRoot = Root->TakeWidget();
    auto* Presenter = NewObject<URPGActionBarHUDPresenter>(Root);
    auto* C = F.Slots->GetActionBarContext();
    TestTrue(TEXT("Two named regions bind"), Presenter->Bind(Root, C));
    auto A = MakeShared<FProvider>(F.Pawn);
    A->Profile->CenterWidgetClass = URPGActionBarWidget::StaticClass();
    C->Activate(A);
    TestEqual(TEXT("Both replacements inserted"), Canvas->GetChildrenCount(), 5);
    TestTrue(TEXT("Base left hidden"), Left->GetVisibility() == ESlateVisibility::Collapsed);
    TestTrue(TEXT("Base identity hidden"), Center->GetVisibility() == ESlateVisibility::Collapsed);
    auto* NewLeft = Cast<URPGActionBarWidget>(Canvas->GetChildAt(3));
    TestNotNull(TEXT("Default native action bar"), NewLeft);
    if (NewLeft)
    {
        TestTrue(TEXT("Shared context"), NewLeft->GetActionBarContext() == C);
        TestTrue(TEXT("Canvas position preserved"), Cast<UCanvasPanelSlot>(NewLeft->Slot)->GetPosition().Equals(LeftSlot->GetPosition()));
        NewLeft->PressAction(TEXT("Dash"));
    }
    C->Deactivate(A);
    TestEqual(TEXT("Only authored widgets remain"), Canvas->GetChildrenCount(), 3);
    TestTrue(TEXT("Left visibility restored"), Left->GetVisibility() == ESlateVisibility::SelfHitTestInvisible);
    TestTrue(TEXT("Center visibility restored"), Center->GetVisibility() == ESlateVisibility::HitTestInvisible);
    TestTrue(TEXT("Right region unchanged"), Right->GetVisibility() == RightVisibility);
    TestEqual(TEXT("Pointer action released on removal"), A->Releases.Num(), 1);
    A->Profile->CenterWidgetClass = nullptr;
    C->Activate(A);
    TestEqual(TEXT("Optional center omitted"), Canvas->GetChildrenCount(), 4);
    auto* PointerBar = Cast<URPGActionBarWidget>(Canvas->GetChildAt(3));
    if (PointerBar) PointerBar->PressAction(TEXT("Dash"));
    Presenter->Unbind();
    TestEqual(TEXT("HUD detach releases pointer immediately"), A->Releases.Num(), 2);
    TestEqual(TEXT("HUD teardown restores regions"), Canvas->GetChildrenCount(), 3);
    TestTrue(TEXT("Unbinding UI does not change gameplay mode"), C->HasOverride());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionBarAuthoredHUDTest, "ProjectRPG.UI.ActionBar.AuthoredHUD",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionBarAuthoredHUDTest::RunTest(const FString&)
{
    using namespace RPGActionBarTests;
    FFixture F;
    if (!TestTrue(TEXT("Pawn fixture"), F.Initialize())) return false;
    auto* Class = LoadClass<UUserWidget>(nullptr, TEXT("/Game/Blueprints/UI/PlayerHUD/WBP_PlayerHUD.WBP_PlayerHUD_C"));
    if (!TestNotNull(TEXT("Authored HUD class"), Class)) return false;
    auto* Root = CreateWidget<UUserWidget>(F.Controller, Class);
    if (!TestNotNull(TEXT("Authored HUD instance"), Root)) return false;
    const TSharedRef<SWidget> PreparedRoot = Root->TakeWidget();
    auto* Presenter = NewObject<URPGActionBarHUDPresenter>(Root);
    auto* C = F.Slots->GetActionBarContext();
    if (!TestTrue(TEXT("Actual HUD exposes both supported canvas regions"), Presenter->Bind(Root, C))) return false;
    auto* Left = Root->WidgetTree->FindWidget(TEXT("PlayerSkillBar"));
    auto* Center = Root->WidgetTree->FindWidget(TEXT("PlayerIdentity"));
    const auto LeftVisibility = Left->GetVisibility();
    const auto CenterVisibility = Center->GetVisibility();
    auto A = MakeShared<FProvider>(F.Pawn);
    C->Activate(A);
    TestTrue(TEXT("Authored skill bar hidden"), Left->GetVisibility() == ESlateVisibility::Collapsed);
    TestTrue(TEXT("Authored identity hidden"), Center->GetVisibility() == ESlateVisibility::Collapsed);
    C->Deactivate(A);
    TestTrue(TEXT("Authored skill bar restored"), Left->GetVisibility() == LeftVisibility);
    TestTrue(TEXT("Authored identity restored"), Center->GetVisibility() == CenterVisibility);
    Presenter->Unbind();
    return true;
}

#endif
