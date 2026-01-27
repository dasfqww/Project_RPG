// Fill out your copyright notice in the Description page of Project Settings.


#include "RPGFastArray.h"
#include "Item/RPGItemBase.h"
#include "Component/RPGInventoryComponent.h"
#include "Item/PickUp/RPGPickUpBase.h"

TArray<URPGItemBase*> FInventoryFastArray::GetAllItems() const
{
	TArray<URPGItemBase*> Results;
	Results.Reserve(Entries.Num());

	for (const auto& Entry : Entries)
	{
		if (!IsValid(Entry.Item)) continue;

		Results.Add(Entry.Item);
	}

	return Results;
}

void FInventoryFastArray::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	URPGInventoryComponent* InventoryComp = Cast<URPGInventoryComponent>(OwnerComponent);

	if (!IsValid(InventoryComp)) return;

	for (int32 Index : AddedIndices)
	{
		InventoryComp->OnItemAdded.Broadcast(Entries[Index].Item);
	}
}

void FInventoryFastArray::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
	URPGInventoryComponent* InventoryComp = Cast<URPGInventoryComponent>(OwnerComponent);

	if (!IsValid(InventoryComp)) return;
	
	for (int32 Index : RemovedIndices)
	{
		InventoryComp->OnItemRemoved.Broadcast(Entries[Index].Item);
	}
}

URPGItemBase* FInventoryFastArray::AddEntry(ARPGPickUpBase* ItemPickup)
{
	check(OwnerComponent);

	AActor* OwningActor = OwnerComponent->GetOwner();
	check(OwningActor->HasAuthority());

	URPGInventoryComponent* InvenComp = Cast<URPGInventoryComponent>(OwnerComponent);
	if (!IsValid(InvenComp)) return nullptr;

	FInventoryEntry& NewEntry = Entries.AddDefaulted_GetRef();
	NewEntry.Item = ItemPickup->GetItemManifest().Manifest(OwningActor);

	InvenComp->AddRepSubObj(NewEntry.Item);
	MarkItemDirty(NewEntry);

	return NewEntry.Item;
}

URPGItemBase* FInventoryFastArray::AddEntry(URPGItemBase* Item)
{
	check(OwnerComponent);
	AActor* OwningActor = OwnerComponent->GetOwner();
	check(OwningActor->HasAuthority());

	URPGInventoryComponent* InvenComp = Cast<URPGInventoryComponent>(OwnerComponent);
	if (!IsValid(InvenComp)) return nullptr;

	FInventoryEntry& NewEntry = Entries.AddDefaulted_GetRef();
	NewEntry.Item = Item;

	InvenComp->AddRepSubObj(NewEntry.Item);
	MarkItemDirty(NewEntry);

	return NewEntry.Item;
}

void FInventoryFastArray::RemoveEntry(URPGItemBase* Item)
{
	for (auto EntryIt=Entries.CreateIterator(); EntryIt; EntryIt++)
	{
		FInventoryEntry& Entry = *EntryIt;
		if (Entry.Item==Item)
		{
			EntryIt.RemoveCurrent();
			MarkArrayDirty();
		}
	}
}

URPGItemBase* FInventoryFastArray::FindFirstItemType(const FGameplayTag& ItemTag)
{
	auto* FoundItem = Entries.FindByPredicate([ItemTag= ItemTag](const FInventoryEntry& Entry)
	{
		return IsValid(Entry.Item) && Entry.Item->GetItemManifest().GetItemTag().MatchesTagExact(ItemTag);
	});

	return FoundItem ? FoundItem->Item : nullptr;
}
