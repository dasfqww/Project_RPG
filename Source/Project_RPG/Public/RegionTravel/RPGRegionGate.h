#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGRegionGate.generated.h"

class UBoxComponent;
class UArrowComponent;

/** Place paired gates in the persistent level. The arrow marks a safe arrival outside the trigger. */
UCLASS()
class PROJECT_RPG_API ARPGRegionGate : public AActor
{
	GENERATED_BODY()
public:
	ARPGRegionGate();
	virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Region")
	TObjectPtr<UBoxComponent> Trigger;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Region")
	TObjectPtr<UArrowComponent> Arrival;

	/** Leave empty for the persistent starting region. */
	UPROPERTY(EditAnywhere, Category="Region")
	TSoftObjectPtr<UWorld> RegionLevel;

	UPROPERTY(EditInstanceOnly, Category="Region")
	TObjectPtr<ARPGRegionGate> Destination;
};
