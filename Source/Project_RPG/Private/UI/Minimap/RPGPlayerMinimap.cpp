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
#include "Layout/Clipping.h"
#include "RegionTravel/RPGRegionGate.h"
#include "Rendering/DrawElements.h"
#include "Textures/SlateShaderResource.h"
#include "UI/HUD/RPGHUDLayout.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"

namespace
{
	constexpr float MapInset = 14.f;
	constexpr double ViewRadiusCm = 1200.0;

	struct FMinimapDisc
	{
		TArray<FVector2D> Points;
		TArray<SlateIndex> Indices;

		FMinimapDisc()
		{
			constexpr int32 Sides = 64;
			Points.Reserve(Sides + 1);
			Indices.Reserve(Sides * 3);
			Points.Add(FVector2D::ZeroVector);
			for (int32 Side = 0; Side < Sides; ++Side)
			{
				const double Angle = Side * UE_TWO_PI / Sides;
				Points.Emplace(FMath::Cos(Angle), FMath::Sin(Angle));
				Indices.Add(0);
				Indices.Add(Side + 1);
				Indices.Add((Side + 1) % Sides + 1);
			}
		}
	};

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

	struct FMinimapRegion
	{
		FMinimapRegion(FName InLevel, const FBox2D& InBounds, const TCHAR* TexturePath)
			: Level(InLevel), Bounds(InBounds)
		{
			Image.Load(TexturePath);
		}

		FName Level;
		FBox2D Bounds;
		FMinimapImage Image;
		TArray<TWeakObjectPtr<ARPGRegionGate>> Gates;
	};

	void DrawLocalTerrain(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry,
		const FMinimapRegion& Region, FVector2D PlayerUV, FVector2D Center, double PixelRadius, FLinearColor Tint)
	{
		const FSlateResourceHandle& Handle = Region.Image.Brush.GetRenderingResource();
		const FSlateShaderResourceProxy* Resource = Handle.GetResourceProxy();
		if (!Resource) return;

		const FVector2D Span = Region.Bounds.GetSize();
		const FVector2D TextureSize = FVector2D(Span.Y, Span.X) * (PixelRadius / ViewRadiusCm);
		const FVector2D TextureTopLeft = Center - PlayerUV * TextureSize;
		const FVector2D DiscExtent(PixelRadius, PixelRadius);
		const FVector2D ClipMin = FVector2D::Max(TextureTopLeft, Center - DiscExtent);
		const FVector2D ClipMax = FVector2D::Min(TextureTopLeft + TextureSize, Center + DiscExtent);

		// Clip to the authored texture extent so its edge pixels do not stretch into unexplored space.
		Elements.PushClip(FSlateClippingZone(Geometry.ToPaintGeometry(ClipMax - ClipMin, FSlateLayoutTransform(ClipMin))));
		static const FMinimapDisc Disc;
		TArray<FSlateVertex> Vertices;
		Vertices.Reserve(Disc.Points.Num());
		const FColor VertexTint = Tint.ToFColor(false);
		for (const FVector2D& Point : Disc.Points)
		{
			const FVector2D Position = Center + Point * PixelRadius;
			const FVector2f UV((Position - TextureTopLeft) / TextureSize);
			Vertices.Add(FSlateVertex::Make(Geometry.GetAccumulatedRenderTransform(), FVector2f(Position),
				Resource->StartUV + UV * Resource->SizeUV, VertexTint));
		}
		FSlateDrawElement::MakeCustomVerts(Elements, Layer, Handle, Vertices, Disc.Indices, nullptr, 0, 0);
		Elements.PopClip();
	}

