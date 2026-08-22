#pragma once

#include "CoreMinimal.h"

class PROJECT_RPG_API IRPGItemClock
{
public:
	virtual ~IRPGItemClock() = default;
	virtual FDateTime UtcNow() const = 0;
};

class PROJECT_RPG_API FRPGSystemItemClock final : public IRPGItemClock
{
public:
	virtual FDateTime UtcNow() const override
	{
		return FDateTime::UtcNow();
	}
};
