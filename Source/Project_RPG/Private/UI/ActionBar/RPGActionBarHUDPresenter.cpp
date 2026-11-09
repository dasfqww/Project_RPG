#include "UI/ActionBar/RPGActionBarHUDPresenter.h"
#include "UI/ActionBar/RPGActionBarContext.h"
#include "UI/ActionBar/RPGActionBarWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "UObject/StrongObjectPtr.h"

bool URPGActionBarHUDPresenter::Bind(UUserWidget* Root, URPGActionBarContext* InContext)
{
    Unbind();
    if (!Root || !Root->WidgetTree || !InContext) return false;
    UWidget* Skill = Root->WidgetTree->FindWidget(TEXT("PlayerSkillBar"));
    UWidget* Identity = Root->WidgetTree->FindWidget(TEXT("PlayerIdentity"));
    if (!Skill || !Identity || !Cast<UCanvasPanelSlot>(Skill->Slot) || !Cast<UCanvasPanelSlot>(Identity->Slot))
        return false;
    HUDRoot = Root;
    Context = InContext;
    BaseLeft = Skill;
    BaseCenter = Identity;
    ChangedHandle = Context->OnChanged.AddUObject(this, &ThisClass::Refresh);
    Refresh();
    return true;
}
void URPGActionBarHUDPresenter::Restore()
{
    // Publish null before widget Destruct callbacks can reenter.
    auto* OldLeft = Left.Get();
    auto* OldCenter = Center.Get();
    Left = nullptr;
    Center = nullptr;
    if (OldLeft) { OldLeft->UnbindContext(); OldLeft->RemoveFromParent(); }
    if (OldCenter) { OldCenter->UnbindContext(); OldCenter->RemoveFromParent(); }
    if (bOverridden)
    {
        bOverridden = false;
        if (BaseLeft) BaseLeft->SetVisibility(LeftVisibility);
        if (BaseCenter) BaseCenter->SetVisibility(CenterVisibility);
    }
}
void URPGActionBarHUDPresenter::Unbind()
{
    if (Context) Context->OnChanged.Remove(ChangedHandle);
    ChangedHandle.Reset();
    Restore();
    Context = nullptr;
    HUDRoot = nullptr;
    BaseLeft = BaseCenter = nullptr;
}
URPGActionBarWidget* URPGActionBarHUDPresenter::CreateReplacement(
    UWidget* Original, TSubclassOf<URPGActionBarWidget> Class)
{
    auto* Canvas = Original ? Cast<UCanvasPanel>(Original->GetParent()) : nullptr;
    auto* OriginalSlot = Original ? Cast<UCanvasPanelSlot>(Original->Slot) : nullptr;
    if (!Canvas || !OriginalSlot || !HUDRoot || !Context || !Class) return nullptr;
    auto Layout = OriginalSlot->GetLayout();
    bool bAutoSize = OriginalSlot->GetAutoSize();
    const auto* Profile = Context->GetProfile();
    if (Original == BaseCenter && Profile && Profile->bOverrideCenterLayout)
    {
        Layout = Profile->CenterLayout;
        bAutoSize = Profile->bCenterAutoSize;
    }
    const int32 ZOrder = OriginalSlot->GetZOrder();
    const uint64 Revision = Context->GetRevision();
    TStrongObjectPtr<URPGActionBarWidget> Widget(CreateWidget<URPGActionBarWidget>(HUDRoot->GetOwningPlayer(), Class));
    if (!Widget.IsValid()) return nullptr;
    Widget->BindContext(Context);
    const TSharedRef<SWidget> PreparedWidget = Widget->TakeWidget();
    if (!Context || Context->GetRevision() != Revision || !HUDRoot) { Widget->UnbindContext(); return nullptr; }
    auto* Slot = Canvas->AddChildToCanvas(Widget.Get());
    Slot->SetLayout(Layout);
    Slot->SetAutoSize(bAutoSize);
    Slot->SetZOrder(ZOrder + 1);
    return Widget.Get();
}
void URPGActionBarHUDPresenter::Refresh()
{
    if (bRefreshing) { bRefreshPending = true; return; }
    TGuardValue<bool> Guard(bRefreshing, true);
    do { bRefreshPending = false; RefreshOnce(); } while (bRefreshPending && Context);
}
void URPGActionBarHUDPresenter::RefreshOnce()
{
    Restore();
    if (!Context || !Context->HasOverride() || !BaseLeft || !BaseCenter) return;
    const auto* Profile = Context->GetProfile();
    if (!Profile) return;
    const uint64 Revision = Context->GetRevision();
    LeftVisibility = BaseLeft->GetVisibility();
    CenterVisibility = BaseCenter->GetVisibility();
    Left = CreateReplacement(BaseLeft, Profile->LeftWidgetClass
        ? Profile->LeftWidgetClass.Get() : URPGActionBarWidget::StaticClass());
    if (!Context || Context->GetRevision() != Revision) { Restore(); return; }
    if (Profile->CenterWidgetClass) Center = CreateReplacement(BaseCenter, Profile->CenterWidgetClass);
    if (!Context || Context->GetRevision() != Revision || !Left)
    {
        Restore();
        return;
    }
    bOverridden = true;
    BaseLeft->SetVisibility(ESlateVisibility::Collapsed);
    BaseCenter->SetVisibility(ESlateVisibility::Collapsed);
}
