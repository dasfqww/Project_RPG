#if WITH_DEV_AUTOMATION_TESTS

#include "AnimInstance/Player/RPGPlayerAnimInstance.h"
#include "AnimInstance/Player/RPGPlayerLinkedAnimLayer.h"
#include "AbilitySystemComponent.h"
#include "Async/Async.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/RPGTestWorldCleanup.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAnimInstanceStateTest,
	"ProjectRPG.Animation.CapturedStateAndLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGAnimInstanceStateTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { RPGTestWorld::Destroy(World); };
	auto* Player = World->SpawnActor<ARPGPlayer>();
	if (!TestNotNull(TEXT("Player"), Player)) return false;
	auto* Anim = NewObject<URPGPlayerAnimInstance>(Player->GetMesh());
	auto* ASC = Player->GetRPGAbilitySystemComponent();
	ASC->InitAbilityActorInfo(Player, Player);
	const FGameplayTag Rage = FGameplayTag::RequestGameplayTag(TEXT("Player.Status.Rage.Active"));
	const FGameplayTag RageParent = FGameplayTag::RequestGameplayTag(TEXT("Player.Status.Rage"));
	Anim->NativeInitializeAnimation();
	ASC->AddLooseGameplayTag(Rage);
	Player->GetCharacterMovement()->Velocity = FVector(300.f, 400.f, 200.f);
	Player->SetActorRotation(FRotator::ZeroRotator);
	Anim->NativeUpdateAnimation(0.f);

	// Changes after collection must not leak into worker-thread graph evaluation.
	Player->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	Player->SetActorRotation(FRotator(0.f, 90.f, 0.f));
	ASC->RemoveLooseGameplayTag(Rage);
	const bool bCapturedRage = Async(EAsyncExecution::ThreadPool, [Anim, Rage]
	{
		Anim->NativeThreadSafeUpdateAnimation(0.f);
		return Anim->DoesOwnerHaveTag(Rage);
	}).Get();
	TestEqual(TEXT("Worker uses captured horizontal velocity"), Anim->GroundSpeed, 500.f);
	TestTrue(TEXT("Worker uses captured facing"), FMath::IsNearlyEqual(Anim->LocomotionDirection, 53.1301f, 0.01f));
	TestTrue(TEXT("Tag snapshot survives live tag removal"), bCapturedRage);
	TestTrue(TEXT("Parent tag matching is preserved"), Anim->DoesOwnerHaveTag(RageParent));
	Anim->NativeUpdateAnimation(0.f);
	Anim->NativeThreadSafeUpdateAnimation(0.f);
	TestEqual(TEXT("Next collection sees new velocity"), Anim->GroundSpeed, 0.f);
	TestFalse(TEXT("Next collection sees removed tag"), Anim->DoesOwnerHaveTag(Rage));

	Anim->NativeThreadSafeUpdateAnimation(4.99f);
	TestFalse(TEXT("Relax threshold preserved below five seconds"), Anim->bShouldEnterRelaxState);
	Anim->NativeThreadSafeUpdateAnimation(0.02f);
	TestTrue(TEXT("Relax threshold preserved above five seconds"), Anim->bShouldEnterRelaxState);
	Anim->NativeInitializeAnimation();
	TestEqual(TEXT("Reinitialize clears elapsed idle time"), Anim->IdleElapsedTime, 0.f);
	TestFalse(TEXT("Reinitialize clears relaxed pose"), Anim->bShouldEnterRelaxState);
	TestEqual(TEXT("Reinitialize clears previous movement output"), Anim->GroundSpeed, 0.f);
	TestEqual(TEXT("Player reference restored"), Anim->OwningPlayerCharacter.Get(), Player);
	Anim->NativeUpdateAnimation(0.f);
	Anim->NativeThreadSafeUpdateAnimation(6.f);
	Anim->NativeUninitializeAnimation();
	TestNull(TEXT("Uninitialize clears character"), Anim->OwningCharacter.Get());
	TestNull(TEXT("Uninitialize clears movement component"), Anim->OwningMovementComponent.Get());
	TestNull(TEXT("Uninitialize clears player"), Anim->OwningPlayerCharacter.Get());
	TestFalse(TEXT("Uninitialize clears relaxation"), Anim->bShouldEnterRelaxState);
	TestFalse(TEXT("Uninitialize invalidates captured frame"), Anim->LocomotionFrame.bValid);
	TestEqual(TEXT("Uninitialize clears direction"), Anim->LocomotionDirection, 0.f);

	auto* OrphanMesh = NewObject<USkeletalMeshComponent>();
	auto* OrphanAnim = NewObject<URPGPlayerAnimInstance>(OrphanMesh);
	OrphanAnim->NativeInitializeAnimation();
	OrphanAnim->NativeUpdateAnimation(0.f);
	OrphanAnim->NativeThreadSafeUpdateAnimation(100.f);
	TestFalse(TEXT("Absent owner never enters relaxation"), OrphanAnim->bShouldEnterRelaxState);
	TestEqual(TEXT("Absent owner has zero speed"), OrphanAnim->GroundSpeed, 0.f);
	auto* Linked = NewObject<URPGPlayerLinkedAnimLayer>(OrphanMesh);
	Linked->NativeInitializeAnimation();
	Linked->NativeUpdateAnimation(0.f);
	TestNull(TEXT("Unbound linked layer returns null"), Linked->GetPlayerAnimInstance());
	Linked->NativeUninitializeAnimation();
	TestNull(TEXT("Uninitialized linked layer returns null"), Linked->GetPlayerAnimInstance());

	// The shared animation base depends on the GAS contract, not the RPG ASC subtype.
	auto* GenericPawn = World->SpawnActor<ACharacter>();
	auto* GenericASC = NewObject<UAbilitySystemComponent>(GenericPawn);
	GenericPawn->AddInstanceComponent(GenericASC);
	GenericASC->RegisterComponent();
	GenericASC->InitAbilityActorInfo(GenericPawn, GenericPawn);
	GenericASC->AddLooseGameplayTag(Rage);
	auto* GenericAnim = NewObject<URPGBaseAnimInstance>(GenericPawn->GetMesh());
	GenericAnim->NativeInitializeAnimation();
	GenericAnim->NativeUpdateAnimation(0.f);
	TestTrue(TEXT("Generic GAS owner is supported"), GenericAnim->DoesOwnerHaveTag(Rage));
	GenericAnim->NativeUninitializeAnimation();
	TestFalse(TEXT("Uninitialize clears captured tags"), GenericAnim->DoesOwnerHaveTag(Rage));
	return true;
}

#endif
