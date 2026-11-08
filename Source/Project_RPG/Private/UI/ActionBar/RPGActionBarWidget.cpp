#include "UI/ActionBar/RPGActionBarWidget.h"
#include "UI/ActionBar/RPGActionBarContext.h"
#include "UI/ActionBar/SRPGActionBarCell.h"
#include "UI/ActionBar/RPGActionBarLayout.h"
#include "Blueprint/WidgetTree.h"
#include "Skill/QuickSlot/RPGQuickSlotInputBindings.h"
#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SBoxPanel.h"

void URPGActionBarWidget::BindContext(URPGActionBarContext* InContext)
{
    UnbindContext();
    Context = InContext;
    BindingRevision = Context ? Context->GetRevision() : 0;
    SetIsFocusable(false);
    OnActionBarBound();
}

FRPGActionBarIdentityView URPGActionBarWidget::ReadIdentity() const
{
    return Context && BindingRevision == Context->GetRevision() ? Context->ReadIdentity() : FRPGActionBarIdentityView{};
}

FName URPGActionBarWidget::SourceFor(FName Id) const
{
    return FName(*FString::Printf(TEXT("Pointer.%u.%s"), GetUniqueID(), *Id.ToString()));
}
void URPGActionBarWidget::PressAction(FName Id)
{
    if (!Context || Context->GetRevision() != BindingRevision) return;
    PointerActions.Add(Id);
    Context->PressAction(SourceFor(Id), Id, BindingRevision);
}
void URPGActionBarWidget::ReleaseAction(FName Id)
{
    if (!PointerActions.Remove(Id)) return;
    if (Context) Context->ReleaseInput(SourceFor(Id));
}
void URPGActionBarWidget::UnbindContext()
{
    const auto Actions = PointerActions.Array();
    for (FName Id : Actions) ReleaseAction(Id);
    Context = nullptr;
}
void URPGActionBarWidget::NativeDestruct()
{
    UnbindContext();
    Super::NativeDestruct();
}

TSharedRef<SWidget> URPGActionBarWidget::RebuildWidget()
{
    // Authored WidgetTree layouts remain in control for custom left/central modules.
    if (WidgetTree && WidgetTree->RootWidget) return Super::RebuildWidget();
    const auto* Profile = Context ? Context->GetProfile() : nullptr;
    TArray<int32> Order;
    if (!Profile || !Profile->TryBuildDisplayOrder(Order)) return SNew(SHorizontalBox);
    TWeakObjectPtr<URPGActionBarWidget> Weak(this);
    return RPGActionBarLayout::CreateRow(Order, [Weak, Profile](int32 Index) -> TSharedRef<SWidget>
    {
        const auto Entry = Profile->Actions[Index];
        const FName Id = Entry.ActionId;
        FSlateBrush Icon;
        Icon.SetResourceObject(Entry.Icon);
        Icon.ImageSize = FVector2D(36.f, 36.f);
        return SNew(SRPGActionBarCell).Label(Entry.Label).Icon(&Icon)
            .View_Lambda([Weak, Id, Slot=Entry.SkillSlot] {
                auto* W = Weak.Get();
                if (!W || !W->Context || W->BindingRevision != W->Context->GetRevision()) return FRPGActionBarActionView{};
                auto View = W->Context->ReadAction(Id);
                if (View.KeyLabel.IsEmpty() && Slot != INDEX_NONE)
                    View.KeyLabel = FRPGQuickSlotInputBindings::ReadKeyLabel(W->GetOwningPlayer(), true, Slot);
                return View;
            })
            .IsHeld_Lambda([Weak, Id] {
                auto* W = Weak.Get();
                return W && W->Context && W->BindingRevision == W->Context->GetRevision() && W->PointerActions.Contains(Id);
            })
            .OnPressed_Lambda([Weak, Id] { if (auto* W = Weak.Get()) W->PressAction(Id); })
            .OnReleased_Lambda([Weak, Id] { if (auto* W = Weak.Get()) W->ReleaseAction(Id); });
    });
}
