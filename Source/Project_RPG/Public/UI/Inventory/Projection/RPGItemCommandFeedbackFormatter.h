#pragma once

#include "CoreMinimal.h"

struct FRPGItemCommandClientResult;

/** Player-facing text only; does not infer a rejection cause or change item state. */
class PROJECT_RPG_API FRPGItemCommandFeedbackFormatter final
{
public:
	/** A success describes the server acknowledgement, not local projection readiness. */
	static FText Build(const FRPGItemCommandClientResult& Result);
};
