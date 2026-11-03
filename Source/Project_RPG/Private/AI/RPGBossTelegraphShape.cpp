#include "AI/RPGBossTelegraphShape.h"

#include "Materials/MaterialInstanceDynamic.h"

bool FRPGBossTelegraphFootprint::Build(const FRPGBossTelegraphGeometry& Geometry, FRPGBossTelegraphFootprint& Out)
{
	Out = {};
	if (!Geometry.bEnabled || Geometry.QueryTransform.ContainsNaN() ||
		!FRPGHitQueryMath::IsShapeValid(Geometry.Shape)) return false;
	const FTransform Transform = FRPGHitQueryMath::ResolveShapeTransform(Geometry.QueryTransform, Geometry.Shape);
	if (Transform.ContainsNaN()) return false;
	// Ground masks have one yaw. Refuse a tilted profile instead of showing misleading geometry.
	if (!Transform.GetUnitAxis(EAxis::Z).Equals(FVector::UpVector, 0.001f)) return false;
	const auto& Shape = Geometry.Shape;
	FVector2D Extent(Shape.Radius, Shape.Radius);
	FVector Center = Transform.GetLocation();
	switch (Shape.Type)
	{
	case ERPGHitQueryShape::Sphere:
	case ERPGHitQueryShape::Capsule:
	case ERPGHitQueryShape::Sector:
	case ERPGHitQueryShape::RingSector:
		break;
	case ERPGHitQueryShape::Box:
		Extent = FVector2D(Shape.BoxHalfExtent.X, Shape.BoxHalfExtent.Y);
		break;
	case ERPGHitQueryShape::LineSweep:
		Extent = FVector2D(Shape.Length * 0.5f + Shape.Radius, Shape.Radius);
		Center += Transform.GetUnitAxis(EAxis::X) * Shape.Length * 0.5f;
		break;
	default:
		return false;
	}
	if (Extent.ContainsNaN() || Extent.GetMin() <= 0.f || Extent.GetMax() > 10000.f ||
		FVector::Dist(Center, Transform.GetLocation()) + Extent.GetMax() > 10000.f) return false;
	Out.Shape = Shape;
	Out.ShapeTransform = Transform;
	Out.Center = Center;
	Out.DecalRotation = FRotator(-90.f, Transform.Rotator().Yaw, 0.f);
	Out.DecalSize = FVector(100.f, Extent.Y, Extent.X);
	return true;
}

void FRPGBossTelegraphFootprint::ApplyMaterialParameters(UMaterialInstanceDynamic& Material) const
{
	Material.SetScalarParameterValue(TEXT("TelegraphUseShape"), 1.f);
	Material.SetScalarParameterValue(TEXT("TelegraphShape"), static_cast<float>(Shape.Type));
	Material.SetScalarParameterValue(TEXT("TelegraphRadius"), Shape.Radius);
	Material.SetScalarParameterValue(TEXT("TelegraphInnerRadius"), Shape.InnerRadius);
	Material.SetScalarParameterValue(TEXT("TelegraphHalfAngle"), FMath::DegreesToRadians(Shape.AngleDegrees * .5f));
	Material.SetScalarParameterValue(TEXT("TelegraphHalfX"), Shape.BoxHalfExtent.X);
	Material.SetScalarParameterValue(TEXT("TelegraphHalfY"), Shape.BoxHalfExtent.Y);
	Material.SetScalarParameterValue(TEXT("TelegraphLength"), Shape.Length);
	Material.SetVectorParameterValue(TEXT("TelegraphOrigin"), FLinearColor(ShapeTransform.GetLocation()));
	Material.SetVectorParameterValue(TEXT("TelegraphForward"), FLinearColor(ShapeTransform.GetUnitAxis(EAxis::X)));
	Material.SetVectorParameterValue(TEXT("TelegraphRight"), FLinearColor(ShapeTransform.GetUnitAxis(EAxis::Y)));
}
