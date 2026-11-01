#if WITH_DEV_AUTOMATION_TESTS

#include "Ability/Player/RPGGameplayAbility_Equipment.h"
#include "AnimNotifyState/RPGAnimNotifyState_PerformTrace.h"
#include "Component/Combat/PawnCombatComponent.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Component/Equipment/RPGEquipmentComponent.h"
#include "Component/Equipment/RPGEquipComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Item/Definition/RPGItemDefinitionFragments.h"
#include "Item/Policy/RPGItemActionPolicy.h"
#include "Item/RPGItemBase.h"
#include "Item/Weapon/RPGWeaponBase.h"
#include "Manager/DataManager.h"
#include "Misc/AutomationTest.h"
#include "Net/UnrealNetwork.h"
#include "RPGItemTags.h"
#include "UObject/UnrealType.h"

namespace RPGEquipmentSnapshotTests
{
FRPGItemOwnerRef MakeOwner()
{
	FRPGItemOwnerRef Owner;
	Owner.Type = ERPGItemOwnerType::Character;
	Owner.OwnerId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001");
	return Owner;
}

URPGItemDefinition* MakeDefinition(const EEquipmentSlotType Slot)
{
	URPGItemDefinition* Definition = NewObject<URPGItemDefinition>(GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), URPGItemDefinition::StaticClass(), TEXT("EquipmentRead")));
	Definition->ItemCategory = EItemCategory::Equip;
	URPGItemEquipmentDefinitionFragment* Fragment = NewObject<URPGItemEquipmentDefinitionFragment>(Definition);
	Fragment->CompatibleSlots.Add(Slot);
	if (FRPGEquipmentSnapshot::IsWeaponSlot(Slot))
	{
		Fragment->WeaponType = ERPGGladiatorWeaponType::Shield;
		Fragment->WeaponHandType = EWeaponHandType::LeftHand;
	}
	Definition->Fragments.Add(Fragment);
	return Definition;
}

FRPGItemRecord MakeRecord(const URPGItemDefinition& Definition, const EEquipmentSlotType Slot,
	const FGuid Id = FGuid::NewGuid(), const int64 Revision = 9007199254740993LL,
	const float StatValue = 6.5f)
{
	FRPGItemInstanceState State;
	FRPGItemStatValue Stat;
	Stat.StatTag = RPGGameplayTags::Fragment_StatMod_1;
	Stat.Value = StatValue;
	FRPGItemInstanceState::TryRestore(Id, 19, 1, {}, { Stat }, State);
	FRPGItemLocation Location;
	Location.ContainerType = ERPGItemContainerType::Equipment;
	Location.ContainerId = MakeOwner().OwnerId;
	Location.SlotIndex = static_cast<int32>(Slot);
	FRPGItemRecord Record;
	FRPGItemRecord::TryRestore(Definition.GetPrimaryAssetId(), Definition.GetDefinitionVersion(),
		MakeOwner(), Location, State, Revision, ERPGItemLifecycleState::Active, {}, Record);
	return Record;
}

struct FFixture
{
	UWorld* World = nullptr;
	ACharacter* Avatar = nullptr;
	URPGAuthoritativeEquipmentComponent* Equipment = nullptr;
	URPGAbilitySystemComponent* ASC = nullptr;
	ARPGWeaponBase* NativeWeapon = nullptr;
	ARPGWeaponBase* StartupWeapon = nullptr;

