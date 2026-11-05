#pragma once

#include "CoreMinimal.h"

class RPGINVENTORYRUNTIME_API IRPGItemClock
{
public:
	virtual ~IRPGItemClock() = default;
	virtual FDateTime UtcNow() const = 0;
};

class RPGINVENTORYRUNTIME_API FRPGSystemItemClock final : public IRPGItemClock
{
public:
	virtual FDateTime UtcNow() const override
	{
		return FDateTime::UtcNow();
	}
};
