#pragma once

#include "CoreMinimal.h"

class ACharacter;
class ATriggerBox;
class ULevel;
class UTexture2D;

// Samples the field's static walking surface from its public entrances.
class FRPGFieldWalkableArea
{
public:
	void Begin(const ACharacter& Character, ULevel& Level, const FBox2D& Bounds, TConstArrayView<FVector> Entrances);
	bool IsForLevel(const ULevel& Level) const { return FieldLevel.Get() == &Level; }
	bool Advance(const ACharacter& Character);
	UTexture2D* CreateTexture() const;
	bool IsHidden(const FVector& Location, double Padding = 0.0) const;

private:
	FVector2D CellCenter(FIntPoint Cell) const;
	int32 CellIndex(FIntPoint Cell) const { return Cell.Y * GridSize.X + Cell.X; }
	void Visit(FIntPoint Cell, double StandingZ);

	TWeakObjectPtr<ULevel> FieldLevel;
	TArray<TWeakObjectPtr<ATriggerBox>> HiddenRoutes;
	FBox2D MapBounds = FBox2D(ForceInit);
	FIntPoint GridSize = FIntPoint::ZeroValue;
	FVector2D CellSize = FVector2D::ZeroVector;
	TBitArray<> WalkableCells;
	TArray<double> StandingHeights;
	TArray<FIntPoint> Pending;
	int32 EntranceCount = 0;
	int32 NextCell = 0;
};
