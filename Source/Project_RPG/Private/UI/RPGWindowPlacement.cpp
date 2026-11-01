#include "UI/RPGWindowPlacement.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/World.h"

FRPGWindowPlacement RPGWindowPlacement::Calculate(
	const FVector2D& ViewportSize, const FVector2D& DesiredSize,
	const ERPGWindowSide Side, const double EdgeInset)
{
	const double HorizontalAnchor = Side == ERPGWindowSide::Left ? 0.0 : Side == ERPGWindowSide::Right ? 1.0 : 0.5;
	double Inset = FMath::Max(0.0, EdgeInset);
	double VerticalOffset = 0.0;
	if (ViewportSize.X > 0.0)
	{
		// Give up the margin before clipping an otherwise fitting window.
		Inset = FMath::Min(Inset, FMath::Max(0.0, (ViewportSize.X - DesiredSize.X) * 0.5));
	}
	if (ViewportSize.Y > 0.0)
	{
		// If the window is too tall, keep its title at the top rather than offscreen.
		VerticalOffset = FMath::Max(0.0, (DesiredSize.Y - ViewportSize.Y) * 0.5);
	}
	return {
		FVector2D(HorizontalAnchor, 0.5),
		FVector2D(HorizontalAnchor, 0.5),
		FVector2D(Side == ERPGWindowSide::Left ? Inset : Side == ERPGWindowSide::Right ? -Inset : 0.0, VerticalOffset)
	};
}

void RPGWindowPlacement::Apply(UUserWidget& Window, const ERPGWindowSide Side)
{
	Window.ForceLayoutPrepass();
	FVector2D ViewportSize = FVector2D::ZeroVector;
	const UWorld* World = Window.GetWorld();
	if (World && World->IsGameWorld() && World->GetGameViewport())
	{
		// These windows use AddToViewport, so use that entire viewport, not a
		// player-region geometry or desktop coordinates. Remove DPI exactly once.
		const double Scale = FMath::Max<double>(UWidgetLayoutLibrary::GetViewportScale(&Window), UE_SMALL_NUMBER);
		ViewportSize = UWidgetLayoutLibrary::GetViewportSize(&Window) / Scale;
	}
	const FRPGWindowPlacement Placement = Calculate(ViewportSize, Window.GetDesiredSize(), Side);
	// Zero size enables natural autosizing; do not freeze inventory content size.
	// UE 5.8's size/position setters reset anchors, so set anchors last.
	Window.SetDesiredSizeInViewport(FVector2D::ZeroVector);
	Window.SetPositionInViewport(Placement.Offset, false);
	Window.SetAnchorsInViewport(FAnchors(Placement.Anchor.X, Placement.Anchor.Y));
	Window.SetAlignmentInViewport(Placement.Alignment);
}
