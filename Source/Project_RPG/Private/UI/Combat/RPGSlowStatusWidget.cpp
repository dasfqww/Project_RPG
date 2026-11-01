#include "UI/Combat/RPGSlowStatusWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Components/TextBlock.h"

void URPGSlowStatusWidget::SetControl(URPGCombatControlComponent* InControl) { Control = InControl; }

void URPGSlowStatusWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	Label = WidgetTree->ConstructWidget<UTextBlock>(); WidgetTree->RootWidget = Label;
	Label->SetJustification(ETextJustify::Center);
	auto Font = Label->GetFont(); Font.Size = 12; Label->SetFont(Font);
	Label->SetColorAndOpacity(FSlateColor(FLinearColor(.35f,.8f,1.f)));
	Label->SetShadowColorAndOpacity(FLinearColor::Black); Label->SetShadowOffset(FVector2D(1,1));
	SetVisibility(ESlateVisibility::HitTestInvisible);
}
void URPGSlowStatusWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);
	if (!Control.IsValid() || !Label) return;
	Label->SetText(NSLOCTEXT("RPGCombatStatus", "SlowName", "둔화"));
}
