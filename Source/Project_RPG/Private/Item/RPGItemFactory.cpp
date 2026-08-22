#include "Item/RPGItemFactory.h"

#include "Item/Definition/RPGItemDefinition.h"
#include "Item/RPGItemInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGItemFactory)

URPGItemInstance* URPGItemFactory::CreateItemInstance(
	UObject* Outer,
	URPGItemDefinition* Definition,
	const int32 Quantity)
{
	return CreateItemInstanceWithSeed(
		Outer,
		Definition,
		Quantity,
		FMath::Rand());
}

URPGItemInstance* URPGItemFactory::CreateItemInstanceWithSeed(
	UObject* Outer,
	URPGItemDefinition* Definition,
	const int32 Quantity,
	const int32 GenerationSeed)
{
	if (!IsValid(Outer) || !IsValid(Definition) || Quantity <= 0)
	{
		return nullptr;
	}

	URPGItemInstance* Instance = NewObject<URPGItemInstance>(Outer);
	return Instance->Initialize(Definition, Quantity, GenerationSeed)
		? Instance
		: nullptr;
}
