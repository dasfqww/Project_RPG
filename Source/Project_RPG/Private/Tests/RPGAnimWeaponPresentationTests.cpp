#if WITH_DEV_AUTOMATION_TESTS

#include "AnimInstance/Player/RPGPlayerAnimInstance.h"
#include "Async/Async.h"
#include "Character/RPGPlayer.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "RPGGameplayTags.h"
#include "Tests/RPGTestWorldCleanup.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAnimWeaponPresentationTest,
	"ProjectRPG.Animation.WeaponOwnershipAndCarrySnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGAnimWeaponPresentationTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { RPGTestWorld::Destroy(World); };
	auto* Player = World->SpawnActor<ARPGPlayer>();
	if (!TestNotNull(TEXT("Player"), Player)) return false;
	auto* Loadout = Player->FindComponentByClass<URPGPlayerWeaponLoadoutComponent>();
	auto* Combat = Player->FindComponentByClass<UPlayerCombatComponent>();
	if (!TestNotNull(TEXT("Loadout"), Loadout) || !TestNotNull(TEXT("Combat"), Combat)) return false;
	auto* Anim = NewObject<URPGPlayerAnimInstance>(Player->GetMesh());
	Anim->NativeInitializeAnimation();
	Combat->CurrentEquippedWeaponTag = RPGGameplayTags::Player_Weapon_GreatSword;
	Anim->NativeUpdateAnimation(0.f);
	TestFalse(TEXT("Stale compatibility tag does not invent a weapon"), Anim->bHasActiveWeapon);
	TestFalse(TEXT("Stale compatibility tag does not choose armed locomotion"), Anim->bUseArmedLocomotion);
	Combat->CurrentEquippedWeaponTag = FGameplayTag();
	auto* Weapon = World->SpawnActor<ARPGPlayerWeapon>();
	if (!TestNotNull(TEXT("Weapon"), Weapon)) return false;

	// The public replicated presentation is the input on every role. This is a
	// replication-state fixture, not a network transport test or persistent grant.
	for (const ENetRole Role : { ROLE_Authority, ROLE_AutonomousProxy, ROLE_SimulatedProxy })
	{
		Player->SetRole(Role);
		for (const auto State : { ERPGWeaponCarryState::Stowed, ERPGWeaponCarryState::Drawn,
			ERPGWeaponCarryState::Sheathing })
		{
			Loadout->Presentation.ActiveWeapon = Weapon;
			Loadout->Presentation.CarryState = State;
			Anim->NativeUpdateAnimation(0.f);
			TestTrue(TEXT("Active loadout is independent of missing compatibility tag and carry"), Anim->bHasActiveWeapon);
			TestEqual(TEXT("Exact carry state is captured"), Anim->WeaponCarryState, State);
			TestEqual(TEXT("Stowed uses unarmed; drawn and sheathing use armed"),
				Anim->bUseArmedLocomotion, State != ERPGWeaponCarryState::Stowed);
			Loadout->Presentation.ActiveWeapon = nullptr;
			Loadout->Presentation.CarryState = ERPGWeaponCarryState::Stowed;
			const bool bCaptured = Async(EAsyncExecution::ThreadPool, [Anim]
			{
				Anim->NativeThreadSafeUpdateAnimation(0.f);
				return Anim->bHasActiveWeapon;
			}).Get();
			TestTrue(TEXT("Later live changes do not leak into the worker snapshot"), bCaptured);
			TestEqual(TEXT("Worker preserves captured carry state"), Anim->WeaponCarryState, State);
			Anim->NativeUpdateAnimation(0.f);
			TestFalse(TEXT("Next capture clears removed weapon"), Anim->bHasActiveWeapon);
			TestFalse(TEXT("Next capture clears armed pose"), Anim->bUseArmedLocomotion);
		}
	}
	Player->SetRole(ROLE_Authority);
	Loadout->Presentation.CarryState = ERPGWeaponCarryState::Drawn;
	Anim->NativeUpdateAnimation(0.f);
	TestEqual(TEXT("Unresolved weapon reference normalizes carry"), Anim->WeaponCarryState, ERPGWeaponCarryState::Stowed);
	Loadout->Presentation.ActiveWeapon = Weapon;
	Anim->NativeUpdateAnimation(0.f);
	Anim->NativeInitializeAnimation();
	TestFalse(TEXT("Reinitialize clears weapon snapshot"), Anim->bHasActiveWeapon);
	TestFalse(TEXT("Reinitialize clears armed pose"), Anim->bUseArmedLocomotion);
	Anim->NativeUpdateAnimation(0.f);
	Anim->NativeUninitializeAnimation();
	TestFalse(TEXT("Uninitialize clears weapon snapshot"), Anim->bHasActiveWeapon);
	TestEqual(TEXT("Uninitialize clears carry"), Anim->WeaponCarryState, ERPGWeaponCarryState::Stowed);
	Anim->NativeInitializeAnimation();
	Loadout->UnregisterComponent();
	Anim->NativeUpdateAnimation(0.f);
	TestFalse(TEXT("Unregistered component cannot expose stale presentation"), Anim->bHasActiveWeapon);
	Loadout->RegisterComponent();
	Loadout->Presentation.ActiveWeapon = Weapon;
	Loadout->Presentation.CarryState = ERPGWeaponCarryState::Drawn;
	Weapon->Destroy();
	Anim->NativeUpdateAnimation(0.f);
	TestFalse(TEXT("Destroyed weapon clears armed pose"), Anim->bUseArmedLocomotion);
	auto* Orphan = NewObject<URPGPlayerAnimInstance>(NewObject<USkeletalMeshComponent>());
	Orphan->NativeInitializeAnimation();
	Orphan->NativeUpdateAnimation(0.f);
	TestFalse(TEXT("Absent owner cannot have an active weapon"), Orphan->bHasActiveWeapon);
	TestEqual(TEXT("Absent owner has neutral carry"), Orphan->WeaponCarryState, ERPGWeaponCarryState::Stowed);
	return true;
}

#endif
