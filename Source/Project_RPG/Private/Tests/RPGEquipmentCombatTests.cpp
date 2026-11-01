#if WITH_DEV_AUTOMATION_TESTS

#include "Component/Combat/PlayerCombatComponent.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/CurveTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "Misc/AutomationTest.h"
#include "RPGItemTags.h"
#include "RPGGameplayTags.h"
#include <limits>

namespace RPGEquipmentCombatTests
{
struct FFixture
{
	UWorld* World = nullptr;
	ACharacter* Avatar = nullptr;
	AActor* Target = nullptr;
	URPGAuthoritativeEquipmentComponent* Equipment = nullptr;
	UPlayerCombatComponent* Combat = nullptr;
	ARPGPlayerWeapon* Startup = nullptr;
	ARPGPlayerWeapon* Primary = nullptr;
	ARPGPlayerWeapon* Secondary = nullptr;

	FFixture()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Avatar = World->SpawnActor<ACharacter>();
		Target = World->SpawnActor<AActor>();
		if (!Avatar || !Target) return;
		Equipment = NewObject<URPGAuthoritativeEquipmentComponent>(Avatar);
		Avatar->AddInstanceComponent(Equipment);
		Equipment->RegisterComponent();
		auto* ASC = NewObject<URPGAbilitySystemComponent>(Avatar);
		Avatar->AddInstanceComponent(ASC);
		ASC->RegisterComponent();
		ASC->InitAbilityActorInfo(Avatar, Avatar);
		Combat = NewObject<UPlayerCombatComponent>(Avatar);
		Avatar->AddInstanceComponent(Combat);
		Combat->RegisterComponent();
		FActorSpawnParameters Params;
		Params.Owner = Avatar;
		Params.Instigator = Avatar;
		Startup = World->SpawnActor<ARPGPlayerWeapon>(FVector(1000.f, 0.f, 0.f), FRotator::ZeroRotator, Params);
		Primary = World->SpawnActor<ARPGPlayerWeapon>(FVector(2000.f, 0.f, 0.f), FRotator::ZeroRotator, Params);
		Secondary = World->SpawnActor<ARPGPlayerWeapon>(FVector(3000.f, 0.f, 0.f), FRotator::ZeroRotator, Params);
		if (!Startup || !Primary || !Secondary) { Combat = nullptr; return; }
		Startup->PlayerWeaponData.WeaponBaseDamage.Value = 11.f;
		Secondary->PlayerWeaponData.WeaponBaseDamage.Value = 47.f;
		UCurveTable* CurveTable = NewObject<UCurveTable>(Primary);
		FRichCurve& Curve = CurveTable->AddRichCurve(TEXT("Test.BaseDamage"));
		Curve.AddKey(1.f, 31.f);
		Curve.AddKey(2.f, 62.f);
		Primary->PlayerWeaponData.WeaponBaseDamage.Value = 1.f;
		Primary->PlayerWeaponData.WeaponBaseDamage.Curve.CurveTable = CurveTable;
		Primary->PlayerWeaponData.WeaponBaseDamage.Curve.RowName = TEXT("Test.BaseDamage");
		Combat->RegisterSpawnedWeapon(RPGGameplayTags::Fragment_StatMod_1, Startup, true);
	}

	~FFixture()
	{
		if (World)
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	}

