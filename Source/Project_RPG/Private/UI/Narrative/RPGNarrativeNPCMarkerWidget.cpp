#include "UI/Narrative/RPGNarrativeNPCMarkerWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"

void URPGNarrativeNPCMarkerWidget::Configure(FProvider InProvider) { Provider = MoveTemp(InProvider); Render(); }
TSharedRef<SWidget> URPGNarrativeNPCMarkerWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        Caption = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NPCPersonalQuestMarker"));
        Caption->SetJustification(ETextJustify::Center); Caption->SetShadowOffset(FVector2D(1,1));
        Caption->SetShadowColorAndOpacity(FLinearColor::Black); WidgetTree->RootWidget = Caption;
        SetVisibility(ESlateVisibility::HitTestInvisible); SetIsFocusable(false);
    }
    Render(); return Super::RebuildWidget();
}
void URPGNarrativeNPCMarkerWidget::Render()
{
    if (!Caption) return;
    TArray<FString> Entries; TArray<FRPGDialogueNode> Nodes;
    Caption->SetText(Provider && Provider(Entries, Nodes) ? FRPGNarrativeNPCMarker::Label(FRPGNarrativeNPCMarker::Resolve(Entries, Nodes)) : FText::GetEmpty());
}
FText URPGNarrativeNPCMarkerWidget::GetDisplayedMarker() { Render(); return Caption ? Caption->GetText() : FText::GetEmpty(); }
void URPGNarrativeNPCMarkerWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{ Super::NativeTick(Geometry, DeltaTime); Elapsed += DeltaTime; if (Elapsed >= .2f) { Elapsed = 0.f; Render(); } }
void URPGNarrativeNPCMarkerWidget::NativeDestruct() { Provider = {}; Super::NativeDestruct(); }
