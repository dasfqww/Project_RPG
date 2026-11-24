#include "UI/Minimap/RPGFieldWalkableArea.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Level.h"
#include "Engine/Texture2D.h"
#include "Engine/TriggerBox.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	constexpr double TargetCellSize = 100.0;
	constexpr double FloorClearance = 2.0;
	const FName HiddenRouteTag(TEXT("Minimap.HiddenRoute"));
	const FIntPoint Neighbors[] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

	struct FWalkingQuery
	{
		explicit FWalkingQuery(const ACharacter& Character)
			: World(*Character.GetWorld()), Movement(*Character.GetCharacterMovement()),
			Channel(Character.GetCapsuleComponent()->GetCollisionObjectType()),
			Capsule(Character.GetCapsuleComponent()->GetCollisionShape()),
			Response(Character.GetCapsuleComponent()->GetCollisionResponseToChannels()),
			Params(SCENE_QUERY_STAT(FieldOverview), false, &Character)
		{
			Params.MobilityType = EQueryMobilityType::Static;
		}

		bool FindStandingPosition(FVector2D Center, double ReferenceZ, double Range, FVector& OutPosition) const
		{
			FHitResult Hit;
			if (!World.SweepSingleByChannel(Hit, FVector(Center, ReferenceZ + Range), FVector(Center, ReferenceZ - Range),
				FQuat::Identity, Channel, Capsule, Params, Response) || !Movement.IsWalkable(Hit))
			{
				return false;
			}
			OutPosition = Hit.Location + FVector(0.0, 0.0, FloorClearance);
			return true;
		}

		bool CanWalkBetween(const FVector& Start, const FVector& End) const
		{
			if (!World.SweepTestByChannel(Start, End, FQuat::Identity, Channel, Capsule, Params, Response))
			{
				return true;
			}

			// A sweep at floor height can hit a curb the movement component can step over.
			const FVector Step(0.0, 0.0, Movement.MaxStepHeight);
			return !World.SweepTestByChannel(Start, Start + Step, FQuat::Identity, Channel, Capsule, Params, Response)
				&& !World.SweepTestByChannel(Start + Step, End + Step, FQuat::Identity, Channel, Capsule, Params, Response)
				&& !World.SweepTestByChannel(End + Step, End, FQuat::Identity, Channel, Capsule, Params, Response);
		}

		const UWorld& World;
		const UCharacterMovementComponent& Movement;
		ECollisionChannel Channel;
		FCollisionShape Capsule;
		FCollisionResponseParams Response;
		FCollisionQueryParams Params;
	};
}

void FRPGFieldWalkableArea::Begin(const ACharacter& Character, ULevel& Level, const FBox2D& Bounds,
	TConstArrayView<FVector> Entrances)
{
	FieldLevel = &Level;
	MapBounds = Bounds;
	const FVector2D Span = Bounds.GetSize();
	GridSize = FIntPoint(FMath::CeilToInt(Span.Y / TargetCellSize), FMath::CeilToInt(Span.X / TargetCellSize));
	CellSize = FVector2D(Span.Y / GridSize.X, Span.X / GridSize.Y);
	WalkableCells.Init(false, GridSize.X * GridSize.Y);
	StandingHeights.SetNumUninitialized(WalkableCells.Num());
	Pending.Reset();
	NextCell = 0;

	HiddenRoutes.Reset();
	for (TActorIterator<ATriggerBox> Box(Character.GetWorld()); Box; ++Box)
	{
		if (Box->ActorHasTag(HiddenRouteTag) &&
			(Box->GetLevel() == &Level || Box->GetLevel() == Character.GetWorld()->PersistentLevel))
		{
			HiddenRoutes.Add(*Box);
		}
	}

	const FWalkingQuery Query(Character);
	const double CellPadding = CellSize.Size() * 0.5;
	for (const FVector& Entrance : Entrances)
	{
		if (!Bounds.IsInside(FVector2D(Entrance)) || IsHidden(Entrance, CellPadding))
		{
			continue;
		}
		const FIntPoint Cell(FMath::FloorToInt((Entrance.Y - Bounds.Min.Y) / CellSize.X),
			FMath::FloorToInt((Bounds.Max.X - Entrance.X) / CellSize.Y));
		FVector StandingPosition;
		// First locate the floor immediately under the actual arrival arrow.
		FVector ArrivalFloor;
		if (Query.FindStandingPosition(FVector2D(Entrance), Entrance.Z, Query.Movement.MaxStepHeight, ArrivalFloor)
			&& Query.FindStandingPosition(CellCenter(Cell), ArrivalFloor.Z, Query.Movement.MaxStepHeight, StandingPosition)
			&& !IsHidden(StandingPosition, CellPadding)
			&& Query.CanWalkBetween(ArrivalFloor, StandingPosition) && !WalkableCells[CellIndex(Cell)])
		{
			Visit(Cell, StandingPosition.Z);
		}
	}
}

