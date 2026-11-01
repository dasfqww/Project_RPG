#pragma once

#include "CoreMinimal.h"
#include "GameFeatureAction.h"
#include "GameFeaturesSubsystem.h"
#include "GameFeatureAction_RPGCameraPresentation.generated.h"

class AGameStateBase;
class URPGClientPresentationComponent;
struct FWorldContext;

/** Configures a local presentation camera directly in an Experience/ActionSet. */
UCLASS(meta = (DisplayName = "RPG Camera Presentation"))
class RPGCLIENTPRESENTATIONRUNTIME_API UGameFeatureAction_RPGCameraPresentation : public UGameFeatureAction
{
	GENERATED_BODY()
public:
	/** Exactly one CameraActor in the selected world must have this Actor Tag. */
	UPROPERTY(EditAnywhere, Category = "Camera")
	FName CameraTargetActorTag = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Camera")
	int32 CameraPriority = 0;

	virtual void OnGameFeatureRegistering() override;
	virtual void OnGameFeatureActivating(FGameFeatureActivatingContext& Context) override;
	virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;
	virtual void OnGameFeatureUnregistering() override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

private:
	struct FWorldData
	{
		FDelegateHandle GameStateSetHandle;
		FDelegateHandle WorldBeginPlayHandle;
		TWeakObjectPtr<AGameStateBase> GameState;
		TWeakObjectPtr<URPGClientPresentationComponent> Component;
	};

	struct FContextData
	{
		uint64 Generation = 0;
		FName TargetTag;
		int32 Priority = 0;
		FDelegateHandle GameInstanceStarted;
		FDelegateHandle WorldInitialized;
		FDelegateHandle WorldCleanup;
		TMap<TWeakObjectPtr<UWorld>, FWorldData> Worlds;
	};

	FContextData* FindActiveContext(const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation);
	FWorldData* FindWorldData(UWorld* World, const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation);
	void AddToWorld(const FWorldContext& WorldContext, const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation);
	void AttachToGameState(UWorld* World, const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation);
	void RemoveWorld(UWorld* World, const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation);
	static void ReleaseComponent(URPGClientPresentationComponent* Component);
	static void ReleaseWorld(UWorld* World, FWorldData& Data);
	static void ReleaseContext(FContextData& Data);

	TMap<FGameFeatureStateChangeContext, FContextData> ContextData;
	uint64 NextGeneration = 0;
	bool bUnregistering = false;
};
