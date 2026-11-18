#pragma once

#include "GameFramework/Actor.h"
#include "RPGCharacterLineupStage.generated.h"

class UPointLightComponent;
class USceneCaptureComponent2D;
class USkeletalMeshComponent;
class UTextureRenderTarget2D;

/** Local presentation actors for the lobby. The roster and selection live in the lobby widget. */
UCLASS(Transient, NotBlueprintable)
class ARPGCharacterLineupStage final : public AActor
{
	GENERATED_BODY()

public:
	static constexpr int32 SlotCount = 6;
	static constexpr int32 ImageWidth = 1152;
	static constexpr int32 ImageHeight = 360;

	ARPGCharacterLineupStage();
	void Present(uint8 OccupiedSlots, int32 SelectedSlot, bool bCloseup, bool bShowCharacter);
	UTextureRenderTarget2D* GetImage() const { return Image; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() TObjectPtr<USceneCaptureComponent2D> Capture;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> Image;
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> Characters;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> SlotLights;
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> CloseupCharacter;
	FVector CameraTarget = FVector(950, 0, 118);
};
