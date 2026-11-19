#if WITH_DEV_AUTOMATION_TESTS
#include "Item/RPGProjectileBase.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGProjectilePoolReuseTest, "ProjectRPG.Combat.ProjectilePool.GlacerReuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGProjectilePoolReuseTest::RunTest(const FString&)
{
	UClass* ProjectileClass = LoadClass<ARPGProjectileBase>(nullptr,
		TEXT("/Game/Blueprints/Character/NPC/Gruntling/Glacer/BP_Projectile_Glacer.BP_Projectile_Glacer_C"));
	if (!TestNotNull(TEXT("Authored Glacer projectile class"), ProjectileClass)) { return false; }

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
	if (!TestNotNull(TEXT("Game world"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();

	APawn* FirstShooter = World->SpawnActor<APawn>();
	APawn* NextShooter = World->SpawnActor<APawn>();
	AActor* Obstacle = World->SpawnActor<AActor>();
	FGameplayEffectSpecHandle FirstSpec(new FGameplayEffectSpec(
		GetDefault<UGameplayEffect>(), FGameplayEffectContextHandle(), 1.f));
	ARPGProjectileBase* First = ARPGProjectileBase::LaunchPooledProjectile(
		ProjectileClass, FVector(0, 0, 200), FRotator::ZeroRotator, FirstShooter, FirstSpec);
	if (!TestNotNull(TEXT("First launch"), First)) { return false; }
	auto* Collision = Cast<UBoxComponent>(First->GetRootComponent());
	auto* Movement = First->FindComponentByClass<UProjectileMovementComponent>();
	const FStructProperty* SpecProperty = FindFProperty<FStructProperty>(
		ARPGProjectileBase::StaticClass(), TEXT("ProjectileDamageEffectSpecHandle"));
	if (!TestNotNull(TEXT("Collision"), Collision) || !TestNotNull(TEXT("Movement"), Movement)
		|| !TestNotNull(TEXT("Damage spec property"), SpecProperty)) { return false; }
	const auto* ActiveSpec = SpecProperty->ContainerPtrToValuePtr<FGameplayEffectSpecHandle>(First);
	TestTrue(TEXT("First launch owns its damage spec"), ActiveSpec->Data == FirstSpec.Data);

	const auto Impact = [&]
	{
		Collision->OnComponentHit.Broadcast(Collision, Obstacle, nullptr, FVector::ZeroVector, FHitResult());
	};
	Impact();
	TestTrue(TEXT("Impact hides and preserves the actor"), IsValid(First) && First->IsHidden());
	TestFalse(TEXT("Retired projectile cannot collide"), First->GetActorEnableCollision());
	TestFalse(TEXT("Retired movement is inactive"), Movement->IsActive());
	TestFalse(TEXT("Retired spec has been cleared"), ActiveSpec->IsValid());
	TestNull(TEXT("Retired instigator has been cleared"), First->GetInstigator());

	ARPGProjectileBase* Concurrent = ARPGProjectileBase::LaunchPooledProjectile(
		ProjectileClass, FVector(0, 0, 400), FRotator::ZeroRotator, FirstShooter, {});
	TestTrue(TEXT("A launch in the impact frame uses a different actor"), IsValid(Concurrent) && Concurrent != First);
	if (IsValid(Concurrent)) { Concurrent->Destroy(); }

	const auto AdvanceTimers = [&](float Delta)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(Delta);
	};
	AdvanceTimers(0.01f);
	const FVector NextLocation(100, 200, 600);
	const FRotator NextRotation(0, 90, 0);
	ARPGProjectileBase* Reused = ARPGProjectileBase::LaunchPooledProjectile(
		ProjectileClass, NextLocation, NextRotation, NextShooter, {});
	if (!TestEqual(TEXT("A later flight uses the same actor"), Reused, First)) { return false; }
	TestTrue(TEXT("Reused actor is visible and collidable"), !Reused->IsHidden() && Reused->GetActorEnableCollision());
	TestTrue(TEXT("Reused flight starts at the requested position"), Reused->GetActorLocation().Equals(NextLocation));
	TestEqual(TEXT("Reused flight gets a new instigator"), Reused->GetInstigator(), NextShooter);
	TestEqual(TEXT("Reused flight gets a new owner"), Reused->GetOwner(), static_cast<AActor*>(NextShooter));
	TestTrue(TEXT("Movement is active at the new heading"),
		Movement->IsActive() && Movement->Velocity.GetSafeNormal().Equals(FVector::YAxisVector));
	TestTrue(TEXT("Reused flight has a fresh lifespan"), Reused->GetLifeSpan() > 3.9f);
	TestFalse(TEXT("Previous flight's spec cannot leak"), ActiveSpec->IsValid());

	Impact();
	TestTrue(TEXT("Reused projectile accepts another impact"), Reused->IsHidden() && !Reused->GetActorEnableCollision());
	AdvanceTimers(0.01f);
	ARPGProjectileBase* Expiring = ARPGProjectileBase::LaunchPooledProjectile(
		ProjectileClass, NextLocation, NextRotation, NextShooter, {});
	if (!TestEqual(TEXT("Projectile returns again after the second impact"), Expiring, First)) { return false; }
	AdvanceTimers(0.01f);
	AdvanceTimers(4.1f);
	TestTrue(TEXT("Lifespan expiry retires the projectile"), IsValid(Expiring) && Expiring->IsHidden());
	AdvanceTimers(0.01f);
	TestEqual(TEXT("Expired projectiles are also reusable"), ARPGProjectileBase::LaunchPooledProjectile(
		ProjectileClass, NextLocation, NextRotation, NextShooter, {}), First);
	return true;
}
#endif
