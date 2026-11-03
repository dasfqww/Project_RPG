#pragma once

#include "Combat/HitQuery/RPGHitQueryTypes.h"
#include "RPGBossTelegraphShape.generated.h"

class UMaterialInstanceDynamic;

/** Public presentation facts captured from the server's actual hit query, not a damage permission. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGBossTelegraphGeometry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Boss|Telegraph")
	bool bEnabled = false;
	UPROPERTY(BlueprintReadOnly, Category = "Boss|Telegraph")
	FTransform QueryTransform = FTransform::Identity;
	UPROPERTY(BlueprintReadOnly, Category = "Boss|Telegraph")
	FRPGHitQueryShape Shape;
};

/** Ground footprint. Decal Z maps to forward, decal Y to right; shader uses world-space axes. */
struct PROJECT_RPG_API FRPGBossTelegraphFootprint
{
	FTransform ShapeTransform = FTransform::Identity;
	FVector Center = FVector::ZeroVector;
	FRotator DecalRotation = FRotator(-90.f, 0.f, 0.f);
	FVector DecalSize = FVector::ZeroVector;
	FRPGHitQueryShape Shape;
	static bool Build(const FRPGBossTelegraphGeometry& Geometry, FRPGBossTelegraphFootprint& Out);
	void ApplyMaterialParameters(UMaterialInstanceDynamic& Material) const;
};
