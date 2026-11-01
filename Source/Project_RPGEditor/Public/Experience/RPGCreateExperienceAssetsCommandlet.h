#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RPGCreateExperienceAssetsCommandlet.generated.h"

/** Creates only missing sample Experience assets. */
UCLASS()
class PROJECT_RPGEDITOR_API URPGCreateExperienceAssetsCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	URPGCreateExperienceAssetsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