	class SRPGRegionMinimap final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRPGRegionMinimap) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& Args, APawn& InPawn)
		{
			Pawn = &InPawn;
			SetVisibility(EVisibility::HitTestInvisible);
			ForceVolatile(true);

			Frame.Load(TEXT("/Game/Minimap/T_UI_HUD_Map_Bg_02.T_UI_HUD_Map_Bg_02"));
			PlayerIcon.Load(TEXT("/Game/Minimap/T_UI_Icons_MainHud_Compass_Marker_01.T_UI_Icons_MainHud_Compass_Marker_01"));
			PortalIcon.Load(TEXT("/Game/Minimap/T_UI_Icons_Map_Portal_01.T_UI_Icons_Map_Portal_01"));
			Regions.Reserve(2);
			Regions.Emplace(NAME_None, FBox2D(FVector2D(-1800, -1800), FVector2D(1800, 1800)),
				TEXT("/Game/Minimap/T_TownMinimap.T_TownMinimap"));
			Regions.Emplace(FName(TEXT("/Game/Maps/RegionTravel/L_Field")),
				FBox2D(FVector2D(47600, -7800), FVector2D(63200, 7800)),
				TEXT("/Game/Minimap/T_FieldMinimap.T_FieldMinimap"));

			for (TActorIterator<ARPGRegionGate> It(InPawn.GetWorld()); It; ++It)
			{
				if (It->GetLevel() != InPawn.GetWorld()->PersistentLevel || !IsValid(It->Destination) || It->Destination == *It)
				{
					continue;
				}
				const FName GateLevel = It->RegionLevel.IsNull() ? NAME_None
					: FName(UWorld::RemovePIEPrefix(It->RegionLevel.ToSoftObjectPath().GetLongPackageName()));
				if (FMinimapRegion* Region = Regions.FindByPredicate([GateLevel](const FMinimapRegion& Candidate)
					{ return Candidate.Level == GateLevel; }))
				{
					Region->Gates.Add(*It);
				}
			}
		}

		virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
		{
			return FVector2D(RPGHUDLayout::MinimapSize, RPGHUDLayout::MinimapSize);
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
			FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
		{
			const APawn* LocalPawn = Pawn.Get();
			if (!LocalPawn) return Layer;

			// Both regions share one world. Use the local pawn's position after it reaches the destination.
			const FVector PlayerLocation = LocalPawn->GetActorLocation();
			const FMinimapRegion* Region = Regions.FindByPredicate([&PlayerLocation](const FMinimapRegion& Candidate)
				{ return IsOnMap(ProjectToMap(PlayerLocation, Candidate.Bounds)); });
			if (!Region || !Region->Image.Texture.IsValid()) return Layer;

			const FVector2D PlayerUV = ProjectToMap(PlayerLocation, Region->Bounds);

			const FVector2D Size = Geometry.GetLocalSize();
			const FVector2D Center = Size * .5;
			const double PixelRadius = FMath::Min(Center.X, Center.Y) - MapInset;
			if (PixelRadius <= 0.0) return Layer;
			const FLinearColor Tint = Style.GetColorAndOpacityTint();
			if (Frame.Texture.IsValid())
			{
				FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform()),
					&Frame.Brush, ESlateDrawEffect::None, Tint);
			}
			DrawLocalTerrain(Elements, Layer + 1, Geometry, *Region, PlayerUV, Center, PixelRadius, Tint);

			const auto MarkerGeometry = [&](const FVector2D& Pixel, float Diameter)
			{
				const float Radius = Diameter / 2.f;
				return Geometry.ToPaintGeometry(FVector2D(Diameter, Diameter),
					FSlateLayoutTransform(Pixel - FVector2D(Radius, Radius)));
			};

			if (PortalIcon.Texture.IsValid())
			{
				for (const TWeakObjectPtr<ARPGRegionGate>& RegionGate : Region->Gates)
				{
					const ARPGRegionGate* Gate = RegionGate.Get();
					if (!Gate) continue;
					const FVector Delta = Gate->GetActorLocation() - PlayerLocation;
					if (Delta.SizeSquared2D() > FMath::Square(ViewRadiusCm)) continue;
					const FVector2D PortalPixel = Center + FVector2D(Delta.Y, -Delta.X) * (PixelRadius / ViewRadiusCm);
					FSlateDrawElement::MakeBox(Elements, Layer + 2, MarkerGeometry(PortalPixel, 23.f),
						&PortalIcon.Brush, ESlateDrawEffect::None, Tint);
				}
			}

			if (PlayerIcon.Texture.IsValid())
			{
				// The Gladiator arrow points down in the texture. World yaw zero points up on this map.
				const float Angle = FMath::DegreesToRadians(LocalPawn->GetActorRotation().Yaw + 180.f);
				FSlateDrawElement::MakeRotatedBox(Elements, Layer + 3, MarkerGeometry(Center, 20.f),
					&PlayerIcon.Brush, ESlateDrawEffect::None, Angle, TOptional<FVector2D>(),
					FSlateDrawElement::RelativeToElement, Tint);
			}
			return Layer + 3;
		}

	private:
		TWeakObjectPtr<APawn> Pawn;
		TArray<FMinimapRegion> Regions;
		FMinimapImage Frame;
		FMinimapImage PlayerIcon;
		FMinimapImage PortalIcon;
	};
}

bool RPGPlayerMinimap::SupportsWorld(const UWorld* World)
{
	return World && UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) == TEXT("/Game/Maps/RegionTravel/L_Town");
}

void RPGPlayerMinimap::Attach(UUserWidget& PlayerHUD, APawn& Pawn)
{
	if (!SupportsWorld(Pawn.GetWorld())) return;
	UWidgetTree* Tree = PlayerHUD.WidgetTree;
	auto* Canvas = Tree ? Cast<UCanvasPanel>(Tree->RootWidget) : nullptr;
	if (!Canvas) return;

	auto* Host = Tree->ConstructWidget<UNativeWidgetHost>(UNativeWidgetHost::StaticClass(), TEXT("PlayerMinimap"));
	Host->SetVisibility(ESlateVisibility::HitTestInvisible);
	Host->SetContent(SNew(SRPGRegionMinimap, Pawn));
	auto* Slot = Canvas->AddChildToCanvas(Host);
	Slot->SetAnchors(FAnchors(1.f, 0.f));
	Slot->SetAlignment(FVector2D(1.f, 0.f));
	Slot->SetPosition(FVector2D(-RPGHUDLayout::Edge, RPGHUDLayout::MinimapTop));
	Slot->SetSize(FVector2D(RPGHUDLayout::MinimapSize, RPGHUDLayout::MinimapSize));
}
