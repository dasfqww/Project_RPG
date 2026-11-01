#if WITH_DEV_AUTOMATION_TESTS

#include "Ability/Player/RPGGameplayAbility_WeaponStance.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Component/Equipment/RPGPlayerWeaponLoadoutComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InputMappingContext.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "Misc/AutomationTest.h"
#include "Net/UnrealNetwork.h"
#include "RPGGameplayTags.h"
#include "UObject/UnrealType.h"

namespace RPGWeaponLoadoutTests
{
struct FFixture
{
	UWorld* World = nullptr;
	ACharacter* Avatar = nullptr;
	APlayerController* Controller = nullptr;
	URPGAbilitySystemComponent* ASC = nullptr;
	URPGAuthoritativeEquipmentComponent* Equipment = nullptr;
	UPlayerCombatComponent* Combat = nullptr;
	URPGPlayerWeaponLoadoutComponent* Loadout = nullptr;
	ARPGPlayerWeapon* Startup = nullptr;
	ARPGPlayerWeapon* Primary = nullptr;
	ARPGPlayerWeapon* Secondary = nullptr;
	FGameplayAbilitySpecHandle EquipHandle;

	FFixture()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Avatar = World->SpawnActor<ACharacter>();
		Controller = World->SpawnActor<APlayerController>();
		if (!Avatar || !Controller) return;
		Controller->SetAsLocalPlayerController(); // Standalone fixture, no remote activation RPC.
		Controller->Possess(Avatar);
		Equipment = Add<URPGAuthoritativeEquipmentComponent>();
		ASC = Add<URPGAbilitySystemComponent>();
		ASC->InitAbilityActorInfo(Avatar, Avatar);
		Combat = Add<UPlayerCombatComponent>();
		FActorSpawnParameters Params;
		Params.Owner = Avatar;
		Params.Instigator = Avatar;
		Startup = World->SpawnActor<ARPGPlayerWeapon>(FVector(1000.f, 0.f, 0.f), FRotator::ZeroRotator, Params);
		Primary = World->SpawnActor<ARPGPlayerWeapon>(FVector(2000.f, 0.f, 0.f), FRotator::ZeroRotator, Params);
		Secondary = World->SpawnActor<ARPGPlayerWeapon>(FVector(3000.f, 0.f, 0.f), FRotator::ZeroRotator, Params);
		if (!Startup || !Primary || !Secondary) return;
		for (auto* Weapon : { Startup, Primary, Secondary })
		{
			FRPGPlayerAbilitySet Default;
			Default.InputTag = RPGGameplayTags::InputTag_UnequipWeapon;
			Default.AbilityToGrant = URPGGameplayAbility_WeaponStance::StaticClass();
			Weapon->PlayerWeaponData.DefaultWeaponAbilities = { Default, FRPGPlayerAbilitySet() };
			FRPGPlayerSkillSet Special;
			Special.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.Skill1"));
			Special.AbilityToGrant = URPGGameplayAbility::StaticClass();
			Weapon->PlayerWeaponData.SpecialWeaponAbilities = { Special };
		}
		Combat->RegisterSpawnedWeapon(RPGGameplayTags::InputTag_EquipWeapon, Startup, true);
		FGameplayAbilitySpec Equip(URPGGameplayAbility_WeaponStance::StaticClass(), 3);
		Equip.SourceObject = Avatar;
		Equip.GetDynamicSpecSourceTags().AddTag(RPGGameplayTags::InputTag_EquipWeapon);
		EquipHandle = ASC->GiveAbility(Equip);
		Loadout = Add<URPGPlayerWeaponLoadoutComponent>();
	}

	template <typename T> T* Add()
	{
		T* Component = NewObject<T>(Avatar);
		Avatar->AddInstanceComponent(Component);
		Component->RegisterComponent();
		return Component;
	}

