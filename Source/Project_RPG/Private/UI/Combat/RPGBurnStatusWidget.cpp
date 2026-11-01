#include "UI/Combat/RPGBurnStatusWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Components/TextBlock.h"

void URPGBurnStatusWidget::SetControl(URPGCombatControlComponent* InControl) { Control = InControl; }

void URPGBurnStatusWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	Label = WidgetTree->ConstructWidget<UTextBlock>(); WidgetTree->RootWidget = Label;
	Label->SetJustification(ETextJustify::Center);
	auto Font = Label->GetFont(); Font.Size = 12; Label->SetFont(Font);
	Label->SetColorAndOpacity(FSlateColor(FLinearColor(1.f,.45f,.15f)));
	Label->SetShadowColorAndOpacity(FLinearColor::Black); Label->SetShadowOffset(FVector2D(1,1));
	SetVisibility(ESlateVisibility::HitTestInvisible);
}
void URPGBurnStatusWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);
	if (!Control.IsValid() || !Label) return;
	Label->SetText(NSLOCTEXT("RPGCombatStatus", "BurnName", "화상"));
}