	FFixture()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Avatar = World->SpawnActor<ACharacter>();
		if (!Avatar) return;
		Equipment = NewObject<URPGAuthoritativeEquipmentComponent>(Avatar);
		Avatar->AddInstanceComponent(Equipment);
		Equipment->RegisterComponent();
		ASC = NewObject<URPGAbilitySystemComponent>(Avatar);
		Avatar->AddInstanceComponent(ASC);
		ASC->RegisterComponent();
		ASC->InitAbilityActorInfo(Avatar, Avatar);
		NativeWeapon = World->SpawnActor<ARPGWeaponBase>();
		StartupWeapon = World->SpawnActor<ARPGWeaponBase>();
		UPawnCombatComponent* Combat = NewObject<UPawnCombatComponent>(Avatar);
		Avatar->AddInstanceComponent(Combat);
		Combat->RegisterComponent();
		Combat->RegisterSpawnedWeapon(RPGGameplayTags::Fragment_StatMod_1, StartupWeapon, true);
	}
	~FFixture()
	{
		if (ASC)
		{
			ASC->CancelAllAbilities();
			ASC->ClearAllAbilities();
		}
		if (World)
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentSnapshotBuildTest,
	"ProjectRPG.Item.EquipmentSnapshot.BuildUsesPersistentStatsAndNativeMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentSnapshotBuildTest::RunTest(const FString&)
{
	using namespace RPGEquipmentSnapshotTests;
	const EEquipmentSlotType Slot = EEquipmentSlotType::Weapon_Primary_L;
	URPGItemDefinition* Definition = MakeDefinition(Slot);
	const FRPGItemRecord Record = MakeRecord(*Definition, Slot);
	FRPGEquipmentSnapshot Snapshot;
	TestTrue(TEXT("Build native equipment view"), FRPGEquipmentSnapshot::TryBuild(Record, *Definition, Snapshot));
	TestEqual(TEXT("Stable ID retained"), Snapshot.ItemId, Record.GetItemId());
	TestEqual(TEXT("Full int64 revision retained"), Snapshot.Revision, Record.GetRevision());
	TestEqual(TEXT("Persisted stat, not a newly rolled value"),
		Snapshot.GetStatValue(RPGGameplayTags::Fragment_StatMod_1), 6.5f);
	TestEqual(TEXT("Native weapon identity"), Snapshot.WeaponType, ERPGGladiatorWeaponType::Shield);
	Definition->DefinitionVersion = 2;
	TestFalse(TEXT("Stale definition version rejected"), FRPGEquipmentSnapshot::TryBuild(Record, *Definition, Snapshot));
	TestFalse(TEXT("Rejected build clears output"), Snapshot.ItemId.IsValid());
	Definition->DefinitionVersion = 1;
	const auto* Fragment = Definition->FindFragmentByClass<URPGItemEquipmentDefinitionFragment>();
	auto* MutableFragment = const_cast<URPGItemEquipmentDefinitionFragment*>(Fragment);
	MutableFragment->UtilityType = ERPGGladiatorUtilityType::Drink;
	FRPGItemActionPolicyRegistry Policies;
	TestFalse(TEXT("Wrong-category metadata rejected before command registration"),
		Policies.RegisterDefinition(*Definition));
	TestFalse(TEXT("Wrong-category metadata cannot become a read model"),
		FRPGEquipmentSnapshot::TryBuild(Record, *Definition, Snapshot));
	MutableFragment->UtilityType = ERPGGladiatorUtilityType::Count;
	TestTrue(TEXT("Valid native identity can register"), Policies.RegisterDefinition(*Definition));
	FRPGItemLocation Bag;
	Bag.ContainerType = ERPGItemContainerType::Inventory;
	Bag.ContainerId = MakeOwner().OwnerId;
	Bag.SlotIndex = 0;
	FRPGItemRecord Unequipped;
	TestTrue(TEXT("Prepare inventory record"), Record.TryMoveTo(Bag, Unequipped));
	TestFalse(TEXT("Inventory item is not equipment evidence"),
		FRPGEquipmentSnapshot::TryBuild(Unequipped, *Definition, Snapshot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentSnapshotRuntimeTest,
	"ProjectRPG.Item.EquipmentSnapshot.ReconcilePublishesAndRetiresOwnerOnlyViews",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentSnapshotRuntimeTest::RunTest(const FString&)
{
	using namespace RPGEquipmentSnapshotTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Equipment fixture"), Fixture.Equipment)) return false;
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UDataManager* Data = NewObject<UDataManager>(GameInstance);
	Data->RefreshItemCache(); // Read actual authored native definition; no asset saves.
	const URPGItemDefinition* Definition = Data->FindNativeItemDefinition(FPrimaryAssetId(
		TEXT("RPGItemDefinition"), TEXT("GameItem.Equipment.Helm.Default")));
	if (!TestNotNull(TEXT("Authored native helm definition"), Definition)) return false;
	const FRPGItemRecord Record = MakeRecord(*Definition, EEquipmentSlotType::Head);
	TestTrue(TEXT("Apply authoritative equipment"), Fixture.Equipment->Reconcile(MakeOwner(), { Record }, *Data));
	FRPGEquipmentSnapshot Snapshot;
	TestTrue(TEXT("Read native equipped helm"), Fixture.Equipment->FindSnapshotInSlot(EEquipmentSlotType::Head, Snapshot));
	TestEqual(TEXT("Read persisted stats"), Snapshot.GetStatValue(RPGGameplayTags::Fragment_StatMod_1), 6.5f);
	const FRPGItemRecord Newer = MakeRecord(*Definition, EEquipmentSlotType::Head,
		Record.GetItemId(), Record.GetRevision() + 1, 8.5f);
	TestTrue(TEXT("Reconcile latest record"), Fixture.Equipment->Reconcile(MakeOwner(), { Newer }, *Data));
	Fixture.Equipment->FindSnapshotInSlot(EEquipmentSlotType::Head, Snapshot);
	TestEqual(TEXT("Same item read model updates revision"), Snapshot.Revision, Newer.GetRevision());
	TestEqual(TEXT("Same item read model updates persistent stats"),
		Snapshot.GetStatValue(RPGGameplayTags::Fragment_StatMod_1), 8.5f);
	TestEqual(TEXT("Same item does not duplicate applied resources"), Fixture.Equipment->NumAppliedItems(), 1);
	TArray<FLifetimeProperty> Props;
	URPGAuthoritativeEquipmentComponent::StaticClass()->SetUpRuntimeReplicationData();
	Fixture.Equipment->GetLifetimeReplicatedProps(Props);
	for (const FName Name : { FName(TEXT("EquipmentSnapshots")), FName(TEXT("ActiveEquipState")) })
	{
		const FProperty* Property = FindFProperty<FProperty>(URPGAuthoritativeEquipmentComponent::StaticClass(), Name);
		const FLifetimeProperty* Lifetime = Property ? Props.FindByPredicate([Property](const FLifetimeProperty& P)
			{ return P.RepIndex == Property->RepIndex; }) : nullptr;
		TestTrue(TEXT("Equipment view replication is owner-only"), Lifetime && Lifetime->Condition == COND_OwnerOnly);
	}
	FRPGItemOwnerRef Other = MakeOwner();
	Other.OwnerId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f002");
	TestFalse(TEXT("Foreign record rejected on character switch"), Fixture.Equipment->Reconcile(Other, { Newer }, *Data));
	TestFalse(TEXT("Previous character view is retired"), Fixture.Equipment->FindSnapshotInSlot(EEquipmentSlotType::Head, Snapshot));
	TestEqual(TEXT("Previous character resources retired"), Fixture.Equipment->NumAppliedItems(), 0);
	TestTrue(TEXT("Empty current character equipment is accepted"), Fixture.Equipment->Reconcile(Other, {}, *Data));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentAbilitySnapshotTest,
	"ProjectRPG.Item.EquipmentSnapshot.AbilityUsesNativeIdentityAndActivationStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentAbilitySnapshotTest::RunTest(const FString&)
{
	using namespace RPGEquipmentSnapshotTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Equipment fixture"), Fixture.Equipment)) return false;
	URPGItemDefinition* Definition = MakeDefinition(EEquipmentSlotType::Weapon_Primary_L);
	FRPGEquipmentSnapshot Native;
	FRPGEquipmentSnapshot::TryBuild(MakeRecord(*Definition, EEquipmentSlotType::Weapon_Primary_L), *Definition, Native);
	Native.EquippedActor = Fixture.NativeWeapon;
	Fixture.Equipment->EquipmentSnapshots = { Native }; // Simulate the replicated validated view.
	const FGameplayAbilitySpecHandle Handle = Fixture.ASC->GiveAbility(FGameplayAbilitySpec(
		URPGGameplayAbility_Equipment::StaticClass(), 1));
	FGameplayAbilitySpec* Spec = Fixture.ASC->FindAbilitySpecFromHandle(Handle);
	auto* Ability = Spec ? Cast<URPGGameplayAbility_Equipment>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("Instanced equipment ability"), Ability)) return false;
	Ability->NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	FRPGGladiatorEquipmentInfo Requirement;
	Requirement.EquipmentType = ERPGGladiatorEquipmentType::Weapon;
	Requirement.WeaponHandType = EWeaponHandType::LeftHand;
	Requirement.RequiredWeaponType = ERPGGladiatorWeaponType::Shield;
	Ability->EquipmentInfos = { Requirement };
	TestTrue(TEXT("GAS activates with native equipment"), Fixture.ASC->TryActivateAbility(Handle));
	TestEqual(TEXT("Ability resolves native actor"), Ability->GetFirstEquipmentActor(), static_cast<AActor*>(Fixture.NativeWeapon));
	TestEqual(TEXT("Ability resolves native ID"), Ability->GetEquipmentItemId(Fixture.NativeWeapon), Native.ItemId);
	TestNull(TEXT("No mutable legacy item mirror"), Ability->GetEquipmentItemInstance(Fixture.NativeWeapon));
	TestEqual(TEXT("Ability reads persistent float stats"),
		Ability->GetEquipmentStatValueByItemId(RPGGameplayTags::Fragment_StatMod_1, Native.ItemId), 6.5f);
	TestEqual(TEXT("Old actor-based stat signature forwards to snapshot"),
		Ability->GetEquipmentStatValue(RPGGameplayTags::Fragment_StatMod_1, Fixture.NativeWeapon), 7);
	Fixture.Equipment->EquipmentSnapshots[0].StatValues[0].Value = 99.f;
	TestEqual(TEXT("Active invocation retains its captured stats"),
		Ability->GetEquipmentStatValueByItemId(RPGGameplayTags::Fragment_StatMod_1, Native.ItemId), 6.5f);
	Fixture.ASC->CancelAbilityHandle(Handle);
	FRPGEquipmentSnapshot Resolved;
	bool bFallback = false;
	Fixture.Equipment->EquipmentSnapshots[0].EquippedActor = nullptr;
	TestTrue(TEXT("Pending native actor cannot borrow a startup actor"),
		Ability->ResolveEquipmentRequirement(Fixture.Avatar, Requirement, Resolved, bFallback)
		== URPGGameplayAbility_Equipment::ERequirementResult::Missing);
	Fixture.Equipment->EquipmentSnapshots[0] = Native;
	Requirement.RequiredWeaponType = ERPGGladiatorWeaponType::Bow;
	TestTrue(TEXT("Known native weapon mismatch is enforced"),
		Ability->ResolveEquipmentRequirement(Fixture.Avatar, Requirement, Resolved, bFallback)
		== URPGGameplayAbility_Equipment::ERequirementResult::WrongType);
	Fixture.Equipment->EquipmentSnapshots.Reset();
	Requirement.RequiredWeaponType = ERPGGladiatorWeaponType::Shield;
	TestTrue(TEXT("Unmigrated startup actor compatibility remains explicit"),
		Ability->ResolveEquipmentRequirement(Fixture.Avatar, Requirement, Resolved, bFallback)
		== URPGGameplayAbility_Equipment::ERequirementResult::Satisfied && bFallback && !Resolved.ItemId.IsValid());
	URPGEquipmentComponent* Legacy = NewObject<URPGEquipmentComponent>(Fixture.Avatar);
	Fixture.Avatar->AddInstanceComponent(Legacy);
	Legacy->RegisterComponent();
	URPGItemBase* OldItem = NewObject<URPGItemBase>(Fixture.Avatar);
	Legacy->AddEquipment_Internal(EEquipmentSlotType::Head, OldItem, 1);
	Requirement = {};
	Requirement.EquipmentType = ERPGGladiatorEquipmentType::Armor;
	Requirement.RequiredArmorType = ERPGGladiatorArmorType::Helmet;
	TestTrue(TEXT("A raw legacy item alone cannot satisfy an armor skill"),
		Ability->ResolveEquipmentRequirement(Fixture.Avatar, Requirement, Resolved, bFallback)
		== URPGGameplayAbility_Equipment::ERequirementResult::Missing);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentTraceSnapshotTest,
	"ProjectRPG.Item.EquipmentSnapshot.TraceUsesActiveNativeWeaponWithoutBypass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentTraceSnapshotTest::RunTest(const FString&)
{
	using namespace RPGEquipmentSnapshotTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Equipment fixture"), Fixture.Equipment)) return false;
	URPGItemDefinition* Definition = MakeDefinition(EEquipmentSlotType::Weapon_Primary_L);
	FRPGEquipmentSnapshot Native;
	FRPGEquipmentSnapshot::TryBuild(MakeRecord(*Definition, EEquipmentSlotType::Weapon_Primary_L), *Definition, Native);
	URPGAnimNotifyState_PerformTrace* Notify = NewObject<URPGAnimNotifyState_PerformTrace>();
	Notify->WeaponHandType = EWeaponHandType::LeftHand;
	Fixture.Equipment->EquipmentSnapshots = { Native };
	TestNull(TEXT("Trace cannot fall back while native actor is pending"), Notify->ResolveWeapon(Fixture.Avatar->GetMesh()));
	Fixture.Equipment->EquipmentSnapshots[0].EquippedActor = Fixture.NativeWeapon;
	TestEqual(TEXT("Trace resolves native actor"), Notify->ResolveWeapon(Fixture.Avatar->GetMesh()), Fixture.NativeWeapon);
	Notify->WeaponHandType = EWeaponHandType::RightHand;
	TestNull(TEXT("Wrong hand cannot use a startup weapon"), Notify->ResolveWeapon(Fixture.Avatar->GetMesh()));
	Notify->WeaponHandType = EWeaponHandType::TwoHand;
	TestNull(TEXT("Two-handed trace cannot select a known left-hand item"), Notify->ResolveWeapon(Fixture.Avatar->GetMesh()));
	Notify->WeaponHandType = EWeaponHandType::LeftHand;
	Fixture.Equipment->SetActiveEquipState(EEquipState::WeaponSet_Secondary);
	TestNull(TEXT("Inactive native weapon is not trace evidence"), Notify->ResolveWeapon(Fixture.Avatar->GetMesh()));
	Fixture.Equipment->SetActiveEquipState(EEquipState::WeaponSet_Primary);
	TestEqual(TEXT("Switching back resolves active actor"), Notify->ResolveWeapon(Fixture.Avatar->GetMesh()), Fixture.NativeWeapon);
	Fixture.Equipment->SetActiveEquipState(static_cast<EEquipState>(255));
	TestEqual(TEXT("Invalid weapon-set state is rejected"), Fixture.Equipment->GetActiveEquipState(), EEquipState::WeaponSet_Primary);
	URPGEquipComponent* LegacyState = NewObject<URPGEquipComponent>(Fixture.Avatar);
	Fixture.Avatar->AddInstanceComponent(LegacyState);
	LegacyState->RegisterComponent();
	// The compatibility wrapper must forward stow to the native state, not a local mirror.
	LegacyState->ChangeEquipState(EEquipState::None);
	TestEqual(TEXT("Legacy state wrapper forwards to the native state"),
		Fixture.Equipment->GetActiveEquipState(), EEquipState::None);
	TestNull(TEXT("Stowed native weapon cannot bypass through startup fallback"), Notify->ResolveWeapon(Fixture.Avatar->GetMesh()));
	Fixture.Equipment->SetActiveEquipState(EEquipState::Utility);
	TestNull(TEXT("Utility state cannot use inactive native weapon"), Notify->ResolveWeapon(Fixture.Avatar->GetMesh()));
	Fixture.Equipment->SetActiveEquipState(EEquipState::WeaponSet_Primary);
	Fixture.Equipment->EquipmentSnapshots.Reset();
	TestEqual(TEXT("Non-item startup actor fallback is preserved"), Notify->ResolveWeapon(Fixture.Avatar->GetMesh()), Fixture.StartupWeapon);
	return true;
}

#endif
