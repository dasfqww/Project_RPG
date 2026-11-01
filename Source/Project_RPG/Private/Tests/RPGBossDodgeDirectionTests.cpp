#include "Character/RPGAbilityMovementDirection.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossDodgeDirectionTest, "ProjectRPG.Boss.DodgeDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossDodgeDirectionTest::RunTest(const FString& Parameters)
{
	using RPGAbilityMovement::ResolveDirection;
	TestEqual(TEXT("Remote server uses validated acceleration despite zero local input"),
		ResolveDirection(FVector::ZeroVector, FVector(-2048., 0., 0.), true), FVector(-1., 0., 0.));
	TestEqual(TEXT("Remote server cannot use a conflicting stale Pawn input vector"),
		ResolveDirection(FVector(1., 0., 0.), FVector(0., -2048., 0.), true), FVector(0., -1., 0.));
	TestEqual(TEXT("Local prediction and listen host preserve local input"),
		ResolveDirection(FVector(-1., 0., 0.), FVector(2048., 0., 0.), false), FVector(-1., 0., 0.));
	const FVector Planar = ResolveDirection(FVector(3., 4., 100.), FVector::ZeroVector, false);
	TestTrue(TEXT("Direction is planar and normalized"), Planar.Equals(FVector(.6, .8, 0.)) && FMath::IsNearlyEqual(Planar.Size(), 1.));
	TestEqual(TEXT("Stationary server does not manufacture a forward roll direction"),
		ResolveDirection(FVector(1., 0., 0.), FVector::ZeroVector, true), FVector::ZeroVector);
	TestEqual(TEXT("Vertical-only input cannot become a planar displacement"),
		ResolveDirection(FVector(0., 0., 10.), FVector::ZeroVector, false), FVector::ZeroVector);
	const FVector Invalid(std::numeric_limits<double>::quiet_NaN(), 0., 0.);
	TestEqual(TEXT("Non-finite selected input fails closed"), ResolveDirection(Invalid, FVector::ZeroVector, false), FVector::ZeroVector);
	TestEqual(TEXT("Non-finite server acceleration fails closed"), ResolveDirection(FVector(1., 0., 0.), Invalid, true), FVector::ZeroVector);
	TestEqual(TEXT("Invalid unused local vector does not replace valid server movement"),
		ResolveDirection(Invalid, FVector(0., 2048., 0.), true), FVector(0., 1., 0.));
	return true;
}
#endif
