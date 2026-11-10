#include "Tests/RPGRidingActionBarTests.h"
#include "Riding/RPGRidingComponent.h"
#include "Component/UI/QuickSlotComponent.h"

ARPGRidingActionBarTestPawn::ARPGRidingActionBarTestPawn()
{
    Riding = CreateDefaultSubobject<URPGRidingComponent>(TEXT("Riding"));
    Slots = CreateDefaultSubobject<UQuickSlotComponent>(TEXT("Slots"));
}

#if WITH_DEV_AUTOMATION_TESTS
#include "UI/ActionBar/RPGRidingActionBarSubsystem.h"
#include "UI/ActionBar/RPGActionBarContext.h"
#include "UI/ActionBar/RPGActionBarHUDPresenter.h"
#include "UI/ActionBar/RPGActionBarWidget.h"
#include "UI/ActionBar/RPGActionBarIdentityWidget.h"
#include "Riding/RPGMountDefinition.h"
#include "Tests/RPGRidingActorTests.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/SizeBox.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "RPGInputTags.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/Text/STextBlock.h"
#include "Layout/ArrangedChildren.h"

namespace RPGRidingActionBarTests
{
struct FFixture
{
    TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>(GEngine)};
    UWorld* World = nullptr;
    UGameViewportClient* Viewport = nullptr;
    ULocalPlayer* Local = nullptr;
    APlayerController* PC = nullptr;
    ARPGRidingActionBarTestPawn* Pawn = nullptr;
    bool Initialize()
    {
        Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/RidingHUD_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
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
        Local = Instance->CreateLocalPlayer(0, Error, false);
        PC = World->SpawnActor<APlayerController>();
        Pawn = World->SpawnActor<ARPGRidingActionBarTestPawn>();
        if (!Local || !PC || !Pawn) return false;
        PC->SetPlayer(Local); World->AddController(PC); PC->Possess(Pawn);
        return Local->GetSubsystem<URPGRidingActionBarSubsystem>() != nullptr;
    }
    ~FFixture()
    {
        Instance->Shutdown();
        if (Viewport) Viewport->DetachViewportClient();
        if (World) { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
    }
};
FRPGRidingRepState Mounted()
{
    FRPGRidingRepState State;
    State.Key = {FGuid::NewGuid(), 1, 0};
    State.Mode = ERPGRidingMode::Mounted; State.MountId = TEXT("horse"); State.DefinitionVersion = 1;
    State.Movement.Version = 1; State.Movement.Speed = 600;
    State.Movement.Acceleration = 2000; State.Movement.Braking = 2000; State.Movement.YawRate = 360;
    return State;
}
// Emulate an accepted network property update through the real RepNotify. No authority/ownership bypass is shipped.
bool Replicate(URPGRidingComponent& C, const FRPGRidingRepState& State)
{
    auto* Property = FindFProperty<FStructProperty>(C.GetClass(), TEXT("State"));
    auto* Notify = C.FindFunction(TEXT("OnRep_State"));
    if (!Property || !Notify || !RPGRiding::IsCanonical(State)) return false;
    auto Previous = C.GetRidingState();
    *Property->ContainerPtrToValuePtr<FRPGRidingRepState>(&C) = State;
    C.ProcessEvent(Notify, &Previous);
    return true;
}
struct FHUD
{
    TStrongObjectPtr<URPGActionBarWidget> Root;
    TStrongObjectPtr<URPGActionBarHUDPresenter> Presenter;
    TSharedPtr<SWidget> Slate;
    UWidget* Left = nullptr;
    UWidget* Center = nullptr;
    UWidget* Right = nullptr;
    bool Initialize(FFixture& F)
    {
        Root.Reset(CreateWidget<URPGActionBarWidget>(F.PC));
        if (!Root.IsValid()) return false;
        Root->WidgetTree = NewObject<UWidgetTree>(Root.Get());
        auto* Canvas = Root->WidgetTree->ConstructWidget<UCanvasPanel>(); Root->WidgetTree->RootWidget = Canvas;
        Left = Root->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PlayerSkillBar"));
        Center = Root->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PlayerIdentity"));
        Right = Root->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Utility"));
        Canvas->AddChildToCanvas(Left); Canvas->AddChildToCanvas(Center); Canvas->AddChildToCanvas(Right);
        Slate = Root->TakeWidget();
        Presenter.Reset(NewObject<URPGActionBarHUDPresenter>(Root.Get()));
        return Presenter->Bind(Root.Get(), F.Pawn->Slots->GetActionBarContext());
    }
    ~FHUD() { if (Presenter.IsValid()) Presenter->Unbind(); }
};
struct FOtherProvider : IRPGActionBarProvider
{
    TWeakObjectPtr<APawn> Pawn;
    TStrongObjectPtr<URPGActionBarProfile> Profile{NewObject<URPGActionBarProfile>()};
    FOtherProvider(APawn* P) : Pawn(P) { Profile->ProfileId = TEXT("OtherFeature"); }
    virtual APawn* GetActionBarPawn() const override { return Pawn.Get(); }
    virtual const URPGActionBarProfile* GetActionBarProfile() const override { return Profile.Get(); }
    virtual FRPGActionBarActionView ReadAction(FName) const override { return {}; }
    virtual void PressAction(FName) override {}
    virtual void ReleaseAction(FName) override {}
};
URPGMountDefinition* DefineMount(const TCHAR* Id, const TCHAR* Name)
{
    auto* D = NewObject<URPGMountDefinition>();
    D->MountId = Id; D->DisplayName = FText::FromString(Name); D->SaddleSocket = TEXT("saddle");
    D->Icon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Test/Icon.Icon")));
    D->Mesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Test/Mesh.Mesh")));
    D->AnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Game/Test/Anim.Anim_C")));
    D->RiderPoseLayer = D->AnimClass;
    D->DashPresentation = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(TEXT("/Game/Test/Dash.Dash")));
    D->EmotePresentation = D->DashPresentation;
    return D;
}
FString ReadSlateText(const TSharedRef<SWidget>& Widget)
{
    Widget->UpdateAllAttributes();
    FString Text;
    if (Widget->GetType() == TEXT("STextBlock")) Text = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
    auto* Children = Widget->GetChildren();
    for (int32 I = 0; I < Children->Num(); ++I) Text += TEXT("|") + ReadSlateText(Children->GetChildAt(I));
    return Text;
}
TSharedPtr<SWidget> FindSlateWidget(const TSharedRef<SWidget>& Widget, FName Type)
{
    Widget->UpdateAllAttributes();
    if (Widget->GetType() == Type) return Widget;
    auto* Children = Widget->GetChildren();
    for (int32 I = 0; I < Children->Num(); ++I)
        if (auto Found = FindSlateWidget(Children->GetChildAt(I), Type)) return Found;
    return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRidingActionBarTransitions, "ProjectRPG.UI.ActionBar.Riding.TransitionsAndCommand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRidingActionBarTransitions::RunTest(const FString&)
{
    using namespace RPGRidingActionBarTests;
    FFixture F; if (!TestTrue(TEXT("Local player auto creates bridge"), F.Initialize())) return false;
    FHUD HUD; if (!TestTrue(TEXT("Actual presenter binds"), HUD.Initialize(F))) return false;
    auto* C = F.Pawn->Slots->GetActionBarContext();
    const_cast<FRPGQuickSlotContent*>(F.Pawn->Slots->GetSkillSlotContent(3))->AbilityTag = RPGGameplayTags::InputTag_Skill1;
    const_cast<FRPGQuickSlotContent*>(F.Pawn->Slots->GetItemSlotContent(0))->ItemId = FGuid::NewGuid();
    const auto SkillBefore = *F.Pawn->Slots->GetSkillSlotContent(3);
    const auto ItemBefore = *F.Pawn->Slots->GetItemSlotContent(0);
    TestFalse(TEXT("OnFoot keeps class profile"), C->HasOverride());
    F.Pawn->Slots->BeginUseSkillSlot(3);
    const auto State = Mounted();
    TestTrue(TEXT("RepNotify receives canonical Mounted"), Replicate(*F.Pawn->Riding, State));
    if (!TestTrue(TEXT("Mounted activates Riding profile"), C->HasOverride())) return false;
    TestEqual(TEXT("Dash and manual dismount actions"), C->GetProfile()->Actions.Num(), 2);
    TestTrue(TEXT("Manual dismount available"), C->ReadAction(TEXT("Dismount")).bAvailable);
    TestTrue(TEXT("Class left and identity hidden"), HUD.Left->GetVisibility() == ESlateVisibility::Collapsed && HUD.Center->GetVisibility() == ESlateVisibility::Collapsed);
    TestTrue(TEXT("Utility region kept"), HUD.Right->GetVisibility() != ESlateVisibility::Collapsed);
    const auto Revision = C->GetRevision();
    F.Pawn->Slots->BeginUseSkillSlot(3);
    TestEqual(TEXT("Held class key does not submit dismount"), C->GetRevision(), Revision);
    F.Pawn->Slots->EndUseSkillSlot(3);
    // This fixture has no admission session: the real server request must reject it, release its invalid epoch and restore the UI.
    F.Pawn->Slots->BeginUseSkillSlot(3);
    TestTrue(TEXT("Real request reaches authority guard, not a local state setter"), !F.Pawn->Riding->GetRidingState().Key.RiderEpoch.IsValid());
    TestFalse(TEXT("Rejected synthetic authority state restores class UI"), C->HasOverride());
    TestTrue(TEXT("Original left restored"), HUD.Left->GetVisibility() != ESlateVisibility::Collapsed);
    TestTrue(TEXT("Class loadout untouched"), F.Pawn->Slots->GetSkillSlotContent(3)->AbilityTag == SkillBefore.AbilityTag);
    TestTrue(TEXT("Item binding untouched"), F.Pawn->Slots->GetItemSlotContent(0)->ItemId == ItemBefore.ItemId);
    F.Pawn->Slots->EndUseSkillSlot(3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRidingActionBarLifetime, "ProjectRPG.UI.ActionBar.Riding.PossessionAndHUDLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRidingActionBarLifetime::RunTest(const FString&)
{
    using namespace RPGRidingActionBarTests;
    FFixture F; if (!TestTrue(TEXT("Fixture"), F.Initialize())) return false;
    auto* C = F.Pawn->Slots->GetActionBarContext();
    auto State = Mounted(); Replicate(*F.Pawn->Riding, State);
    if (!TestTrue(TEXT("Mounted profile"), C->HasOverride())) return false;
    const auto* Profile = C->GetProfile(); const auto Revision = C->GetRevision();
    { FHUD First; TestTrue(TEXT("First visual"), First.Initialize(F)); }
    { FHUD Second; TestTrue(TEXT("Recreated visual"), Second.Initialize(F)); TestTrue(TEXT("Mounted visual recreated"), Second.Left->GetVisibility() == ESlateVisibility::Collapsed); }
    TestTrue(TEXT("HUD recreation preserves provider/profile"), C->GetProfile() == Profile && C->GetRevision() == Revision);
    const auto Duplicate = State; Replicate(*F.Pawn->Riding, Duplicate);
    TestEqual(TEXT("Duplicate state is idempotent"), C->GetRevision(), Revision);
    State.Key.RiderEpoch = FGuid::NewGuid(); Replicate(*F.Pawn->Riding, State);
    TestTrue(TEXT("New epoch invalidates old widget revision"), C->GetRevision() > Revision);
    auto* OldPawn = F.Pawn;
    auto* Replacement = F.World->SpawnActor<ARPGRidingActionBarTestPawn>(); F.PC->Possess(Replacement);
    TestFalse(TEXT("Possession retires old provider"), C->HasOverride());
    TestFalse(TEXT("New OnFoot Pawn stays on class bar"), Replacement->Slots->GetActionBarContext()->HasOverride());
    OldPawn->Riding->OnChanged.Broadcast(State);
    TestFalse(TEXT("Late old Pawn callback cannot switch new bar"), Replacement->Slots->GetActionBarContext()->HasOverride());
    Replicate(*Replacement->Riding, Mounted());
    TestTrue(TEXT("New Pawn can mount"), Replacement->Slots->GetActionBarContext()->HasOverride());
    F.PC->UnPossess();
    TestFalse(TEXT("Unpossess retires provider"), Replacement->Slots->GetActionBarContext()->HasOverride());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRidingActionBarArbitration, "ProjectRPG.UI.ActionBar.Riding.OwnershipAndCleanup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRidingActionBarArbitration::RunTest(const FString&)
{
    using namespace RPGRidingActionBarTests;
    FFixture F; if (!TestTrue(TEXT("Fixture"), F.Initialize())) return false;
    auto* C = F.Pawn->Slots->GetActionBarContext();
    auto Other = MakeShared<FOtherProvider>(F.Pawn);
    C->Activate(Other);
    auto State = Mounted(); Replicate(*F.Pawn->Riding, State);
    TestTrue(TEXT("Riding does not steal another profile"), C->GetProfile() == Other->Profile.Get());
    C->Deactivate(Other);
    TestTrue(TEXT("Retiring other feature reveals current mount"), C->HasOverride() && C->GetProfile()->ProfileId == TEXT("Riding"));
    C->Activate(Other);
    FRPGRidingRepState OnFoot; OnFoot.Key = State.Key; ++OnFoot.Key.StateRevision;
    Replicate(*F.Pawn->Riding, OnFoot);
    TestTrue(TEXT("Dismount does not clear another feature"), C->GetProfile() == Other->Profile.Get());
    C->Deactivate(Other); Replicate(*F.Pawn->Riding, Mounted());
    F.Pawn->Riding->UnregisterComponent();
    FTSTicker::GetCoreTicker().Tick(0.11f);
    TestFalse(TEXT("Unregistered source releases profile"), C->HasOverride());
    F.Pawn->Riding->RegisterComponent(); Replicate(*F.Pawn->Riding, Mounted());
    FTSTicker::GetCoreTicker().Tick(0.11f);
    TestTrue(TEXT("Late component registration is observed"), C->HasOverride());
    F.Local->GetSubsystem<URPGRidingActionBarSubsystem>()->Deinitialize();
    TestFalse(TEXT("Local player shutdown releases its profile"), C->HasOverride());
    F.Pawn->Riding->OnChanged.Broadcast(Mounted());
    TestFalse(TEXT("Late callback after shutdown is inert"), C->HasOverride());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRidingActionBarIdentity, "ProjectRPG.UI.ActionBar.Riding.IdentityAndMovement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRidingActionBarIdentity::RunTest(const FString&)
{
    using namespace RPGRidingActionBarTests;
    FFixture F; if (!TestTrue(TEXT("Local fixture"), F.Initialize())) return false;
    FHUD HUD; if (!TestTrue(TEXT("HUD"), HUD.Initialize(F))) return false;
    auto* C = F.Pawn->Slots->GetActionBarContext();
    auto State = Mounted();
    TestTrue(TEXT("Fixture consumes the admitted movement contract"), RPGRiding::IsCanonical(State));
    auto* Horse = DefineMount(TEXT("horse"), TEXT("First Mount"));
    auto* Wolf = DefineMount(TEXT("wolf"), TEXT("Second Mount"));
    TArray<URPGMountDefinition*> Definitions{Horse, Wolf};
    RPGSetRidingVisualTestState(*F.Pawn->Riding, State, Definitions);
    auto View = C->ReadIdentity();
    TestTrue(TEXT("Identity is visible"), View.bVisible);
    TestEqual(TEXT("Metadata display name"), View.Title.ToString(), FString(TEXT("First Mount")));
    TestTrue(TEXT("Admitted base speed"), View.Detail.ToString().Contains(FText::AsNumber(600.f).ToString()));
    auto* Canvas = Cast<UCanvasPanel>(HUD.Root->WidgetTree->RootWidget);
    auto* Identity = Cast<URPGActionBarIdentityWidget>(Canvas->GetChildAt(4));
    if (!TestNotNull(TEXT("Actual central identity replacement"), Identity)) return false;
    TStrongObjectPtr<URPGActionBarIdentityWidget> OldIdentity(Identity);
    const auto Slate = Identity->GetCachedWidget();
    if (!TestTrue(TEXT("Native identity Slate tree"), Slate.IsValid())) return false;
    TestTrue(TEXT("Actual rendered text includes name"), ReadSlateText(Slate.ToSharedRef()).Contains(TEXT("First Mount")));
    auto Panel = FindSlateWidget(Slate.ToSharedRef(), TEXT("SVerticalBox"));
    if (!TestTrue(TEXT("Native central layout"), Panel.IsValid())) return false;
    Panel->SlatePrepass(1.f);
    FArrangedChildren Arranged(EVisibility::Visible);
    Panel->ArrangeChildren(FGeometry::MakeRoot(FVector2f(80.f, 100.f), FSlateLayoutTransform()), Arranged, true);
    if (!TestEqual(TEXT("Three identity text rows"), Arranged.Num(), 3)) return false;
    TestTrue(TEXT("Title wraps within narrow host"), Arranged[0].Geometry.GetLocalSize().X <= 80.01f);
    TestTrue(TEXT("Speed wraps within narrow host"), Arranged[2].Geometry.GetLocalSize().X <= 80.01f);
    const auto Revision = C->GetRevision();
    State.Movement.Speed = 900; ++State.Key.StateRevision;
    RPGSetRidingVisualTestState(*F.Pawn->Riding, State, Definitions);
    TestEqual(TEXT("Value update keeps profile/input revision"), C->GetRevision(), Revision);
    TestTrue(TEXT("Same native tree reads changed speed"), ReadSlateText(Slate.ToSharedRef()).Contains(FText::AsNumber(900.f).ToString()));
    State.MountId = TEXT("wolf"); ++State.Key.StateRevision;
    RPGSetRidingVisualTestState(*F.Pawn->Riding, State, Definitions);
    TestTrue(TEXT("Replacement mount updates the same identity widget"), ReadSlateText(Slate.ToSharedRef()).Contains(TEXT("Second Mount")));
    State.Key.RiderEpoch = FGuid::NewGuid(); State.Key.StateRevision = 1;
    RPGSetRidingVisualTestState(*F.Pawn->Riding, State, Definitions);
    TestFalse(TEXT("Retired epoch widget cannot expose new identity"), OldIdentity->ReadIdentity().bVisible);
    FRPGRidingRepState OnFoot; OnFoot.Key = State.Key; ++OnFoot.Key.StateRevision;
    Replicate(*F.Pawn->Riding, OnFoot);
    TestFalse(TEXT("Dismount clears override identity"), C->ReadIdentity().bVisible);
    TestTrue(TEXT("Original class identity restored"), HUD.Center->GetVisibility() != ESlateVisibility::Collapsed);
    TestTrue(TEXT("Right utility remains visible"), HUD.Right->GetVisibility() != ESlateVisibility::Collapsed);
    return true;
}

#endif
