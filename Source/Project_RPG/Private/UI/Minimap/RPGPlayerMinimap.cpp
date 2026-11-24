#include "UI/Minimap/RPGPlayerMinimap.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/NativeWidgetHost.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "RegionTravel/RPGRegionGate.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"

namespace
{
	constexpr float MapSize = 208.f;
	constexpr float MapInset = 10.f;
	const FBox2D TownBounds(FVector2D(-1800, -1800), FVector2D(1800, 1800));

	FVector2D ProjectToMap(const FVector& Location, const FBox2D& Bounds)
	{
		const FVector2D Span = Bounds.GetSize();
		return FVector2D((Location.Y - Bounds.Min.Y) / Span.Y, (Bounds.Max.X - Location.X) / Span.X);
	}

	bool IsOnMap(const FVector2D& UV)
	{
		return UV.X >= 0 && UV.X <= 1 && UV.Y >= 0 && UV.Y <= 1;
	}

	struct FMinimapImage
	{
		TStrongObjectPtr<UTexture2D> Texture;
		FSlateBrush Brush;

		void Load(const TCHAR* Path)
		{
			Texture.Reset(LoadObject<UTexture2D>(nullptr, Path));
			Brush.SetResourceObject(Texture.Get());
			Brush.DrawAs = ESlateBrushDrawType::Image;
		}
	};

	class SRPGTownMinimap final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRPGTownMinimap) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& Args, APawn& InPawn)
		{
			Pawn = &InPawn;
			SetVisibility(EVisibility::HitTestInvisible);
			ForceVolatile(true);

			Frame.Load(TEXT("/Game/Minimap/T_UI_HUD_Map_Bg_02.T_UI_HUD_Map_Bg_02"));
			Map.Load(TEXT("/Game/Minimap/T_TownMinimap.T_TownMinimap"));
			PlayerIcon.Load(TEXT("/Game/Minimap/T_UI_Icons_MainHud_Compass_Marker_01.T_UI_Icons_MainHud_Compass_Marker_01"));
			PortalIcon.Load(TEXT("/Game/Minimap/T_UI_Icons_Map_Portal_01.T_UI_Icons_Map_Portal_01"));

			for (TActorIterator<ARPGRegionGate> It(InPawn.GetWorld()); It; ++It)
			{
				if (It->GetLevel() == InPawn.GetWorld()->PersistentLevel && It->RegionLevel.IsNull()
					&& IsValid(It->Destination) && It->Destination != *It)
				{
					TownGates.Add(*It);
				}
			}
		}

		virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
		{
			return FVector2D(MapSize, MapSize);
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
			FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
		{
			const APawn* LocalPawn = Pawn.Get();
			if (!LocalPawn || !Map.Texture.IsValid()) return Layer;

			const FVector2D PlayerUV = ProjectToMap(LocalPawn->GetActorLocation(), TownBounds);
			if (!IsOnMap(PlayerUV)) return Layer;

			const FVector2D Size = Geometry.GetLocalSize();
			const FVector2D Inset(MapInset, MapInset);
			const FVector2D InnerSize = Size - 2 * Inset;
			const FLinearColor Tint = Style.GetColorAndOpacityTint();
			if (Frame.Texture.IsValid())
			{
				FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform()),
					&Frame.Brush, ESlateDrawEffect::None, Tint);
			}
			FSlateDrawElement::MakeBox(Elements, Layer + 1, Geometry.ToPaintGeometry(InnerSize, FSlateLayoutTransform(Inset)),
				&Map.Brush, ESlateDrawEffect::None, Tint);

			const auto MarkerGeometry = [&](const FVector2D& UV, float Diameter)
			{
				const float Radius = Diameter / 2.f;
				const FVector2D Pixel = UV * InnerSize;
				const FVector2D Center(FMath::Clamp(Pixel.X, Radius, InnerSize.X - Radius),
					FMath::Clamp(Pixel.Y, Radius, InnerSize.Y - Radius));
				return Geometry.ToPaintGeometry(FVector2D(Diameter, Diameter),
					FSlateLayoutTransform(Inset + Center - FVector2D(Radius, Radius)));
			};

			if (PortalIcon.Texture.IsValid())
			{
				for (const TWeakObjectPtr<ARPGRegionGate>& TownGate : TownGates)
				{
					const ARPGRegionGate* Gate = TownGate.Get();
					if (!Gate) continue;
					const FVector2D PortalUV = ProjectToMap(Gate->GetActorLocation(), TownBounds);
					if (!IsOnMap(PortalUV)) continue;
					FSlateDrawElement::MakeBox(Elements, Layer + 2, MarkerGeometry(PortalUV, 23.f),
						&PortalIcon.Brush, ESlateDrawEffect::None, Tint);
				}
			}

			if (PlayerIcon.Texture.IsValid())
			{
				// The Gladiator arrow points down in the texture. World yaw zero points up on this map.
				const float Angle = FMath::DegreesToRadians(LocalPawn->GetActorRotation().Yaw + 180.f);
				FSlateDrawElement::MakeRotatedBox(Elements, Layer + 3, MarkerGeometry(PlayerUV, 20.f),
					&PlayerIcon.Brush, ESlateDrawEffect::None, Angle, TOptional<FVector2D>(),
					FSlateDrawElement::RelativeToElement, Tint);
			}
			return Layer + 3;
		}

	private:
		TWeakObjectPtr<APawn> Pawn;
		TArray<TWeakObjectPtr<ARPGRegionGate>> TownGates;
		FMinimapImage Frame;
		FMinimapImage Map;
		FMinimapImage PlayerIcon;
		FMinimapImage PortalIcon;
	};
}

void RPGPlayerMinimap::Attach(UUserWidget& PlayerHUD, APawn& Pawn)
{
	const UWorld* World = Pawn.GetWorld();
	if (!World || UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) != TEXT("/Game/Maps/RegionTravel/L_Town"))
	{
		return;
	}

	UWidgetTree* Tree = PlayerHUD.WidgetTree;
	auto* Canvas = Tree ? Cast<UCanvasPanel>(Tree->RootWidget) : nullptr;
	if (!Canvas) return;

	auto* Host = Tree->ConstructWidget<UNativeWidgetHost>(UNativeWidgetHost::StaticClass(), TEXT("PlayerMinimap"));
	Host->SetVisibility(ESlateVisibility::HitTestInvisible);
	Host->SetContent(SNew(SRPGTownMinimap, Pawn));
	auto* Slot = Canvas->AddChildToCanvas(Host);
	Slot->SetAnchors(FAnchors(0.f, 0.f));
	Slot->SetPosition(FVector2D(24.f, 24.f));
	Slot->SetSize(FVector2D(MapSize, MapSize));
}
