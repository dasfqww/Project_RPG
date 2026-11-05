#pragma once

#include "Item/Policy/RPGItemActionPolicyTypes.h"

class URPGItemDefinition;

/** Thread-safe startup registry. Runtime commands never synchronously load assets. */
class PROJECT_RPG_API FRPGItemActionPolicyRegistry final
	: public IRPGItemActionPolicyCatalog
{
public:
	bool RegisterDefinition(const URPGItemDefinition& Definition);
	bool RegisterPolicy(const FRPGItemActionPolicy& Policy);
	void Reset();

	virtual bool TryFindActionPolicy(
		const FPrimaryAssetId& DefinitionId,
		FRPGItemActionPolicy& OutPolicy) const override;

private:
	mutable FCriticalSection CriticalSection;
	TMap<FPrimaryAssetId, FRPGItemActionPolicy> Policies;
};