	FRPGEquipmentSnapshot Snapshot(const EEquipmentSlotType Slot, AActor* Actor) const
	{
		// Simulate a validated owner read model; this fixture never grants persistent items.
		FRPGEquipmentSnapshot Result;
		Result.ItemId = FGuid::NewGuid();
		Result.DefinitionId = FPrimaryAssetId(TEXT("RPGItemDefinition"), TEXT("CombatFixture"));
		Result.DefinitionVersion = 1;
		Result.Revision = 9007199254740993LL;
		Result.SlotType = Slot;
		Result.WeaponType = ERPGGladiatorWeaponType::GreatSword;
		Result.WeaponHandType = EWeaponHandType::TwoHand;
		Result.EquippedActor = Actor;
		return Result;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentCombatReadTest,
	"ProjectRPG.Item.EquipmentCombat.NativeActiveWeaponAndAuthoredDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentCombatReadTest::RunTest(const FString&)
{
	using namespace RPGEquipmentCombatTests;
	FFixture F;
	if (!TestNotNull(TEXT("Combat fixture"), F.Combat)) return false;
	TestEqual(TEXT("Startup compatibility before native weapons"), F.Combat->GetPlayerCurrentEquippedWeapon(), F.Startup);
	TestEqual(TEXT("Startup base damage remains authored"), F.Combat->GetPlayerCurrentEquipWeaponDamageAtLevel(1.f), 11.f);
	TestEqual(TEXT("Legacy startup retains its own type"), F.Combat->GetCharacterCurrentEquippedWeaponType(), F.Startup->GetWeaponType());
	F.Equipment->EquipmentSnapshots = {
		F.Snapshot(EEquipmentSlotType::Weapon_Primary_R, F.Primary),
		F.Snapshot(EEquipmentSlotType::Weapon_Secondary_R, F.Secondary)
	};
	TestEqual(TEXT("Native actor overrides the startup tag map"), F.Combat->GetPlayerCurrentEquippedWeapon(), F.Primary);
	TestEqual(TEXT("Shared native actor has no strict type"), F.Primary->GetWeaponType(), ERPGGladiatorWeaponType::Count);
	TestEqual(TEXT("Combat reads native item identity"), F.Combat->GetCharacterCurrentEquippedWeaponType(), ERPGGladiatorWeaponType::GreatSword);
	F.Equipment->EquipmentSnapshots[0].WeaponType = ERPGGladiatorWeaponType::Bow;
	TestEqual(TEXT("Actor reuse cannot disguise a different native type"), F.Combat->GetCharacterCurrentEquippedWeaponType(), ERPGGladiatorWeaponType::Bow);
	F.Equipment->EquipmentSnapshots[0].WeaponType = ERPGGladiatorWeaponType::Count;
	TestEqual(TEXT("Unknown native identity stays unknown"), F.Combat->GetCharacterCurrentEquippedWeaponType(), ERPGGladiatorWeaponType::Count);
	F.Equipment->EquipmentSnapshots[0].WeaponType = ERPGGladiatorWeaponType::GreatSword;
	TestEqual(TEXT("Native damage evaluates its own curve"), F.Combat->GetPlayerCurrentEquipWeaponDamageAtLevel(2.f), 62.f);
	F.Equipment->EquipmentSnapshots[0].EquippedActor = nullptr;
	TestNull(TEXT("Pending actor cannot borrow startup weapon"), F.Combat->GetPlayerCurrentEquippedWeapon());
	TestEqual(TEXT("Pending actor cannot borrow startup identity"), F.Combat->GetCharacterCurrentEquippedWeaponType(), ERPGGladiatorWeaponType::Count);
	TestEqual(TEXT("Pending actor damage fails closed"), F.Combat->GetPlayerCurrentEquipWeaponDamageAtLevel(1.f), 0.f);
	F.Equipment->EquipmentSnapshots[0].EquippedActor = F.Primary;
	F.Equipment->SetActiveEquipState(EEquipState::WeaponSet_Secondary);
	TestEqual(TEXT("Secondary set resolves its own actor"), F.Combat->GetPlayerCurrentEquippedWeapon(), F.Secondary);
	TestEqual(TEXT("Secondary damage is not the primary curve"), F.Combat->GetPlayerCurrentEquipWeaponDamageAtLevel(1.f), 47.f);
	for (const EEquipState State : { EEquipState::None, EEquipState::Utility })
	{
		F.Equipment->SetActiveEquipState(State);
		TestNull(TEXT("Inactive state cannot bypass via startup tag"), F.Combat->GetPlayerCurrentEquippedWeapon());
		TestEqual(TEXT("Inactive state has no equipped identity"), F.Combat->GetCharacterCurrentEquippedWeaponType(), ERPGGladiatorWeaponType::Count);
		TestEqual(TEXT("Inactive weapon has no damage"), F.Combat->GetPlayerCurrentEquipWeaponDamageAtLevel(1.f), 0.f);
	}
	F.Equipment->SetActiveEquipState(EEquipState::WeaponSet_Primary);
	F.Equipment->EquipmentSnapshots[0].EquippedActor = F.Target;
	TestEqual(TEXT("Non-weapon actor has no weapon damage"), F.Combat->GetPlayerCurrentEquipWeaponDamageAtLevel(1.f), 0.f);
	F.Equipment->EquipmentSnapshots[0].EquippedActor = F.Primary;
	TestEqual(TEXT("Invalid level fails closed"),
		F.Combat->GetPlayerCurrentEquipWeaponDamageAtLevel(std::numeric_limits<float>::quiet_NaN()), 0.f);
	F.Primary->Destroy();
	TestNull(TEXT("Destroyed native actor cannot borrow startup"), F.Combat->GetPlayerCurrentEquippedWeapon());
	TestEqual(TEXT("Destroyed native actor has no identity"), F.Combat->GetCharacterCurrentEquippedWeaponType(), ERPGGladiatorWeaponType::Count);
	F.Equipment->EquipmentSnapshots.Reset();
	F.Startup->Destroy();
	TestNull(TEXT("Destroyed startup actor is not a dangling map entry"),
		F.Combat->GetPlayerHasWeaponByTag(RPGGameplayTags::Fragment_StatMod_1));
	F.Combat->ToggleWeaponCollision(true); // No check/crash for missing weapons.
	TestEqual(TEXT("Missing weapon safely returns zero damage"), F.Combat->GetPlayerCurrentEquipWeaponDamageAtLevel(1.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentCombatCollisionTest,
	"ProjectRPG.Item.EquipmentCombat.SwitchStopsOldCollisionAndStaleEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentCombatCollisionTest::RunTest(const FString&)
{
	using namespace RPGEquipmentCombatTests;
	FFixture F;
	if (!TestNotNull(TEXT("Combat fixture"), F.Combat)) return false;
	F.Combat->ToggleWeaponCollision(true);
	TestEqual(TEXT("Startup collision can be enabled on server"), F.Startup->GetWeaponCollisionBox()->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	const FOnTargetInteractedDelegate StaleStartupHit = F.Startup->OnWeaponHitTarget;
	F.Equipment->EquipmentSnapshots = {
		F.Snapshot(EEquipmentSlotType::Weapon_Primary_R, F.Primary),
		F.Snapshot(EEquipmentSlotType::Weapon_Secondary_R, F.Secondary)
	};
	F.Equipment->RefreshSnapshotActors(); // No applied resources in this fixture; actors become pending.
	TestEqual(TEXT("New native presence retires startup collision"), F.Startup->GetWeaponCollisionBox()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	F.Equipment->EquipmentSnapshots[0].EquippedActor = F.Primary;
	F.Equipment->EquipmentSnapshots[1].EquippedActor = F.Secondary;
	F.Equipment->OnRep_EquipmentSnapshots();
	F.Combat->ToggleWeaponCollision(true);
	TestEqual(TEXT("Native active weapon collision enabled"), F.Primary->GetWeaponCollisionBox()->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	TestTrue(TEXT("Native actor events bound to combat without a second tag registry"), F.Primary->OnWeaponHitTarget.IsBoundToObject(F.Combat));
	const FOnTargetInteractedDelegate StalePrimaryHit = F.Primary->OnWeaponHitTarget;
	F.Primary->OnWeaponHitTarget.ExecuteIfBound(F.Target);
	TestEqual(TEXT("Active server weapon dispatches a hit"), F.Combat->OverlappedActors.Num(), 1);
	F.Equipment->OnRep_EquipmentSnapshots();
	TestEqual(TEXT("Same-actor view refresh preserves current attack window"), F.Primary->GetWeaponCollisionBox()->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	F.Equipment->SetActiveEquipState(EEquipState::WeaponSet_Secondary);
	TestEqual(TEXT("Set switch stops previous collision immediately"), F.Primary->GetWeaponCollisionBox()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("Old actor hit binding released"), F.Primary->OnWeaponHitTarget.IsBoundToObject(F.Combat));
	TestTrue(TEXT("Set switch clears per-window hit cache"), F.Combat->OverlappedActors.IsEmpty());
	TestEqual(TEXT("Switch alone does not begin a new attack"), F.Secondary->GetWeaponCollisionBox()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	F.Combat->ToggleWeaponCollision(true);
	// Even callbacks queued before unbinding must retain and validate their original source.
	StaleStartupHit.ExecuteIfBound(F.Target);
	StalePrimaryHit.ExecuteIfBound(F.Target);
	TestTrue(TEXT("Inactive source cannot dispatch stale hit events"), F.Combat->OverlappedActors.IsEmpty());
	F.Secondary->OnWeaponHitTarget.ExecuteIfBound(F.Target);
	TestEqual(TEXT("New active source dispatches normally"), F.Combat->OverlappedActors.Num(), 1);
	F.Equipment->EquipmentSnapshots[1].EquippedActor = nullptr;
	F.Equipment->OnRep_EquipmentSnapshots();
	TestEqual(TEXT("Actor loss disables tracked old actor, not a newly resolved one"), F.Secondary->GetWeaponCollisionBox()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	F.Combat->ToggleWeaponCollision(false);
	F.Equipment->EquipmentSnapshots[1].EquippedActor = F.Secondary;
	F.Equipment->OnRep_EquipmentSnapshots();
	F.Combat->ToggleWeaponCollision(true);
	F.Combat->UnregisterComponent();
	TestEqual(TEXT("Unregister stops collision"), F.Secondary->GetWeaponCollisionBox()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("Unregister releases native delegates"), F.Secondary->OnWeaponHitTarget.IsBoundToObject(F.Combat));
	F.Combat->RegisterComponent();
	F.Combat->ToggleWeaponCollision(true);
	F.Equipment->SetActiveEquipState(EEquipState::None);
	TestEqual(TEXT("Reregister restores state-change cleanup"), F.Secondary->GetWeaponCollisionBox()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentCombatAuthorityTest,
	"ProjectRPG.Item.EquipmentCombat.ClientCannotEnableOrDispatchWeaponHits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentCombatAuthorityTest::RunTest(const FString&)
{
	using namespace RPGEquipmentCombatTests;
	FFixture F;
	if (!TestNotNull(TEXT("Combat fixture"), F.Combat)) return false;
	F.Equipment->EquipmentSnapshots = { F.Snapshot(EEquipmentSlotType::Weapon_Primary_R, F.Primary) };
	F.Equipment->OnRep_EquipmentSnapshots();
	F.Avatar->SetRole(ROLE_SimulatedProxy);
	TestEqual(TEXT("Client may read replicated active weapon"), F.Combat->GetPlayerCurrentEquippedWeapon(), F.Primary);
	F.Combat->ToggleWeaponCollision(true);
	TestEqual(TEXT("Client notify cannot enable hit box"), F.Primary->GetWeaponCollisionBox()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestTrue(TEXT("Client does not start an attack window"), !F.Combat->CollisionWeapon.IsValid());
	F.Avatar->SetRole(ROLE_Authority);
	F.Combat->ToggleWeaponCollision(true);
	F.Avatar->SetRole(ROLE_SimulatedProxy);
	F.Primary->OnWeaponHitTarget.ExecuteIfBound(F.Target);
	TestTrue(TEXT("Client cannot dispatch even a previously bound server event"), F.Combat->OverlappedActors.IsEmpty());
	F.Combat->ToggleWeaponCollision(false);
	F.Avatar->SetRole(ROLE_Authority);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWeaponSweepTest,
	"ProjectRPG.Boss.WeaponSweep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGWeaponSweepTest::RunTest(const FString&)
{
	using namespace RPGEquipmentCombatTests;
	FFixture F;
	if (!TestNotNull(TEXT("Sweep fixture"), F.Combat)) return false;
	AAIController* SourceController = F.World->SpawnActor<AAIController>();
	AAIController* TargetController = F.World->SpawnActor<AAIController>();
	SourceController->SetGenericTeamId(FGenericTeamId(0));
	TargetController->SetGenericTeamId(FGenericTeamId(1));
	SourceController->Possess(F.Avatar);
	ACharacter* Target = F.World->SpawnActor<ACharacter>(FVector(1000.f, 0.f, 0.f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Sweep target"), Target)) return false;
	TargetController->Possess(Target);
	Target->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Overlap);
	UBoxComponent* Box = F.Startup->GetWeaponCollisionBox();
	Box->SetBoxExtent(FVector(2.f));
	Box->SetCollisionObjectType(ECC_WorldDynamic);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Box->SetGenerateOverlapEvents(true);
	const FVector Before(1000.f, -150.f, 0.f), After(1000.f, 150.f, 0.f);
	F.Startup->SetActorLocation(Before);
	int32 Hits = 0;
	bool bStopDuringHit = false;
	auto* ASC = F.Avatar->FindComponentByClass<URPGAbilitySystemComponent>();
	const FDelegateHandle HitHandle = ASC->GenericGameplayEventCallbacks.FindOrAdd(RPGGameplayTags::Shared_Event_Hit).AddLambda(
		[&](const FGameplayEventData* Event)
		{
			++Hits;
			FHitResult Receipt;
			TestEqual(TEXT("Sweep event target"), Event->Target.Get(), static_cast<const AActor*>(Target));
			TestTrue(TEXT("Only synchronous server event exposes sweep proof"), F.Combat->TryGetCurrentServerWeaponHit(Target, Receipt));
			TestEqual(TEXT("Receipt is actual physics target"), Receipt.GetActor(), static_cast<AActor*>(Target));
			TestFalse(TEXT("Receipt cannot authorize another target"), F.Combat->TryGetCurrentServerWeaponHit(F.Target, Receipt));
			if (bStopDuringHit)
			{
				F.Combat->ToggleWeaponCollision(false);
				F.Combat->ToggleWeaponCollision(true);
				TestFalse(TEXT("Stop/reopen invalidates in-flight old receipt"), F.Combat->TryGetCurrentServerWeaponHit(Target, Receipt));
			}
		});
	F.Combat->ToggleWeaponCollision(true);
	TestTrue(TEXT("Active server player window ticks"), F.Combat->IsComponentTickEnabled());
	TestFalse(TEXT("Start endpoint misses target"), Box->IsOverlappingActor(Target));
	F.Startup->SetActorLocation(After);
	TestFalse(TEXT("End endpoint misses target"), Box->IsOverlappingActor(Target));
	F.Combat->SweepCurrentWeapon();
	TestEqual(TEXT("Continuous path finds target missed by both endpoints"), Hits, 1);
	FHitResult Receipt;
	TestFalse(TEXT("Sweep receipt expires after event"), F.Combat->TryGetCurrentServerWeaponHit(Target, Receipt));
	F.Startup->SetActorLocation(Before);
	F.Combat->SweepCurrentWeapon();
	TestEqual(TEXT("Same window cannot hit twice"), Hits, 1);
	F.Combat->ToggleWeaponCollision(false);
	TestFalse(TEXT("Closed window stops tracing tick"), F.Combat->IsComponentTickEnabled());
	F.Startup->SetActorLocation(After);
	F.Combat->SweepCurrentWeapon();
	TestEqual(TEXT("Closed window cannot sweep"), Hits, 1);
	F.Combat->ToggleWeaponCollision(true);
	F.Avatar->SetRole(ROLE_SimulatedProxy);
	F.Startup->SetActorLocation(Before);
	F.Combat->SweepCurrentWeapon();
	TestEqual(TEXT("Client cannot sweep a previously opened window"), Hits, 1);
	F.Avatar->SetRole(ROLE_Authority);
	F.Combat->ToggleWeaponCollision(false);
	F.Combat->ToggleWeaponCollision(true);
	TargetController->SetGenericTeamId(FGenericTeamId(0));
	F.Startup->SetActorLocation(After);
	F.Combat->SweepCurrentWeapon();
	TestEqual(TEXT("Friendly target is rejected"), Hits, 1);
	TargetController->SetGenericTeamId(FGenericTeamId(1));
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	F.Startup->SetActorLocation(Before);
	F.Combat->SweepCurrentWeapon();
	TestEqual(TEXT("Sweep preserves authored collision filters"), Hits, 1);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	F.Startup->SetActorLocation(FVector(1000.f, 1000.f, 0.f));
	F.Combat->SweepCurrentWeapon();
	TestEqual(TEXT("Discontinuous motion fails closed within query budget"), Hits, 1);
	F.Combat->ToggleWeaponCollision(false);
	F.Startup->SetActorLocation(Before);
	F.Combat->ToggleWeaponCollision(true);
	bStopDuringHit = true;
	F.Startup->SetActorLocation(After);
	F.Combat->SweepCurrentWeapon();
	TestEqual(TEXT("A fresh window may hit once and safely stop reentrantly"), Hits, 2);
	F.Combat->ToggleWeaponCollision(false);
	bStopDuringHit = false;
	Target->SetActorLocation(FVector(1000.f, 90.f, 0.f));
	F.Startup->SetActorLocation(FVector(1000.f, 0.f, 0.f));
	F.Startup->SetActorRotation(FRotator::ZeroRotator);
	Box->SetBoxExtent(FVector(100.f, 2.f, 2.f));
	F.Combat->ToggleWeaponCollision(true);
	TestFalse(TEXT("Rotation start does not overlap"), Box->IsOverlappingActor(Target));
	F.Startup->SetActorRotation(FRotator(0.f, 180.f, 0.f));
	TestFalse(TEXT("Rotation end does not overlap"), Box->IsOverlappingActor(Target));
	F.Combat->SweepCurrentWeapon();
	TestEqual(TEXT("Rotational substeps find target with stationary box center"), Hits, 3);
	F.Combat->ToggleWeaponCollision(false);
	Target->SetActorLocation(FVector(1000.f, 200.f, 0.f));
	F.Combat->ToggleWeaponCollision(true);
	F.Startup->SetActorRotation(FRotator::ZeroRotator);
	F.Combat->SweepCurrentWeapon();
	TestEqual(TEXT("Trace does not expand authored weapon reach"), Hits, 3);
	F.Combat->ToggleWeaponCollision(false);
	ASC->GenericGameplayEventCallbacks.FindOrAdd(RPGGameplayTags::Shared_Event_Hit).Remove(HitHandle);
	return true;
}

#endif
