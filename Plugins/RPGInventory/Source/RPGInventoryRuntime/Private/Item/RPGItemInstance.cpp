#include "Item/RPGItemInstance.h"

#include "Item/Definition/RPGItemDefinition.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGItemInstance)

void URPGItemInstance::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, Definition);
	DOREPLIFETIME(ThisClass, State);
}

bool URPGItemInstance::Initialize(
	URPGItemDefinition* InDefinition,
	const int32 RequestedQuantity,
	const int32 GenerationSeed)
{
	if (IsValid(Definition) || !IsValid(InDefinition) || RequestedQuantity <= 0)
	{
		return false;
	}

	Definition = InDefinition;
	Definition->BuildInstanceState(
		RequestedQuantity,
		GenerationSeed,
		State);
	BroadcastChanged();
	return State.IsValid();
}

bool URPGItemInstance::SetQuantity(const int32 NewQuantity)
{
	if (!IsValid(Definition))
	{
		return false;
	}

	const int32 ClampedQuantity = FMath::Clamp(
		NewQuantity,
		0,
		Definition->GetMaxStackSize());
	if (State.GetQuantity() == ClampedQuantity)
	{
		return false;
	}

	State.SetQuantity(ClampedQuantity);
	BroadcastChanged();
	return true;
}

int32 URPGItemInstance::GetMaxStackSize() const
{
	return IsValid(Definition) ? Definition->GetMaxStackSize() : 0;
}

bool URPGItemInstance::IsStackable() const
{
	return IsValid(Definition) && Definition->IsStackable();
}

void URPGItemInstance::OnRep_Definition()
{
	BroadcastChanged();
}

void URPGItemInstance::OnRep_State()
{
	BroadcastChanged();
}

void URPGItemInstance::BroadcastChanged()
{
	OnChanged.Broadcast(*this);
}
