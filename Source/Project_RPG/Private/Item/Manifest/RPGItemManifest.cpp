// Fill out your copyright notice in the Description page of Project Settings.


#include "Item/Manifest/RPGItemManifest.h"
#include "Item/RPGItemBase.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Item/Fragment/RPGItemFragment.h"
#include "UI/Composite/RPGCompositeBase.h"
#include "Manager/ObjectManager.h"

URPGItemBase* FItemManifest::Manifest(UObject* NewOuter)
{
	URPGItemBase* Item = NewObject<URPGItemBase>(NewOuter, URPGItemBase::StaticClass());
	Item->SetItemManifest(*this);
	
	for (auto& Fragment : Item->GetItemManifestMutable().GetFragmentsMutable())
	{
		Fragment.GetMutable().Manifest();
	}
	//ClearFragments();

	return Item;
}

void FItemManifest::AssimilateInventoryFragments(URPGCompositeBase* Composite) const
{
	const auto& InventoryItemFragments = GetAllFragmentsOfType<FInventoryItemFragment>();
	for (const auto* Fragment : InventoryItemFragments)
	{
		Composite->ApplyFunction([Fragment](URPGCompositeBase* Widget)
		{
				Fragment->Assimilate(Widget);
		});

	}
}

void FItemManifest::SpawnPickupActor(const UObject* WorldContextObject,
	const FVector& SpawnLocation, const FRotator& SpawnRotation)
{
	if (!IsValid(PickupActorClass) || !IsValid(WorldContextObject)) return;

	// ItemTag의 태그 이름을 Pool 이름으로 사용 (예: "GameItems.Sword" -> "Sword")
	FName PoolName = TagName;
	
	/*UObjectManager* ObjectManager = UObjectManager::Get<UObjectManager>(const_cast<UObject*>(WorldContextObject));
	if (!IsValid(ObjectManager)) return;*/

	AActor* SpawnedActor = UObjectManager::Get<UObjectManager>(const_cast<UObject*>(WorldContextObject))
		->SpawnObject(TagName, PickupActorClass, SpawnLocation, SpawnRotation, true);

	if (!IsValid(SpawnedActor)) return;

	ARPGPickUpBase* ItemPickup = Cast<ARPGPickUpBase>(SpawnedActor);
	if (!IsValid(ItemPickup)) return;

	ItemPickup->InitItemManifest(*this);
}

void FItemManifest::ClearFragments()
{
	for (auto& Fragment : Fragments)
	{
		Fragment.Reset();
	}
	Fragments.Empty();
}
