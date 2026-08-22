#include "Item/Persistence/RPGItemRecord.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGItemRecord)

bool FRPGItemRecord::TryCreate(
	const FPrimaryAssetId& InDefinitionId,
	const int32 InDefinitionVersion,
	const FRPGItemOwnerRef& InOwner,
	const FRPGItemLocation& InLocation,
	const FRPGItemInstanceState& InState,
	const FRPGItemRecordMetadata& InMetadata,
	FRPGItemRecord& OutRecord)
{
	return TryRestore(
		InDefinitionId,
		InDefinitionVersion,
		InOwner,
		InLocation,
		InState,
		0,
		ERPGItemLifecycleState::Active,
		InMetadata,
		OutRecord);
}

bool FRPGItemRecord::TryRestore(
	const FPrimaryAssetId& InDefinitionId,
	const int32 InDefinitionVersion,
	const FRPGItemOwnerRef& InOwner,
	const FRPGItemLocation& InLocation,
	const FRPGItemInstanceState& InState,
	const int64 InRevision,
	const ERPGItemLifecycleState InLifecycleState,
	const FRPGItemRecordMetadata& InMetadata,
	FRPGItemRecord& OutRecord)
{
	FRPGItemRecord Candidate;
	Candidate.DefinitionId = InDefinitionId;
	Candidate.DefinitionVersion = InDefinitionVersion;
	Candidate.Owner = InOwner;
	Candidate.Location = InLocation;
	Candidate.State = InState;
	Candidate.Revision = InRevision;
	Candidate.LifecycleState = InLifecycleState;
	Candidate.Metadata = InMetadata;

	if (!Candidate.IsStructurallyValid())
	{
		return false;
	}

	OutRecord = MoveTemp(Candidate);
	return true;
}

bool FRPGItemRecord::IsStructurallyValid() const
{
	if (!DefinitionId.IsValid() ||
		DefinitionVersion < 1 ||
		!Owner.IsValid() ||
		!State.HasValidIdentity() ||
		Revision < 0 ||
		!Metadata.Durability.IsValid())
	{
		return false;
	}

	if (IsActive())
	{
		return Location.IsActiveLocation() && State.IsValid();
	}

	return Location.IsTerminalLocation() && State.GetQuantity() == 0;
}

bool FRPGItemRecord::TryMoveTo(
	const FRPGItemLocation& NewLocation,
	FRPGItemRecord& OutRecord) const
{
	if (!IsActive() ||
		Metadata.bLocked ||
		!NewLocation.IsActiveLocation())
	{
		return false;
	}

	OutRecord = *this;
	OutRecord.Location = NewLocation;
	return true;
}

bool FRPGItemRecord::TryEquipTo(
	const FRPGItemLocation& EquipmentLocation,
	FRPGItemRecord& OutRecord) const
{
	if (EquipmentLocation.ContainerType !=
		ERPGItemContainerType::Equipment ||
		!TryMoveTo(EquipmentLocation, OutRecord))
	{
		return false;
	}

	if (OutRecord.Metadata.BindState == ERPGItemBindState::BindOnEquip)
	{
		OutRecord.Metadata.BindState = ERPGItemBindState::CharacterBound;
	}
	return true;
}

bool FRPGItemRecord::TrySetQuantity(
	const int32 NewQuantity,
	FRPGItemRecord& OutRecord) const
{
	if (!IsActive() || Metadata.bLocked || NewQuantity <= 0)
	{
		return false;
	}

	OutRecord = *this;
	OutRecord.State.SetQuantity(NewQuantity);
	return true;
}

bool FRPGItemRecord::TryMarkConsumed(FRPGItemRecord& OutRecord) const
{
	if (!IsActive() || Metadata.bLocked)
	{
		return false;
	}

	OutRecord = *this;
	OutRecord.State.SetQuantity(0);
	OutRecord.Location = FRPGItemLocation::MakeTerminal();
	OutRecord.LifecycleState = ERPGItemLifecycleState::Consumed;
	OutRecord.Metadata.bLocked = true;
	return true;
}

FRPGItemRecord FRPGItemRecord::CopyWithRevision(
	const int64 NewRevision) const
{
	FRPGItemRecord Result = *this;
	Result.Revision = NewRevision;
	return Result;
}
