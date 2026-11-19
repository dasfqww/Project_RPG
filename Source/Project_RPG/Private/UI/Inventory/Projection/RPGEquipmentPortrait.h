#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class ACharacter;
class FPreviewScene;
class UMeshComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/** Owns the local equipment window's render scene and presentation-only mesh copies. */
class FRPGEquipmentPortrait
{
public:
	FRPGEquipmentPortrait();
	~FRPGEquipmentPortrait();

	UTextureRenderTarget2D* Draw(ACharacter& Character, float AspectRatio);

private:
	TUniquePtr<FPreviewScene> Scene;
	TStrongObjectPtr<UTextureRenderTarget2D> Target;
	USceneCaptureComponent2D* Capture = nullptr;
	TMap<TWeakObjectPtr<UMeshComponent>, UMeshComponent*> MeshCopies;
};
