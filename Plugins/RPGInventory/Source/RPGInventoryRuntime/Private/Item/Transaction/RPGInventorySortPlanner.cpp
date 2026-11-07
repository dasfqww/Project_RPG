#include "Item/Transaction/RPGInventorySortPlanner.h"

#include "Algo/StableSort.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"
#include "Misc/SecureHash.h"

FRPGItemCommandPlan FRPGInventorySortPlanner::Prepare(const IRPGItemRecordSource& Records,
	const FGuid& RequestId, const FRPGItemOwnerRef& Owner, const EItemCategory Category)
{
	FRPGItemCommandPlan Plan;
	if (!RequestId.IsValid() || !Owner.IsValid() || Owner.Type != ERPGItemOwnerType::Character ||
		!RPGItemInventoryPolicy::IsTab(Category)) return Plan;
	TArray<FRPGItemRecord> All, Items;
	Records.FindByOwner(Owner, All);
	TSet<int32> Fixed, Seen;
	for (const auto& Item : All)
	{
		if (!Item.IsActive() || Item.GetLocation().ContainerType != ERPGItemContainerType::Inventory ||
			!RPGItemInventoryPolicy::Contains(Category, Item.GetLocation().SlotIndex)) continue;
		if (Item.GetOwner() != Owner || Item.GetLocation().ContainerId != Owner.OwnerId ||
			!RPGItemInventoryPolicy::CanStore(Item.GetDefinitionId(), Item.GetLocation().SlotIndex) ||
			Seen.Contains(Item.GetLocation().SlotIndex)) return Plan;
		Seen.Add(Item.GetLocation().SlotIndex);
		if (Item.GetMetadata().bLocked) Fixed.Add(Item.GetLocation().SlotIndex);
		else
		{
			if (Item.GetRevision() <= 0 || Item.GetRevision() == MAX_int64) return Plan;
			Items.Add(Item);
		}
	}
	// The source collection order is not a persistence contract. Establish the old slot order first.
	Items.Sort([](const auto& A, const auto& B) { return A.GetLocation().SlotIndex < B.GetLocation().SlotIndex; });
	Algo::StableSort(Items, [](const auto& A, const auto& B)
	{
		const int32 Name = A.GetDefinitionId().ToString().Compare(B.GetDefinitionId().ToString(), ESearchCase::CaseSensitive);
		if (Name != 0) return Name < 0;
		if (A.GetDefinitionVersion() != B.GetDefinitionVersion()) return A.GetDefinitionVersion() < B.GetDefinitionVersion();
		if (A.GetState().GetEquipmentProgression().Level != B.GetState().GetEquipmentProgression().Level)
			return A.GetState().GetEquipmentProgression().Level > B.GetState().GetEquipmentProgression().Level;
		return A.GetQuantity() > B.GetQuantity();
	});
	auto& Commit = Plan.CommitRequest;
	Commit.RequestId = RequestId;
	Commit.Actor = Owner;
	Commit.Operation = TEXT("Item.Sort");
	FString Fingerprint = FString::Printf(TEXT("sort-v1|%d"), static_cast<int32>(Category));
	int32 Slot = RPGItemInventoryPolicy::FirstSlot(Category);
	bool bChanged = false;
	for (const auto& Item : Items)
	{
		while (Fixed.Contains(Slot)) ++Slot;
		if (!RPGItemInventoryPolicy::Contains(Category, Slot)) return FRPGItemCommandPlan();
		FRPGItemLocation Location = Item.GetLocation();
		Location.SlotIndex = Slot++;
		bChanged |= Location != Item.GetLocation();
		FRPGItemRecord Moved;
		if (!Item.TryMoveTo(Location, Moved)) return FRPGItemCommandPlan();
		Commit.Mutations.Add({Item.GetRevision(), MoveTemp(Moved)});
		Fingerprint += FString::Printf(TEXT("|%s|%lld|%d"), *Item.GetItemId().ToString(), Item.GetRevision(), Location.SlotIndex);
	}
	if (!bChanged)
	{
		Plan = {};
		Plan.FailureStatus = ERPGItemTransactionStatus::NoChange;
		return Plan;
	}
	const FTCHARToUTF8 Bytes(*Fingerprint);
	uint8 Digest[20];
	FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Digest);
	Commit.CommandFingerprint = BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
	Plan.bReady = true;
	return Plan;
}
