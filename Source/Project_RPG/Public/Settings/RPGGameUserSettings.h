// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"
#include "GameplayTagContainer.h"
#include "Type/RPGEnumTypes.h"
#include "UObject/SoftObjectPath.h"
#include "RPGGameUserSettings.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnSoundSettingsChanged);

USTRUCT(BlueprintType)
struct FRPGKeyMapping
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Input")
	FGameplayTag InputTag;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Input")
	FKey Key;

	/** Authored key identifies an individual binding; invalid values are legacy primary overrides. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Input")
	FKey DefaultKey;
};

/** Feature bindings are independent of gameplay tags and scoped to a local player slot. */
USTRUCT()
struct FRPGFeatureKeyMapping
{
	GENERATED_BODY()

	UPROPERTY(Config)
	int32 LocalPlayerIndex = 0;

	UPROPERTY(Config)
	FSoftObjectPath MappingContext;

	UPROPERTY(Config)
	FSoftObjectPath Action;

	UPROPERTY(Config)
	FKey DefaultKey;

	UPROPERTY(Config)
	FKey Key;
};

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	URPGGameUserSettings();

	static URPGGameUserSettings* GetRPGGameUserSettings();

	virtual void ApplySettings(bool bCheckForCommandLineOverrides) override;
	virtual void SaveSettings() override;
	virtual void BeginDestroy() override;

	/** Apply the requested graphics values without requesting an INI save.
	 * Concurrent audio/input saves serialize the previous display until this synchronous application ends.
	 * Startup preserves the existing command-line override and notification behavior.
	 */
	bool ApplyGraphicsOptionsWithoutSaving(FIntPoint Resolution, EWindowMode::Type WindowMode,
		bool bVSync, bool bStartup = false);

	/** One process-wide display preview, owned by the requesting widget. No persistence until confirmation. */
	bool StartGraphicsPreview(UObject* Owner, FIntPoint Resolution, EWindowMode::Type WindowMode, bool bVSync);
	bool ConfirmGraphicsPreview(const UObject* Owner);
	void CancelGraphicsPreview(const UObject* Owner);
	bool HasGraphicsPreview() const { return bGraphicsPreviewActive || bChangingGraphicsPreview; }
	bool IsGraphicsPreviewOwner(const UObject* Owner) const;
	int32 GetGraphicsPreviewSecondsRemaining(const UObject* Owner) const;

	UPROPERTY(Config, BlueprintReadWrite, Category = "Audio")
	float MasterVolume = 1.0f;

	UPROPERTY(Config, BlueprintReadWrite, Category = "Audio")
	float BGMVolume = 1.0f;

	UPROPERTY(Config, BlueprintReadWrite, Category = "Audio")
	float EffectVolume = 1.0f;

	UPROPERTY(Config, BlueprintReadWrite, Category = "Audio")
	bool bMasterMuted = false;

	UPROPERTY(Config, BlueprintReadWrite, Category = "Audio")
	bool bBGMMuted = false;

	UPROPERTY(Config, BlueprintReadWrite, Category = "Audio")
	bool bEffectMuted = false;

	UPROPERTY(Config, BlueprintReadWrite, Category = "Input")
	TArray<FRPGKeyMapping> CustomKeyMappings;

	/** In-memory startup claims belong to this shared settings object, across feature reloads and PIE instances. */
	bool bHasInitializedGraphicsOptions = false;
	bool bHasInitializedSoundOptions = false;

	FOnSoundSettingsChanged OnSoundSettingsChanged;

	float GetVolume(ESoundType Type) const;
	void SetVolume(ESoundType Type, float InVolume);

	bool IsMuted(ESoundType Type) const;
	void SetMute(ESoundType Type, bool bMute);

	// Key Mapping Methods
	void SetKeyMapping(const FGameplayTag& InTag, const FKey& InKey);
	FKey GetKeyMapping(const FGameplayTag& InTag) const;
	void SetKeyBinding(const FGameplayTag& InTag, const FKey& DefaultKey, const FKey& NewKey);
	/** True only when an explicit binding exists and every duplicate record has the expected key. */
	bool MatchesKeyBinding(const FGameplayTag& InTag, const FKey& DefaultKey, const FKey& ExpectedKey) const;

	/** No asset loads or disk writes. Missing/invalid/inconsistent records resolve to the authored default. */
	FKey GetFeatureKeyBinding(int32 PlayerIndex, const FSoftObjectPath& Context, const FSoftObjectPath& Action,
		FKey DefaultKey) const;
	bool SetFeatureKeyBinding(int32 PlayerIndex, const FSoftObjectPath& Context, const FSoftObjectPath& Action,
		FKey DefaultKey, FKey NewKey);
	bool MatchesFeatureKeyBinding(int32 PlayerIndex, const FSoftObjectPath& Context, const FSoftObjectPath& Action,
		FKey DefaultKey, FKey ExpectedKey) const;
	bool CanSaveAdditionalInputBindings() const;

	/** Synchronous guard shared by actions that may refer to the same player's context. */
	bool bApplyingAdditionalInputBinding = false;

private:
	UPROPERTY(Config)
	TArray<FRPGFeatureKeyMapping> FeatureKeyMappings;

	struct FGraphicsDisplayState
	{
		uint32 Width = 0;
		uint32 Height = 0;
		int32 WindowMode = 2;
		int32 PreferredWindowMode = 1;
		bool bVSync = false;
	};
	FGraphicsDisplayState CaptureGraphicsDisplayState() const;
	void SetGraphicsDisplayState(const FGraphicsDisplayState& State);
	bool MatchesGraphicsPreview() const;
	bool TickGraphicsPreview(float DeltaSeconds);
	void RevertGraphicsPreview();
	void ClearGraphicsPreview();

	FGraphicsDisplayState PreviousGraphicsDisplay;
	FGraphicsDisplayState PreviewGraphicsDisplay;
	FGraphicsDisplayState PreviousGraphicsApplyDisplay;
	TWeakObjectPtr<UObject> GraphicsPreviewOwner;
	TWeakObjectPtr<UWorld> GraphicsPreviewWorld;
	FTSTicker::FDelegateHandle GraphicsPreviewTicker;
	double GraphicsPreviewDeadline = 0.0;
	bool bGraphicsPreviewActive = false;
	bool bChangingGraphicsPreview = false;
	bool bCancelGraphicsPreviewRequested = false;
	bool bSavingSettings = false;
	bool bApplyingGraphicsOptions = false;
};
