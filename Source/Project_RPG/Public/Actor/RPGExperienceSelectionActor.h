#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "RPGExperienceSelectionActor.generated.h"

class URPGExperienceDefinition;

UENUM(BlueprintType)
enum class ERPGExperienceSelectionMode : uint8
{
	LoadExperience,
	DisableExperience
};

/** Optional map-owned selection, read once by the server before Experience loading. */
UCLASS(Blueprintable, meta = (DisplayName = "RPG Experience Selection"))
class PROJECT_RPG_API ARPGExperienceSelectionActor : public AInfo
{
	GENERATED_BODY()
public:
	ARPGExperienceSelectionActor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Disabled actors do not participate in selection or suppress project settings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Experience")
	bool bEnabled = true;

	/** DisableExperience suppresses project settings; explicit URL/command-line overrides still win. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Experience", meta = (EditCondition = "bEnabled"))
	ERPGExperienceSelectionMode SelectionMode = ERPGExperienceSelectionMode::LoadExperience;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Experience",
		meta = (EditCondition = "bEnabled && SelectionMode == ERPGExperienceSelectionMode::LoadExperience"))
	TSoftObjectPtr<URPGExperienceDefinition> Experience;

	/** Returns the one enabled persistent-level selector, or an error for an ambiguous/streamed selection. */
	static const ARPGExperienceSelectionActor* FindForWorld(const UWorld& World, FString& OutError);

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
	virtual void CheckForErrors() override;

private:
	virtual bool ActorTypeIsMainWorldOnly() const override { return true; }
#endif
};
