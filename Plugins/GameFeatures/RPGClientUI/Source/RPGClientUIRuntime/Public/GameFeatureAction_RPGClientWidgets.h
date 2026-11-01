#pragma once

#include "CoreMinimal.h"
#include "GameFeatureAction.h"
#include "GameFeaturesSubsystem.h"
#include "GameFeatureAction_RPGClientWidgets.generated.h"

class AHUD;
class UUserWidget;
class URPGClientUIWidgetComponent;
struct FAssetBundleData;
struct FWorldContext;

USTRUCT()
struct RPGCLIENTUIRUNTIME_API FRPGClientWidgetEntry
{
	GENERATED_BODY()

	/** Requests with the same ID on one HUD share one widget and must agree on configuration. */
	UPROPERTY(EditAnywhere, Category = "UI")
	FName WidgetId;

	UPROPERTY(EditAnywhere, Category = "UI", meta = (AssetBundles = "Client"))
	TSoftClassPtr<UUserWidget> WidgetClass;

	UPROPERTY(EditAnywhere, Category = "UI")
	int32 ZOrder = 0;

	/** Enable for informational overlays which should never intercept player input. */
	UPROPERTY(EditAnywhere, Category = "UI")
	bool bInputTransparent = true;
};

/** Adds explicitly configured widgets to local HUDs in the selected world contexts. */
UCLASS(meta = (DisplayName = "RPG Additional Client Widgets"))
class RPGCLIENTUIRUNTIME_API UGameFeatureAction_RPGClientWidgets : public UGameFeatureAction
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "UI", meta = (TitleProperty = "WidgetId"))
	TArray<FRPGClientWidgetEntry> Widgets;

	virtual void OnGameFeatureActivating(FGameFeatureActivatingContext& Context) override;
	virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
#if WITH_EDITORONLY_DATA
	virtual void AddAdditionalAssetBundleData(FAssetBundleData& AssetBundleData) override;
#endif

private:
	struct FHUDRequest
	{
		TWeakObjectPtr<URPGClientUIWidgetComponent> Component;
		TArray<FName> WidgetIds;
	};
	struct FWorldData
	{
		FDelegateHandle ActorSpawnedHandle;
		TMap<TWeakObjectPtr<AHUD>, FHUDRequest> HUDs;
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
	void AddToWorld(const FWorldContext& WorldContext, const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation);
	void AttachToHUD(AHUD* HUD, FWorldData& Data);
	void RemoveWorld(UWorld* World, const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation);
	static void ReleaseWorld(UWorld* World, FWorldData& Data);
	TMap<FGameFeatureStateChangeContext, FContextData> ContextData;
	uint64 NextGeneration = 0;
};
