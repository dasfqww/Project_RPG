#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/HitQuery/RPGHitQueryTypes.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGHitQuerySectorMathTest,
	"ProjectRPG.Combat.HitQuery.SectorMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGHitQuerySectorMathTest::RunTest(const FString& Parameters)
{
	FRPGHitQueryShape Shape;
	Shape.Type = ERPGHitQueryShape::Sector;
	Shape.Radius = 500.0f;
	Shape.AngleDegrees = 90.0f;
	Shape.HalfHeight = 100.0f;

	const FTransform Transform = FTransform::Identity;
	TestTrue(
		TEXT("Point in front is inside"),
		FRPGHitQueryMath::IsPointInsideShape(
			Transform, Shape, FVector(400.0f, 0.0f, 0.0f)));
	TestTrue(
		TEXT("Point inside the angular boundary is inside"),
		FRPGHitQueryMath::IsPointInsideShape(
			Transform, Shape, FVector(300.0f, 200.0f, 0.0f)));
	TestFalse(
		TEXT("Point outside the angular boundary is rejected"),
		FRPGHitQueryMath::IsPointInsideShape(
			Transform, Shape, FVector(100.0f, 300.0f, 0.0f)));
	TestFalse(
		TEXT("Point beyond radius is rejected"),
		FRPGHitQueryMath::IsPointInsideShape(
			Transform, Shape, FVector(501.0f, 0.0f, 0.0f)));
	TestFalse(
		TEXT("Point beyond vertical tolerance is rejected"),
		FRPGHitQueryMath::IsPointInsideShape(
			Transform, Shape, FVector(300.0f, 0.0f, 101.0f)));

	const FTransform RotatedTransform(FRotator(0.0f, 90.0f, 0.0f));
	TestTrue(
		TEXT("Shape orientation is respected"),
		FRPGHitQueryMath::IsPointInsideShape(
			RotatedTransform, Shape, FVector(0.0f, 300.0f, 0.0f)));
	TestFalse(
		TEXT("World forward is rejected after rotating the shape"),
		FRPGHitQueryMath::IsPointInsideShape(
			RotatedTransform, Shape, FVector(300.0f, 0.0f, 0.0f)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGHitQueryRingSectorMathTest,
	"ProjectRPG.Combat.HitQuery.RingSectorMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGHitQueryRingSectorMathTest::RunTest(const FString& Parameters)
{
	FRPGHitQueryShape Shape;
	Shape.Type = ERPGHitQueryShape::RingSector;
	Shape.InnerRadius = 200.0f;
	Shape.Radius = 500.0f;
	Shape.AngleDegrees = 120.0f;
	Shape.HalfHeight = 100.0f;

	TestFalse(
		TEXT("Ring center hole is rejected"),
		FRPGHitQueryMath::IsPointInsideShape(
			FTransform::Identity, Shape, FVector(100.0f, 0.0f, 0.0f)));
	TestTrue(
		TEXT("Point between ring radii is accepted"),
		FRPGHitQueryMath::IsPointInsideShape(
			FTransform::Identity, Shape, FVector(300.0f, 0.0f, 0.0f)));
	TestFalse(
		TEXT("Point beyond outer radius is rejected"),
		FRPGHitQueryMath::IsPointInsideShape(
			FTransform::Identity, Shape, FVector(600.0f, 0.0f, 0.0f)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGHitQueryPrimitiveMathTest,
	"ProjectRPG.Combat.HitQuery.PrimitiveMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGHitQueryPrimitiveMathTest::RunTest(const FString& Parameters)
{
	FRPGHitQueryShape Shape;
	Shape.Type = ERPGHitQueryShape::Box;
	Shape.BoxHalfExtent = FVector(100.0f, 50.0f, 25.0f);
	TestTrue(
		TEXT("Point inside box is accepted"),
		FRPGHitQueryMath::IsPointInsideShape(
			FTransform::Identity, Shape, FVector(90.0f, 40.0f, 20.0f)));
	TestFalse(
		TEXT("Point outside box is rejected"),
		FRPGHitQueryMath::IsPointInsideShape(
			FTransform::Identity, Shape, FVector(101.0f, 0.0f, 0.0f)));

	Shape.Type = ERPGHitQueryShape::LineSweep;
	Shape.Length = 500.0f;
	Shape.Radius = 50.0f;
	TestTrue(
		TEXT("Point inside line sweep radius is accepted"),
		FRPGHitQueryMath::IsPointInsideShape(
			FTransform::Identity, Shape, FVector(250.0f, 40.0f, 0.0f)));
	TestFalse(
		TEXT("Point behind line sweep start is rejected"),
		FRPGHitQueryMath::IsPointInsideShape(
			FTransform::Identity, Shape, FVector(-60.0f, 0.0f, 0.0f)));

	return true;
}

#endif
