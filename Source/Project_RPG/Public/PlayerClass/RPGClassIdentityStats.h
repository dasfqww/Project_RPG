#pragma once

#include "UObject/Interface.h"
#include "RPGClassIdentityStats.generated.h"

class UAbilitySystemComponent;

struct FRPGIdentityStatRow
{
	FText Label;
	FText Value;
	FText Tooltip;
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class URPGClassIdentityStats : public UInterface
{
	GENERATED_BODY()
};

/** Optional read-only presentation provided by the class definition's identity ability CDO. */
class PROJECT_RPG_API IRPGClassIdentityStats
{
	GENERATED_BODY()
public:
	virtual void ReadIdentityStats(const UAbilitySystemComponent& ASC, FText& OutTitle,
		TArray<FRPGIdentityStatRow>& OutRows) const = 0;
};
