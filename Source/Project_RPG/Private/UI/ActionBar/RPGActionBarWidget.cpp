#include "UI/ActionBar/RPGActionBarWidget.h"
#include "UI/ActionBar/RPGActionBarContext.h"
#include "UI/ActionBar/SRPGActionBarCell.h"
#include "Blueprint/WidgetTree.h"
#include "Skill/QuickSlot/RPGQuickSlotInputBindings.h"
#include "Engine/Texture2D.h"
#include "Widgets/SBoxPanel.h"

void URPGActionBarWidget::BindContext(URPGActionBarContext* InContext)
{
    UnbindContext();
    Context = InContext;
    BindingRevision = Context ? Context->GetRevision() : 0;
    SetIsFocusable(false);
    OnActionBarBound();
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
    auto Row = SNew(SHorizontalBox);
    if (!Profile) return Row;
    IconBrushes.SetNum(Profile->Actions.Num());
    TWeakObjectPtr<URPGActionBarWidget> Weak(this);
    for (int32 Index = 0; Index < Profile->Actions.Num(); ++Index)
    {
        const auto Entry = Profile->Actions[Index];
        const FName Id = Entry.ActionId;
        IconBrushes[Index].SetResourceObject(Entry.Icon);
        IconBrushes[Index].ImageSize = FVector2D(36.f, 36.f);
        Row->AddSlot().AutoWidth().Padding(3.f)[
            SNew(SRPGActionBarCell).Label(Entry.Label).Icon(&IconBrushes[Index])
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
            .OnReleased_Lambda([Weak, Id] { if (auto* W = Weak.Get()) W->ReleaseAction(Id); })
        ];
    }
    return Row;
}
