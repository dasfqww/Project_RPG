#pragma once

#include "CoreMinimal.h"
#include "GameFeatureAction.h"
#include "GameFeaturesSubsystem.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Input/RPGAdditionalInputProvider.h"
#include "GameFeatureAction_RPGInputMapping.generated.h"

class UInputMappingContext;
class ULocalPlayer;
class UEnhancedInputLocalPlayerSubsystem;
class UEnhancedPlayerInput;
struct FAssetBundleData;
struct FWorldContext;

USTRUCT()
struct RPGINPUTMAPPINGRUNTIME_API FRPGFeatureInputMapping
{
	GENERATED_BODY()

	/** Use a dedicated mapping context with CountRegistrations enabled. */
	UPROPERTY(EditAnywhere, Category = "Input", meta = (AssetBundles = "Client"))
	TSoftObjectPtr<UInputMappingContext> MappingContext;

	/** Shared registrations of the same context must use the same priority. */
	UPROPERTY(EditAnywhere, Category = "Input")
	int32 Priority = 0;

	/** Apply a transient copy for this player/world/PlayerInput, preserving the authored asset.
	 * Actions sharing this source must use the same copy mode and priority.
	 * Existing assets keep shared-asset registration until this is enabled explicitly.
	 */
	UPROPERTY(EditAnywhere, Category = "Input")
	bool bUsePlayerOwnedCopy = false;

	/** Show live bindings in the key options list. Shared assets remain read-only; owned copies can be edited. */
	UPROPERTY(EditAnywhere, Category = "Input")
	bool bShowInInputOptions = true;
};

/** Adds only explicitly configured mappings to local players in the selected worlds. */
UCLASS(meta = (DisplayName = "RPG Additional Input Mapping"))
class RPGINPUTMAPPINGRUNTIME_API UGameFeatureAction_RPGInputMapping : public UGameFeatureAction, public IRPGAdditionalInputProvider
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "Input", meta = (TitleProperty = "MappingContext"))
	TArray<FRPGFeatureInputMapping> InputMappings;

	/** Read-only lookup for this action's live registration. Shared mode returns the authored asset.
	 * Returns null outside the registered player's current world/input or after feature release.
	 * Does not create a copy, apply keys, rebuild input, or request persistence.
	 */
	const UInputMappingContext* FindAppliedMappingContext(const ULocalPlayer* Player,
		const UInputMappingContext* SourceMapping) const;

	virtual const UObject* GetInputOptionsOwner() const override { return this; }
	virtual void GetInputOptionContexts(const ULocalPlayer* Player,
		TArray<FRPGAdditionalInputContext>& OutContexts) const override;
	virtual bool TryApplyInputOptionBinding(ULocalPlayer* Player, const FRPGAdditionalInputBinding& Expected,
		FKey NewKey, TFunctionRef<bool(FText&)> ValidatePolicy, FText& OutError) override;
	virtual TUniquePtr<IRPGAdditionalInputRestorePlan> PrepareInputOptionRestoration(ULocalPlayer* Player,
		TConstArrayView<FRPGAdditionalInputBinding> ExpectedBindings, FText& OutError) override;
	virtual void BeginDestroy() override;

	virtual void OnGameFeatureActivating(FGameFeatureActivatingContext& Context) override;
	virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
#if WITH_EDITORONLY_DATA
	virtual void AddAdditionalAssetBundleData(FAssetBundleData& AssetBundleData) override;
#endif

private:
	struct FDefaultRestorationPlan;
	struct FMappingRegistration
	{
		// Keep the canonical asset alive while its player copy is registered; this does not pin a world/player.
		TStrongObjectPtr<UInputMappingContext> SourceMapping;
		TWeakObjectPtr<UInputMappingContext> AppliedMapping;
		int32 Priority = 0;
		bool bShowInInputOptions = true;
		bool bUsePlayerOwnedCopy = false;
	};
	struct FPlayerData
	{
		TWeakObjectPtr<ULocalPlayer> OwnerPlayer;
		TWeakObjectPtr<UWorld> OwnerWorld;
		TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> Subsystem;
		TWeakObjectPtr<UEnhancedPlayerInput> PlayerInput;
		TArray<FMappingRegistration> RegisteredMappings;
		bool bRegistrationPending = false;
	};
	struct FWorldData
	{
		FTimerHandle RefreshTimer;
		bool bRefreshing = false;
		TMap<TWeakObjectPtr<ULocalPlayer>, FPlayerData> Players;
	};
	struct FContextData
	{
		uint64 Generation = 0;
		FDelegateHandle GameInstanceStarted;
		FDelegateHandle WorldInitialized;
		FDelegateHandle WorldCleanup;
		TMap<TWeakObjectPtr<UWorld>, FWorldData> Worlds;
	};

	void NotifyRegisteredPlayersInputOptions() const;
	UInputMappingContext* FindEditableInputMapping(const ULocalPlayer* Player,
		const UInputMappingContext* Source, const UInputMappingContext* Applied) const;
	FContextData* FindActiveContext(const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation);
	FWorldData* FindWorldData(TWeakObjectPtr<UWorld> World, const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation);
	void AddToWorld(const FWorldContext& WorldContext, const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation);
	void RefreshPlayers(TWeakObjectPtr<UWorld> World, const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation);
	void RemoveWorld(UWorld* World, const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation);
	static void ReleasePlayer(FPlayerData& Data);
	static void ReleaseWorld(UWorld* World, FWorldData& Data);
	TMap<FGameFeatureStateChangeContext, FContextData> ContextData;
	uint64 NextGeneration = 0;
	bool bInputOptionsProviderRegistered = false;
};