	FRPGEquipmentSnapshot Snapshot(EEquipmentSlotType Slot, ARPGPlayerWeapon* Weapon) const
	{
		// Validated read-model fixture only: never a database item grant.
		FRPGEquipmentSnapshot Result;
		Result.ItemId = FGuid::NewGuid();
		Result.DefinitionId = FPrimaryAssetId(TEXT("RPGItemDefinition"), TEXT("LoadoutFixture"));
		Result.DefinitionVersion = 1;
		Result.Revision = 9007199254740993LL;
		Result.SlotType = Slot;
		Result.EquippedActor = Weapon;
		return Result;
	}

	~FFixture()
	{
		if (World)
		{
			if (Avatar) Avatar->SetRole(ROLE_Authority);
			RPGTestWorld::Destroy(World);
		}
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWeaponLoadoutLifecycleTest,
	"ProjectRPG.Item.WeaponLoadout.ActiveGrantLifecycleAndLegacyRetirement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGWeaponLoadoutLifecycleTest::RunTest(const FString&)
{
	using namespace RPGWeaponLoadoutTests;
	FFixture F;
	if (!TestNotNull(TEXT("Loadout fixture"), F.Loadout)) return false;
	TArray<FGameplayAbilitySpecHandle> Legacy;
	F.ASC->GrantPlayerWeaponAbilities(F.Startup->PlayerWeaponData.DefaultWeaponAbilities,
		F.Startup->PlayerWeaponData.SpecialWeaponAbilities, 3, Legacy);
	FGameplayAbilitySpec Foreign(URPGGameplayAbility::StaticClass(), 7);
	Foreign.SourceObject = F.Secondary;
	const auto ForeignHandle = F.ASC->GiveAbility(Foreign);
	Legacy.Add(ForeignHandle);
	F.Startup->AssignGrantedAbilitySpecHandles(Legacy);
	F.Equipment->EquipmentSnapshots = {
		F.Snapshot(EEquipmentSlotType::Weapon_Primary_R, F.Primary),
		F.Snapshot(EEquipmentSlotType::Weapon_Secondary_R, F.Secondary)
	};
	F.Equipment->OnRep_EquipmentSnapshots();
	TestTrue(TEXT("Native presence retires startup visibility"), F.Startup->IsHidden());
	TestNull(TEXT("Legacy default grant removed"), F.ASC->FindAbilitySpecFromHandle(Legacy[0]));
	TestNull(TEXT("Legacy special grant removed"), F.ASC->FindAbilitySpecFromHandle(Legacy[1]));
	TestNotNull(TEXT("Foreign grant preserved"), F.ASC->FindAbilitySpecFromHandle(ForeignHandle));
	if (!TestEqual(TEXT("Only valid authored abilities granted"), F.Loadout->GrantedHandles.Num(), 2)) return false;
	for (const auto Handle : F.Loadout->GrantedHandles)
	{
		const auto* Spec = F.ASC->FindAbilitySpecFromHandle(Handle);
		if (!TestNotNull(TEXT("Active spec"), Spec)) return false;
		TestEqual(TEXT("Existing startup equip level retained"), Spec->Level, 3);
		TestEqual(TEXT("Native resource source is exact actor"), Spec->SourceObject.Get(), static_cast<UObject*>(F.Primary));
	}
	const auto FirstHandles = F.Loadout->GrantedHandles;
	++F.Equipment->EquipmentSnapshots[0].Revision;
	F.Equipment->OnRep_EquipmentSnapshots();
	TestTrue(TEXT("Same item and actor refresh is idempotent"), F.Loadout->GrantedHandles == FirstHandles);
	F.Equipment->SetActiveEquipState(EEquipState::None);
	TestTrue(TEXT("Stow releases active grants"), F.Loadout->GrantedHandles.IsEmpty());
	TestNull(TEXT("Stow clears old handles"), F.ASC->FindAbilitySpecFromHandle(FirstHandles[0]));
	TestTrue(TEXT("Stow retains native gate"), F.Loadout->IsNativeManaged());
	TestNull(TEXT("Stow publishes no active weapon"), F.Loadout->Presentation.ActiveWeapon.Get());
	F.Equipment->SetActiveEquipState(EEquipState::WeaponSet_Secondary);
	TestEqual(TEXT("Switch grants secondary actor"), F.Loadout->GrantedWeapon.Get(), F.Secondary);
	const auto SecondaryHandles = F.Loadout->GrantedHandles;
	if (!TestEqual(TEXT("Secondary grant batch ready"), SecondaryHandles.Num(), 2)) return false;
	F.Equipment->EquipmentSnapshots[1].EquippedActor = nullptr;
	F.Equipment->OnRep_EquipmentSnapshots();
	TestNull(TEXT("Pending actor clears previous loadout"), F.ASC->FindAbilitySpecFromHandle(SecondaryHandles[0]));
	TestTrue(TEXT("Pending actor cannot restore startup fallback"), F.Loadout->IsNativeManaged());
	F.Equipment->EquipmentSnapshots[1].EquippedActor = F.Secondary;
	F.Equipment->OnRep_EquipmentSnapshots();
	F.Loadout->UnregisterComponent();
	TestTrue(TEXT("Unregister releases owned grants"), F.Loadout->GrantedHandles.IsEmpty());
	F.Loadout->RegisterComponent();
	TestEqual(TEXT("Reregister restores native loadout exactly once"), F.Loadout->GrantedHandles.Num(), 2);
	F.Equipment->EquipmentSnapshots.Reset();
	F.Equipment->OnRep_EquipmentSnapshots();
	TestFalse(TEXT("No native records restores startup visibility"), F.Startup->IsHidden());
	TestFalse(TEXT("Native gate released only after all records removed"), F.Loadout->IsNativeManaged());
	TestNotNull(TEXT("Startup equip ability preserved"), F.ASC->FindAbilitySpecFromHandle(F.EquipHandle));
	TestNotNull(TEXT("Foreign ability still preserved"), F.ASC->FindAbilitySpecFromHandle(ForeignHandle));
	TestEqual(TEXT("No legacy auto-regrant or persistent item creation"), F.ASC->GetActivatableAbilities().Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWeaponLoadoutAuthorityTest,
	"ProjectRPG.Item.WeaponLoadout.AuthorityPrivacyAndConflicts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGWeaponLoadoutAuthorityTest::RunTest(const FString&)
{
	using namespace RPGWeaponLoadoutTests;
	FFixture F;
	if (!TestNotNull(TEXT("Loadout fixture"), F.Loadout)) return false;
	F.Equipment->EquipmentSnapshots = { F.Snapshot(EEquipmentSlotType::Weapon_Primary_R, F.Primary) };
	F.Avatar->SetRole(ROLE_SimulatedProxy);
	F.Loadout->RefreshLoadout();
	TestTrue(TEXT("Client cannot grant abilities from private snapshots"), F.Loadout->GrantedHandles.IsEmpty());
	TestFalse(TEXT("Client cannot hide startup resources"), F.Startup->IsHidden());
	F.Loadout->Presentation.bNativeManaged = true;
	F.Loadout->Presentation.ActiveWeapon = F.Primary;
	F.Loadout->OnRep_Presentation();
	TestEqual(TEXT("Public appearance has no GAS write"), F.ASC->GetActivatableAbilities().Num(), 1);
	F.Avatar->SetRole(ROLE_Authority);
	FGameplayAbilitySpec Conflict(URPGGameplayAbility::StaticClass(), 5);
	Conflict.SourceObject = F.Secondary;
	Conflict.GetDynamicSpecSourceTags().AddTag(F.Primary->PlayerWeaponData.SpecialWeaponAbilities[0].InputTag);
	const auto ConflictHandle = F.ASC->GiveAbility(Conflict);
	F.Loadout->RefreshLoadout();
	TestTrue(TEXT("Whole batch rejected before any grant on tag conflict"), F.Loadout->GrantedHandles.IsEmpty());
	TestNull(TEXT("Conflicted loadout does not publish usable appearance"), F.Loadout->Presentation.ActiveWeapon.Get());
	TestNotNull(TEXT("Conflicting foreign resource is not cleared"), F.ASC->FindAbilitySpecFromHandle(ConflictHandle));
	F.ASC->ClearAbility(ConflictHandle);
	F.Loadout->RefreshLoadout();
	TestEqual(TEXT("Retry after conflict removal succeeds"), F.Loadout->GrantedHandles.Num(), 2);
	F.ASC->ClearAbility(F.EquipHandle);
	F.Loadout->RefreshLoadout();
	TestTrue(TEXT("Missing authored level source fails closed"), F.Loadout->GrantedHandles.IsEmpty());

	TArray<FLifetimeProperty> Props;
	URPGPlayerWeaponLoadoutComponent::StaticClass()->SetUpRuntimeReplicationData();
	F.Loadout->GetLifetimeReplicatedProps(Props);
	const FProperty* View = FindFProperty<FProperty>(F.Loadout->GetClass(), TEXT("Presentation"));
	const auto* Lifetime = View ? Props.FindByPredicate([View](const FLifetimeProperty& P) { return P.RepIndex == View->RepIndex; }) : nullptr;
	TestTrue(TEXT("Appearance replicates to relevant observers, not owner only"), Lifetime && Lifetime->Condition == COND_None);
	int32 FieldCount = 0;
	for (TFieldIterator<FProperty> It(FRPGPlayerWeaponPresentation::StaticStruct()); It; ++It) ++FieldCount;
	TestEqual(TEXT("Public view only contains native gate and actor reference"), FieldCount, 2);
	TestNull(TEXT("No item GUID in public appearance"), FindFProperty<FProperty>(FRPGPlayerWeaponPresentation::StaticStruct(), TEXT("ItemId")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWeaponLoadoutInputTest,
	"ProjectRPG.Item.WeaponLoadout.InputOwnershipAndPriorityRestoration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGWeaponLoadoutInputTest::RunTest(const FString&)
{
	using namespace RPGWeaponLoadoutTests;
	FFixture F;
	if (!TestNotNull(TEXT("Loadout fixture"), F.Loadout)) return false;
	auto* Local = NewObject<ULocalPlayer>(GEngine);
	Local->PlayerController = F.Controller;
	F.Controller->PlayerInput = NewObject<UEnhancedPlayerInput>(F.Controller);
	auto* Subsystem = NewObject<UEnhancedInputLocalPlayerSubsystem>(Local);
	auto* WeaponContext = NewObject<UInputMappingContext>();
	auto* OtherContext = NewObject<UInputMappingContext>();
	Subsystem->AddMappingContext(WeaponContext, 4);
	Subsystem->AddMappingContext(OtherContext, 8);
	F.Loadout->ApplyInputContext(Subsystem, WeaponContext);
	int32 Priority = 0;
	TestTrue(TEXT("Native mapping uses authored priority"), Subsystem->HasMappingContext(WeaponContext, Priority) && Priority == 1);
	F.Loadout->ApplyInputContext(Subsystem, WeaponContext);
	F.Loadout->ReleaseInputContext();
	TestTrue(TEXT("Repeated refresh preserves original priority"), Subsystem->HasMappingContext(WeaponContext, Priority) && Priority == 4);
	TestTrue(TEXT("Unrelated mapping survives"), Subsystem->HasMappingContext(OtherContext, Priority) && Priority == 8);
	Subsystem->RemoveMappingContext(WeaponContext);
	F.Loadout->ApplyInputContext(Subsystem, WeaponContext);
	F.Loadout->ReleasePresentation();
	TestFalse(TEXT("Unowned original context is removed on stow"), Subsystem->HasMappingContext(WeaponContext));
	F.Loadout->ApplyInputContext(Subsystem, WeaponContext);
	Subsystem->AddMappingContext(WeaponContext, 9);
	F.Loadout->ReleaseInputContext();
	TestTrue(TEXT("Later priority writer is not overwritten"), Subsystem->HasMappingContext(WeaponContext, Priority) && Priority == 9);
	F.Loadout->ApplyInputContext(Subsystem, WeaponContext);
	F.Controller->PlayerInput = NewObject<UEnhancedPlayerInput>(F.Controller);
	Subsystem->AddMappingContext(WeaponContext, 6);
	F.Loadout->ApplyInputContext(Subsystem, WeaponContext);
	TestTrue(TEXT("Player input replacement rebinds even on same subsystem"), Subsystem->HasMappingContext(WeaponContext, Priority) && Priority == 1);
	F.Loadout->UnregisterComponent();
	TestTrue(TEXT("Unregister restores replacement input's own priority"), Subsystem->HasMappingContext(WeaponContext, Priority) && Priority == 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWeaponStanceTest,
	"ProjectRPG.Item.WeaponLoadout.NativeStanceActivationAndPendingGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGWeaponStanceTest::RunTest(const FString&)
{
	using namespace RPGWeaponLoadoutTests;
	FFixture F;
	if (!TestNotNull(TEXT("Loadout fixture"), F.Loadout)) return false;
	F.Equipment->EquipmentSnapshots = { F.Snapshot(EEquipmentSlotType::Weapon_Primary_R, F.Primary) };
	F.Equipment->OnRep_EquipmentSnapshots();
	if (!TestEqual(TEXT("Native stance batch ready"), F.Loadout->GrantedHandles.Num(), 2)) return false;
	const auto StowHandle = F.Loadout->GrantedHandles[0];
	auto* Stow = Cast<URPGGameplayAbility_WeaponStance>(F.ASC->FindAbilitySpecFromHandle(StowHandle)->GetPrimaryInstance());
	if (!TestNotNull(TEXT("Stance ability instance"), Stow)) return false;
	Stow->DesiredWeaponState = EEquipState::None;
	TestTrue(TEXT("Server can activate native stow"), F.ASC->TryActivateAbility(StowHandle, false));
	TestEqual(TEXT("Stow changes only native active state"), F.Equipment->GetActiveEquipState(), EEquipState::None);
	TestTrue(TEXT("Self-removing stow activation safely releases active grants"), F.Loadout->GrantedHandles.IsEmpty());
	TestTrue(TEXT("Native equip can activate without legacy Blueprint side effects"), F.ASC->TryActivateAbility(F.EquipHandle, false));
	TestEqual(TEXT("Equip selects primary native set"), F.Equipment->GetActiveEquipState(), EEquipState::WeaponSet_Primary);
	TestEqual(TEXT("Equip restores exactly one native loadout"), F.Loadout->GrantedHandles.Num(), 2);
	F.Equipment->SetActiveEquipState(EEquipState::None);
	F.Equipment->EquipmentSnapshots[0].EquippedActor = nullptr;
	F.Equipment->OnRep_EquipmentSnapshots();
	TestFalse(TEXT("Pending native actor cannot activate startup equip fallback"), F.ASC->TryActivateAbility(F.EquipHandle, false));
	TestEqual(TEXT("Rejected equip leaves state unchanged"), F.Equipment->GetActiveEquipState(), EEquipState::None);
	F.Equipment->EquipmentSnapshots = {
		F.Snapshot(EEquipmentSlotType::Weapon_Primary_L, nullptr),
		F.Snapshot(EEquipmentSlotType::Weapon_Primary_R, F.Primary)
	};
	F.Equipment->OnRep_EquipmentSnapshots();
	TestFalse(TEXT("Pending first hand cannot borrow a ready second hand"), F.ASC->TryActivateAbility(F.EquipHandle, false));
	return true;
}

#endif