bool FRPGFieldWalkableArea::Advance(const ACharacter& Character)
{
	const FWalkingQuery Query(Character);
	const double CellPadding = CellSize.Size() * 0.5;
	const double FloorRange = FMath::Max(Query.Movement.MaxStepHeight,
		FMath::Max(CellSize.X, CellSize.Y) * FMath::Tan(FMath::DegreesToRadians(Query.Movement.GetWalkableFloorAngle())));
	const double Deadline = FPlatformTime::Seconds() + 0.002;
	const int32 MaxCells = NextCell + 256;
	while (NextCell < Pending.Num() && NextCell < MaxCells && FPlatformTime::Seconds() < Deadline)
	{
		const FIntPoint Cell = Pending[NextCell++];
		const FVector Start(CellCenter(Cell), StandingHeights[CellIndex(Cell)]);
		for (const FIntPoint Direction : Neighbors)
		{
			const FIntPoint Neighbor = Cell + Direction;
			if (Neighbor.X < 0 || Neighbor.Y < 0 || Neighbor.X >= GridSize.X || Neighbor.Y >= GridSize.Y
				|| WalkableCells[CellIndex(Neighbor)])
			{
				continue;
			}
			FVector End;
			if (Query.FindStandingPosition(CellCenter(Neighbor), Start.Z, FloorRange, End)
				&& !IsHidden(End, CellPadding) && Query.CanWalkBetween(Start, End))
			{
				Visit(Neighbor, End.Z);
			}
		}
	}
	return NextCell == Pending.Num();
}

bool FRPGFieldWalkableArea::IsHidden(const FVector& Location, double Padding) const
{
	for (const TWeakObjectPtr<ATriggerBox>& Route : HiddenRoutes)
	{
		const ATriggerBox* Box = Route.Get();
		const UBoxComponent* Shape = Box ? Cast<UBoxComponent>(Box->GetCollisionComponent()) : nullptr;
		if (Shape)
		{
			const FVector Local = Shape->GetComponentTransform().InverseTransformPositionNoScale(Location).GetAbs();
			const FVector Extent = Shape->GetScaledBoxExtent() + FVector(Padding, Padding, Padding);
			if (Local.X <= Extent.X && Local.Y <= Extent.Y && Local.Z <= Extent.Z)
			{
				return true;
			}
		}
	}
	return false;
}

FVector2D FRPGFieldWalkableArea::CellCenter(FIntPoint Cell) const
{
	return FVector2D(MapBounds.Max.X - (Cell.Y + 0.5) * CellSize.Y,
		MapBounds.Min.Y + (Cell.X + 0.5) * CellSize.X);
}

void FRPGFieldWalkableArea::Visit(FIntPoint Cell, double StandingZ)
{
	const int32 Index = CellIndex(Cell);
	WalkableCells[Index] = true;
	StandingHeights[Index] = StandingZ;
	Pending.Add(Cell);
}

UTexture2D* FRPGFieldWalkableArea::CreateTexture() const
{
	if (Pending.IsEmpty()) return nullptr;

	// Outline the boundary inside each cell, preserving transparent pixels outside the reachable area.
	constexpr int32 PixelsPerCell = 4;
	const FIntPoint Size = GridSize * PixelsPerCell;
	TArray<FColor> Pixels;
	Pixels.Init(FColor::Transparent, Size.X * Size.Y);
	for (const FIntPoint Cell : Pending)
	{
		const int32 Index = CellIndex(Cell);
		const bool bLeft = Cell.X == 0 || !WalkableCells[Index - 1];
		const bool bRight = Cell.X == GridSize.X - 1 || !WalkableCells[Index + 1];
		const bool bTop = Cell.Y == 0 || !WalkableCells[Index - GridSize.X];
		const bool bBottom = Cell.Y == GridSize.Y - 1 || !WalkableCells[Index + GridSize.X];
		for (int32 Y = 0; Y < PixelsPerCell; ++Y)
		{
			for (int32 X = 0; X < PixelsPerCell; ++X)
			{
				const bool bOutline = (bLeft && X == 0) || (bRight && X == PixelsPerCell - 1)
					|| (bTop && Y == 0) || (bBottom && Y == PixelsPerCell - 1);
				const FIntPoint Pixel = Cell * PixelsPerCell + FIntPoint(X, Y);
				Pixels[Pixel.Y * Size.X + Pixel.X] = bOutline ? FColor(30, 34, 39, 190) : FColor(148, 151, 156, 156);
			}
		}
	}
	const TConstArrayView64<uint8> ImageBytes(reinterpret_cast<const uint8*>(Pixels.GetData()),
		Pixels.Num() * sizeof(FColor));
	return UTexture2D::CreateTransient(Size.X, Size.Y, PF_B8G8R8A8, NAME_None, ImageBytes);
}
