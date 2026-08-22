#include "Combat/HitQuery/RPGHitQueryTypes.h"

#include "Engine/EngineTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGHitQueryTypes)

FRPGHitQueryFilter::FRPGHitQueryFilter()
{
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));
}

FTransform FRPGHitQueryMath::ResolveShapeTransform(
	const FTransform& QueryTransform,
	const FRPGHitQueryShape& Shape)
{
	const FVector WorldLocation = QueryTransform.TransformPositionNoScale(Shape.LocalOffset);
	const FQuat WorldRotation =
		QueryTransform.TransformRotation(Shape.LocalRotation.Quaternion()).GetNormalized();
	return FTransform(WorldRotation, WorldLocation, FVector::OneVector);
}

bool FRPGHitQueryMath::IsShapeValid(const FRPGHitQueryShape& Shape)
{
	if (Shape.LocalOffset.ContainsNaN() || Shape.LocalRotation.ContainsNaN() ||
		!FMath::IsFinite(Shape.Radius) || !FMath::IsFinite(Shape.InnerRadius) ||
		!FMath::IsFinite(Shape.Length) || !FMath::IsFinite(Shape.HalfHeight) ||
		Shape.BoxHalfExtent.ContainsNaN() || !FMath::IsFinite(Shape.AngleDegrees))
	{
		return false;
	}

	switch (Shape.Type)
	{
	case ERPGHitQueryShape::Sphere:
		return Shape.Radius > 0.0f;

	case ERPGHitQueryShape::Capsule:
		return Shape.Radius > 0.0f && Shape.HalfHeight >= Shape.Radius;

	case ERPGHitQueryShape::Box:
		return Shape.BoxHalfExtent.X > 0.0f &&
			Shape.BoxHalfExtent.Y > 0.0f &&
			Shape.BoxHalfExtent.Z > 0.0f;

	case ERPGHitQueryShape::Sector:
		return Shape.Radius > 0.0f &&
			Shape.HalfHeight >= 0.0f &&
			Shape.AngleDegrees > 0.0f &&
			Shape.AngleDegrees <= 360.0f;

	case ERPGHitQueryShape::RingSector:
		return Shape.Radius > 0.0f &&
			Shape.InnerRadius > 0.0f &&
			Shape.InnerRadius < Shape.Radius &&
			Shape.HalfHeight >= 0.0f &&
			Shape.AngleDegrees > 0.0f &&
			Shape.AngleDegrees <= 360.0f;

	case ERPGHitQueryShape::LineSweep:
		return Shape.Length > 0.0f && Shape.Radius > 0.0f;
	}

	return false;
}

bool FRPGHitQueryMath::IsPointInsideShape(
	const FTransform& ShapeTransform,
	const FRPGHitQueryShape& Shape,
	const FVector& WorldPoint)
{
	if (!IsShapeValid(Shape) || WorldPoint.ContainsNaN())
	{
		return false;
	}

	const FVector LocalPoint = ShapeTransform.InverseTransformPositionNoScale(WorldPoint);

	switch (Shape.Type)
	{
	case ERPGHitQueryShape::Sphere:
		return LocalPoint.SizeSquared() <= FMath::Square(Shape.Radius);

	case ERPGHitQueryShape::Capsule:
	{
		const float SegmentHalfLength = Shape.HalfHeight - Shape.Radius;
		const FVector ClosestPoint(
			0.0f,
			0.0f,
			FMath::Clamp(LocalPoint.Z, -SegmentHalfLength, SegmentHalfLength));
		return FVector::DistSquared(LocalPoint, ClosestPoint) <= FMath::Square(Shape.Radius);
	}

	case ERPGHitQueryShape::Box:
		return FMath::Abs(LocalPoint.X) <= Shape.BoxHalfExtent.X &&
			FMath::Abs(LocalPoint.Y) <= Shape.BoxHalfExtent.Y &&
			FMath::Abs(LocalPoint.Z) <= Shape.BoxHalfExtent.Z;

	case ERPGHitQueryShape::Sector:
	case ERPGHitQueryShape::RingSector:
	{
		if (FMath::Abs(LocalPoint.Z) > Shape.HalfHeight)
		{
			return false;
		}

		const float DistanceSquared2D =
			FMath::Square(LocalPoint.X) + FMath::Square(LocalPoint.Y);
		if (DistanceSquared2D > FMath::Square(Shape.Radius))
		{
			return false;
		}
		if (Shape.Type == ERPGHitQueryShape::RingSector &&
			DistanceSquared2D < FMath::Square(Shape.InnerRadius))
		{
			return false;
		}

		if (Shape.AngleDegrees >= 360.0f - KINDA_SMALL_NUMBER)
		{
			return true;
		}
		if (DistanceSquared2D <= UE_SMALL_NUMBER)
		{
			return Shape.Type == ERPGHitQueryShape::Sector;
		}

		const float InvDistance2D = FMath::InvSqrt(DistanceSquared2D);
		const float ForwardDot = LocalPoint.X * InvDistance2D;
		const float CosHalfAngle =
			FMath::Cos(FMath::DegreesToRadians(Shape.AngleDegrees * 0.5f));
		return ForwardDot >= CosHalfAngle;
	}

	case ERPGHitQueryShape::LineSweep:
	{
		const float ClosestX = FMath::Clamp(LocalPoint.X, 0.0f, Shape.Length);
		const FVector ClosestPoint(ClosestX, 0.0f, 0.0f);
		return FVector::DistSquared(LocalPoint, ClosestPoint) <= FMath::Square(Shape.Radius);
	}
	}

	return false;
}
