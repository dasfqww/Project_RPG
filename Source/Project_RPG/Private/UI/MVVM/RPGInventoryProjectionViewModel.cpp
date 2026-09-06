#include "UI/MVVM/RPGInventoryProjectionViewModel.h"

#include "Component/RPGInventoryProjectionComponent.h"
#include "GameFramework/PlayerController.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGInventoryProjectionViewModel)

void URPGInventoryProjectionViewModel::BeginDestroy()
{
	UnbindComponents();
	Super::BeginDestroy();
}

void URPGInventoryProjectionViewModel::InitializeFromPlayerController(
	APlayerController* PlayerController)
{
	UnbindComponents();

	LastCommandResult = FRPGItemCommandClientResult();
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(LastCommandResult);
	if (bHasLastCommandResult)
	{
		bHasLastCommandResult = false;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(bHasLastCommandResult);
	}

	URPGInventoryProjectionComponent* Projection = PlayerController
		? PlayerController->FindComponentByClass<
			URPGInventoryProjectionComponent>()
		: nullptr;
	URPGItemCommandComponent* Commands = PlayerController
		? PlayerController->FindComponentByClass<URPGItemCommandComponent>()
		: nullptr;
	ProjectionComponent = Projection;
	CommandComponent = Commands;

	if (Projection)
	{
		Projection->OnProjectionChanged.AddUniqueDynamic(
			this,
			&ThisClass::HandleProjectionChanged);
		Projection->OnLoadStateChanged.AddUniqueDynamic(
			this,
			&ThisClass::HandleLoadStateChanged);
	}
	if (Commands)
	{
		Commands->OnItemCommandCompleted.AddUniqueDynamic(
			this,
			&ThisClass::HandleCommandCompleted);
	}

	HandleLoadStateChanged(
		Projection
			? Projection->GetLoadState()
			: ERPGInventoryProjectionLoadState::Uninitialized);
}

bool URPGInventoryProjectionViewModel::RequestConsume(
	const FGuid ItemId,
	FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	FRPGInventoryProjectionEntry Entry;
	URPGItemCommandComponent* Commands = CommandComponent.Get();
	if (!Commands ||
		!TryGetEntry(
			ItemId,
			ERPGItemContainerType::Inventory,
			Entry))
	{
		return false;
	}

	OutRequestId = FGuid::NewGuid();
	Commands->ServerConsumeItem(
		OutRequestId,
		ItemId,
		Entry.GetRevision());
	return true;
}

bool URPGInventoryProjectionViewModel::RequestEquip(
	const FGuid ItemId,
	const EEquipmentSlotType SlotType,
	FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	if (SlotType == EEquipmentSlotType::None ||
		SlotType == EEquipmentSlotType::Count)
	{
		return false;
	}

	FRPGInventoryProjectionEntry Entry;
	URPGItemCommandComponent* Commands = CommandComponent.Get();
	if (!Commands ||
		!TryGetEntry(
			ItemId,
			ERPGItemContainerType::Inventory,
			Entry))
	{
		return false;
	}

	OutRequestId = FGuid::NewGuid();
	Commands->ServerEquipItem(
		OutRequestId,
		ItemId,
		Entry.GetRevision(),
		SlotType);
	return true;
}

bool URPGInventoryProjectionViewModel::RequestUnequip(
	const FGuid ItemId,
	const int32 InventorySlotIndex,
	FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	if (InventorySlotIndex < 0)
	{
		return false;
	}

	FRPGInventoryProjectionEntry Entry;
	URPGItemCommandComponent* Commands = CommandComponent.Get();
	if (!Commands ||
		!TryGetEntry(
			ItemId,
			ERPGItemContainerType::Equipment,
			Entry))
	{
		return false;
	}

	OutRequestId = FGuid::NewGuid();
	Commands->ServerUnequipItem(
		OutRequestId,
		ItemId,
		Entry.GetRevision(),
		InventorySlotIndex);
	return true;
}

void URPGInventoryProjectionViewModel::HandleProjectionChanged()
{
	RefreshProjection();
}

void URPGInventoryProjectionViewModel::HandleLoadStateChanged(
	const ERPGInventoryProjectionLoadState NewLoadState)
{
	if (LoadState != NewLoadState)
	{
		LoadState = NewLoadState;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(LoadState);
	}
	RefreshProjection();
}

void URPGInventoryProjectionViewModel::HandleCommandCompleted(
	const FRPGItemCommandClientResult Result)
{
	LastCommandResult = Result;
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(LastCommandResult);
	if (!bHasLastCommandResult)
	{
		bHasLastCommandResult = true;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(bHasLastCommandResult);
	}
	OnCommandCompleted.Broadcast(Result);
}

bool URPGInventoryProjectionViewModel::TryGetEntry(
	const FGuid& ItemId,
	const ERPGItemContainerType ExpectedContainer,
	FRPGInventoryProjectionEntry& OutEntry) const
{
	const URPGInventoryProjectionComponent* Projection =
		ProjectionComponent.Get();
	return Projection &&
		Projection->FindProjectedItem(ItemId, OutEntry) &&
		OutEntry.GetContainerType() == ExpectedContainer;
}

void URPGInventoryProjectionViewModel::RefreshProjection()
{
	TArray<FRPGInventoryProjectionEntry> NewInventoryItems;
	TArray<FRPGInventoryProjectionEntry> NewEquipmentItems;
	if (const URPGInventoryProjectionComponent* Projection =
		ProjectionComponent.Get())
	{
		for (const FRPGInventoryProjectionEntry& Entry :
			Projection->GetProjectedItems())
		{
			switch (Entry.GetContainerType())
			{
			case ERPGItemContainerType::Inventory:
				NewInventoryItems.Add(Entry);
				break;
			case ERPGItemContainerType::Equipment:
				NewEquipmentItems.Add(Entry);
				break;
			default:
				break;
			}
		}
	}

	InventoryItems = MoveTemp(NewInventoryItems);
	EquipmentItems = MoveTemp(NewEquipmentItems);
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(InventoryItems);
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(EquipmentItems);
}

void URPGInventoryProjectionViewModel::UnbindComponents()
{
	if (URPGInventoryProjectionComponent* Projection =
		ProjectionComponent.Get())
	{
		Projection->OnProjectionChanged.RemoveDynamic(
			this,
			&ThisClass::HandleProjectionChanged);
		Projection->OnLoadStateChanged.RemoveDynamic(
			this,
			&ThisClass::HandleLoadStateChanged);
	}
	if (URPGItemCommandComponent* Commands = CommandComponent.Get())
	{
		Commands->OnItemCommandCompleted.RemoveDynamic(
			this,
			&ThisClass::HandleCommandCompleted);
	}
	ProjectionComponent.Reset();
	CommandComponent.Reset();
}
