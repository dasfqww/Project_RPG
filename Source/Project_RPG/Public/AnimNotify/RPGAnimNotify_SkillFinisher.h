#pragma once

#include "Animation/AnimNotifies/AnimNotify.h"
#include "RPGAnimNotify_SkillFinisher.generated.h"

/** One authored finishing strike. Its owning server ability validates the montage and execution phase. */
UCLASS(meta = (DisplayName = "Skill Finishing Strike"))
class PROJECT_RPG_API URPGAnimNotify_SkillFinisher : public UAnimNotify
{
	GENERATED_BODY()
public:
	URPGAnimNotify_SkillFinisher();
	virtual void BranchingPointNotify(FBranchingPointNotifyPayload& Payload) override;
};
