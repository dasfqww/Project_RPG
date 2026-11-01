#pragma once

#include "CoreMinimal.h"
#include "GameFeatureAction.h"
#include "GameFeaturesSubsystem.h"
#include "GameFeatureAction_RPGOptionsUI.generated.h"

class URPGOptionsMenuComponent;
class ARPGHUD;
class UInputAction;
struct FAssetBundleData;
struct FWorldContext;

/** Owns options-menu requests and world callbacks within this feature. */
UCLASS(meta = (DisplayName = "RPG Options UI"))
class RPGOPTIONSUIRUNTIME_API UGameFeatureAction_RPGOptionsUI : public UGameFeatureAction
{
	GENERATED_BODY()
public:
	/** Optional feature-owned shortcut. Existing options configurations can leave this empty. */
	UPROPERTY(EditAnywhere, Category = "Input", meta = (AssetBundles = "Client"))
	TSoftObjectPtr<UInputAction> ToggleInputAction;

#if WITH_EDITORONLY_DATA
	virtual void AddAdditionalAssetBundleData(FAssetBundleData& AssetBundleData) override;
#endif

	virtual void OnGameFeatureActivating(FGameFeatureActivatingContext& Context) override;
	virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;

private:
	struct FHUDRequest
	{
		TWeakObjectPtr<URPGOptionsMenuComponent> Component;
		TWeakObjectPtr<UInputAction> ToggleAction;
	};
	struct FWorldData
	{
		FDelegateHandle ActorSpawnedHandle;
		FDelegateHandle WorldBeginPlayHandle;
		TMap<TWeakObjectPtr<ARPGHUD>, FHUDRequest> HUDs;
	};
	struct FContextData
	{
		uint64 Generation = 0;
		FDelegateHandle GameInstanceStarted;
		FDelegateHandle WorldInitialized;
		FDelegateHandle WorldCleanup;
		TMap<TWeakObjectPtr<UWorld>, FWorldData> Worlds;
	};

	FContextData* FindActiveContext(const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation);
	void AddToWorld(const FWorldContext& WorldContext,
		const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation);
	void InitializeOptionsForWorld(UWorld* World,
		const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation);
	void AttachToHUD(ARPGHUD* HUD, FWorldData& Data);
	void RemoveWorld(UWorld* World, const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation);
	static void ReleaseWorld(UWorld* World, FWorldData& Data);

	TMap<FGameFeatureStateChangeContext, FContextData> ContextData;
	uint64 NextGeneration = 0;
};
