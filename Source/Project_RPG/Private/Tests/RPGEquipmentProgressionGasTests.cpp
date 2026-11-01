#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Tests/RPGCombatControlTestFixture.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Item/Equipment/RPGEquipmentProgressionRules.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Manager/DataManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace RPGEquipmentProgressionTests
{
	FRPGEquipmentProgressionState Growth(int32 Level) { FRPGEquipmentProgressionState G; G.GrowthBandId=TEXT("FirstSlice"); G.RuleVersion=1; G.Level=Level; return G; }
	FPrimaryAssetId Id(const TCHAR* Suffix) { return {TEXT("RPGItemDefinition"), FName(FString(TEXT("GameItem.Equipment.Progression."))+Suffix)}; }
	struct FFixture
	{
		UWorld* World = nullptr;
		ARPGStunTestCharacter* Character = nullptr;
		URPGAbilitySystemComponent* ASC = nullptr;
		URPGAuthoritativeEquipmentComponent* Equipment = nullptr;
		TStrongObjectPtr<UGameInstance> Instance;
		TStrongObjectPtr<UDataManager> Data;
		FRPGItemOwnerRef Owner;
		FFixture() : Instance(NewObject<UGameInstance>()), Data(NewObject<UDataManager>(Instance.Get()))
		{
			Owner.Type=ERPGItemOwnerType::Character; Owner.OwnerId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
			World=UWorld::CreateWorld(EWorldType::Game,false,MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass()));
			if (!World) return;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
			Character=World->SpawnActor<ARPGStunTestCharacter>();
			if (!Character) return;
			ASC=Character->GetRPGAbilitySystemComponent(); ASC->InitAbilityActorInfo(Character,Character);
			Equipment=Character->FindComponentByClass<URPGAuthoritativeEquipmentComponent>();
			ASC->SetNumericAttributeBase(URPGAttributeSet::GetAttackAttribute(),50);
			ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(),200);
			ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),120);
			Data->RefreshItemCache(); // Real installed definitions; no asset saves.
		}
		~FFixture() { if (World) RPGTestWorld::Destroy(World); }
		FRPGItemRecord Record(const TCHAR* Suffix,EEquipmentSlotType Slot,int32 Level=0,FGuid Item=FGuid(),int64 Revision=1,bool bExplicit=true)
		{
			FRPGItemRecord R; FRPGItemInstanceState State;
			const auto* Definition=Data->FindNativeItemDefinition(Id(Suffix));
			if (!Definition) return R;
			if (!FRPGItemInstanceState::TryRestore(Item.IsValid()?Item:FGuid::NewGuid(),0,1,{}, {},State,bExplicit?Growth(Level):FRPGEquipmentProgressionState())) return R;
			FRPGItemLocation Location; Location.ContainerType=ERPGItemContainerType::Equipment; Location.ContainerId=Owner.OwnerId; Location.SlotIndex=static_cast<int32>(Slot);
			FRPGItemRecordMetadata Metadata; Metadata.BindState=ERPGItemBindState::CharacterBound;
			FRPGItemRecord::TryRestore(Definition->GetPrimaryAssetId(),1,Owner,Location,State,Revision,ERPGItemLifecycleState::Active,Metadata,R);
			return R;
		}
		float Attack() const { return ASC->GetNumericAttribute(URPGAttributeSet::GetAttackAttribute()); }
		float Max() const { return ASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute()); }
		float Health() const { return ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentContributionRulesTest,"ProjectRPG.Item.EquipmentProgression.Recipes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentContributionRulesTest::RunTest(const FString&)
{
	using namespace RPGEquipmentProgressionTests;
	struct FCase { const TCHAR* Suffix; EEquipmentSlotType Slot; float BaseAttack,BaseHealth,AttackStep,HealthStep; };
	const FCase Cases[]={{TEXT("Starter.GreatSword"),EEquipmentSlotType::Weapon_Primary_R,0,0,3,0},
		{TEXT("Starter.Chest"),EEquipmentSlotType::Chest,0,0,0,40},{TEXT("Starter.Gloves"),EEquipmentSlotType::Hands,0,0,2,0},
		{TEXT("Target.EnduranceGreatSword"),EEquipmentSlotType::Weapon_Primary_R,10,0,3,0},
		{TEXT("Target.CirculationGreatSword"),EEquipmentSlotType::Weapon_Primary_R,10,0,3,0},
		{TEXT("Target.Chest"),EEquipmentSlotType::Chest,0,100,0,40},{TEXT("Target.Gloves"),EEquipmentSlotType::Hands,4,0,2,0}};
	for (const auto& C : Cases) for(int32 Level=0;Level<=3;++Level)
	{
		FRPGEquipmentProgressionContribution V;
		TestTrue(TEXT("Supported recipe"),RPGEquipmentProgression::TryCalculate(Id(C.Suffix),1,C.Slot,Growth(Level),V));
		TestEqual(TEXT("Attack matches backend"),V.Attack,C.BaseAttack+C.AttackStep*Level);
		TestEqual(TEXT("HP matches backend"),V.MaximumHealth,C.BaseHealth+C.HealthStep*Level);
	}
	FRPGEquipmentProgressionContribution V;
	TestTrue(TEXT("Missing legacy state is zero"),RPGEquipmentProgression::TryCalculate({},999,EEquipmentSlotType::None,{},V));
	TestEqual(TEXT("No inferred starter bonus"),V.Attack+V.MaximumHealth,0.f);
	TestFalse(TEXT("Unknown explicit definition"),RPGEquipmentProgression::TryCalculate(Id(TEXT("Unknown")),1,EEquipmentSlotType::Chest,Growth(0),V));
	TestFalse(TEXT("Wrong slot"),RPGEquipmentProgression::TryCalculate(Id(TEXT("Target.Chest")),1,EEquipmentSlotType::Head,Growth(0),V));
	TestFalse(TEXT("Wrong version"),RPGEquipmentProgression::TryCalculate(Id(TEXT("Target.Chest")),2,EEquipmentSlotType::Chest,Growth(0),V));
	TestFalse(TEXT("Unsupported level"),RPGEquipmentProgression::TryCalculate(Id(TEXT("Target.Chest")),1,EEquipmentSlotType::Chest,Growth(4),V));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentProgressionLifecycleTest,"ProjectRPG.Item.EquipmentProgression.GasLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentProgressionLifecycleTest::RunTest(const FString&)
{
	using namespace RPGEquipmentProgressionTests;
	FFixture F; if (!TestNotNull(TEXT("Equipment"),F.Equipment)) return false;
	auto Sword=F.Record(TEXT("Target.EnduranceGreatSword"),EEquipmentSlotType::Weapon_Primary_R);
	auto Chest=F.Record(TEXT("Target.Chest"),EEquipmentSlotType::Chest);
	auto Gloves=F.Record(TEXT("Target.Gloves"),EEquipmentSlotType::Hands);
	TestTrue(TEXT("Apply +0 targets"),F.Equipment->Reconcile(F.Owner,{Sword,Chest,Gloves},*F.Data));
	TestEqual(TEXT("+14 attack"),F.Attack(),64.f); TestEqual(TEXT("+100 max HP"),F.Max(),300.f); TestEqual(TEXT("No heal"),F.Health(),120.f);
	for(int32 I=0;I<3;++I) TestTrue(TEXT("Repeat restore"),F.Equipment->Reconcile(F.Owner,{Sword,Chest,Gloves},*F.Data));
	TestEqual(TEXT("Single aggregate GE"),F.ASC->GetActiveEffects(FGameplayEffectQuery()).Num(),1);
	Sword=F.Record(TEXT("Target.EnduranceGreatSword"),EEquipmentSlotType::Weapon_Primary_R,3,Sword.GetItemId(),2);
	Chest=F.Record(TEXT("Target.Chest"),EEquipmentSlotType::Chest,3,Chest.GetItemId(),2);
	Gloves=F.Record(TEXT("Target.Gloves"),EEquipmentSlotType::Hands,3,Gloves.GetItemId(),2);
	TestTrue(TEXT("Same identities new growth"),F.Equipment->Reconcile(F.Owner,{Sword,Chest,Gloves},*F.Data));
	TestEqual(TEXT("+29 attack"),F.Attack(),79.f); TestEqual(TEXT("+220 HP"),F.Max(),420.f); TestEqual(TEXT("Upgrade does not refill"),F.Health(),120.f);
	TestEqual(TEXT("Still one GE"),F.ASC->GetActiveEffects(FGameplayEffectQuery()).Num(),1);
	F.Equipment->SetActiveEquipState(EEquipState::WeaponSet_Secondary);
	TestEqual(TEXT("Inactive primary weapon excluded"),F.Attack(),60.f); TestEqual(TEXT("Armor remains"),F.Max(),420.f);
	F.Equipment->SetActiveEquipState(EEquipState::WeaponSet_Primary); TestEqual(TEXT("Primary restored once"),F.Attack(),79.f);
	F.ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),410);
	auto StarterChest=F.Record(TEXT("Starter.Chest"),EEquipmentSlotType::Chest,3);
	TestTrue(TEXT("Swap chest"),F.Equipment->Reconcile(F.Owner,{Sword,StarterChest,Gloves},*F.Data));
	TestEqual(TEXT("Final max HP"),F.Max(),320.f); TestEqual(TEXT("Clamp to final maximum, not temporary base"),F.Health(),320.f);
	TestTrue(TEXT("Unequip chest"),F.Equipment->Reconcile(F.Owner,{Sword,Gloves},*F.Data));
	TestEqual(TEXT("Original maximum"),F.Max(),200.f); TestEqual(TEXT("Clamp on unequip"),F.Health(),200.f);
	F.ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),0);
	TestTrue(TEXT("Restore while dead"),F.Equipment->Reconcile(F.Owner,{Sword,Chest,Gloves},*F.Data));
	TestEqual(TEXT("No resurrection"),F.Health(),0.f);
	TestTrue(TEXT("Remove all"),F.Equipment->Reconcile(F.Owner,{},*F.Data));
	TestEqual(TEXT("Base attack preserved"),F.Attack(),50.f); TestEqual(TEXT("Base maximum preserved"),F.Max(),200.f);
	TestEqual(TEXT("No gear GE remains"),F.ASC->GetActiveEffects(FGameplayEffectQuery()).Num(),0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentProgressionIsolationTest,"ProjectRPG.Item.EquipmentProgression.ScopeAndAuthority",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentProgressionIsolationTest::RunTest(const FString&)
{
	using namespace RPGEquipmentProgressionTests;
	FFixture F; if (!TestNotNull(TEXT("Equipment"),F.Equipment)) return false;
	auto Chest=F.Record(TEXT("Target.Chest"),EEquipmentSlotType::Chest);
	FRPGItemLocation Bag; Bag.ContainerType=ERPGItemContainerType::Inventory; Bag.ContainerId=F.Owner.OwnerId; Bag.SlotIndex=0;
	FRPGItemRecord Stored; TestTrue(TEXT("Move to bag"),Chest.TryMoveTo(Bag,Stored));
	TestTrue(TEXT("Bag record accepted but ignored"),F.Equipment->Reconcile(F.Owner,{Stored},*F.Data)); TestEqual(TEXT("Bag grants nothing"),F.Max(),200.f);
	TestTrue(TEXT("Missing state preserved"),F.Equipment->Reconcile(F.Owner,{F.Record(TEXT("Target.Chest"),EEquipmentSlotType::Chest,0,FGuid(),1,false)},*F.Data));
	TestEqual(TEXT("No automatic +0"),F.Max(),200.f);
	TestTrue(TEXT("Equip"),F.Equipment->Reconcile(F.Owner,{Chest},*F.Data));
	FRPGItemOwnerRef Other=F.Owner; Other.OwnerId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	TestFalse(TEXT("Foreign record after owner switch rejected"),F.Equipment->Reconcile(Other,{Chest},*F.Data));
	TestEqual(TEXT("Old character contribution retired"),F.Max(),200.f);
	F.Character->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client cannot apply authoritative gear"),F.Equipment->Reconcile(F.Owner,{Chest},*F.Data));
	TestEqual(TEXT("Client base unchanged"),F.Max(),200.f);
	F.Character->SetRole(ROLE_Authority);
	return true;
}
#endif
