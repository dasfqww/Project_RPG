// Fill out your copyright notice in the Description page of Project Settings.


#include "Settings/RPGGameUserSettings.h"

#include "ComponentRecreateRenderStateContext.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "UObject/StrongObjectPtr.h"

URPGGameUserSettings::URPGGameUserSettings()
{
	MasterVolume = 1.0f;
	BGMVolume = 1.0f;
	EffectVolume = 1.0f;
	bMasterMuted = false;
	bBGMMuted = false;
	bEffectMuted = false;
}

URPGGameUserSettings* URPGGameUserSettings::GetRPGGameUserSettings()
{
	return Cast<URPGGameUserSettings>(UGameUserSettings::GetGameUserSettings());
}

void URPGGameUserSettings::ApplySettings(bool bCheckForCommandLineOverrides)
{
	if (bApplyingGraphicsOptions || bSavingSettings) { return; }
	Super::ApplySettings(bCheckForCommandLineOverrides);

	OnSoundSettingsChanged.Broadcast();
}

bool URPGGameUserSettings::ApplyGraphicsOptionsWithoutSaving(FIntPoint Resolution,
	EWindowMode::Type WindowMode, bool bVSync, bool bStartup)
{
	if (!IsInGameThread() || IsUnreachable() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
		|| GetRPGGameUserSettings() != this || bApplyingGraphicsOptions || bSavingSettings || HasGraphicsPreview()
		|| Resolution.X <= 0 || Resolution.Y <= 0
		|| (WindowMode != EWindowMode::Fullscreen && WindowMode != EWindowMode::WindowedFullscreen
			&& WindowMode != EWindowMode::Windowed)) { return false; }

	TStrongObjectPtr<URPGGameUserSettings> KeepAlive(this);
	TGuardValue<bool> ApplyingGuard(bApplyingGraphicsOptions, true);
	PreviousGraphicsApplyDisplay = CaptureGraphicsDisplayState();
	bHasInitializedGraphicsOptions = true;
	SetScreenResolution(Resolution);
	SetFullscreenMode(WindowMode);
	SetVSyncEnabled(bVSync);
	const auto IsCurrentSettings = [this]()
	{
		return GetRPGGameUserSettings() == this && !IsUnreachable()
			&& !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed);
	};
	{
		FGlobalComponentRecreateRenderStateContext RenderStateContext;
		ApplyResolutionSettings(bStartup);
		if (!IsCurrentSettings()) { return false; }
		ApplyNonResolutionSettings();
	}
	if (!IsCurrentSettings()) { return false; }
	if (!bStartup)
	{
		RequestUIUpdate();
		if (!IsCurrentSettings()) { return false; }
		OnSoundSettingsChanged.Broadcast();
	}
	return IsCurrentSettings();
}

void URPGGameUserSettings::SaveSettings()
{
	if (bSavingSettings) { return; }
	TStrongObjectPtr<URPGGameUserSettings> KeepAlive(
		!HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) && !IsUnreachable() ? this : nullptr);
	bool bCancelAfterSave = false;
	{
		TGuardValue<bool> SavingGuard(bSavingSettings, true);
		if (!bGraphicsPreviewActive && !bApplyingGraphicsOptions)
		{
			Super::SaveSettings();
			return;
		}

		// Audio/input saves retain the previous display during a preview or an uncommitted graphics application.
		const FGraphicsDisplayState LiveDisplay = CaptureGraphicsDisplayState();
		SetGraphicsDisplayState(bGraphicsPreviewActive ? PreviousGraphicsDisplay : PreviousGraphicsApplyDisplay);
		Super::SaveSettings();
		if (bGraphicsPreviewActive || bApplyingGraphicsOptions) { SetGraphicsDisplayState(LiveDisplay); }
		// Cloud-save callbacks can request cancellation while the preview settings are temporarily masked.
		bCancelAfterSave = bGraphicsPreviewActive
			&& (bCancelGraphicsPreviewRequested || FPlatformTime::Seconds() >= GraphicsPreviewDeadline);
	}
	if (bCancelAfterSave) { RevertGraphicsPreview(); }
}

void URPGGameUserSettings::BeginDestroy()
{
	ClearGraphicsPreview();
	Super::BeginDestroy();
}

URPGGameUserSettings::FGraphicsDisplayState URPGGameUserSettings::CaptureGraphicsDisplayState() const
{
	FGraphicsDisplayState State;
	State.Width = ResolutionSizeX;
	State.Height = ResolutionSizeY;
	State.WindowMode = FullscreenMode;
	State.PreferredWindowMode = PreferredFullscreenMode;
	State.bVSync = bUseVSync;
	return State;
}

void URPGGameUserSettings::SetGraphicsDisplayState(const FGraphicsDisplayState& State)
{
	ResolutionSizeX = State.Width;
	ResolutionSizeY = State.Height;
	FullscreenMode = State.WindowMode;
	PreferredFullscreenMode = State.PreferredWindowMode;
	bUseVSync = State.bVSync;
}

bool URPGGameUserSettings::MatchesGraphicsPreview() const
{
	return ResolutionSizeX == PreviewGraphicsDisplay.Width && ResolutionSizeY == PreviewGraphicsDisplay.Height
		&& FullscreenMode == PreviewGraphicsDisplay.WindowMode && bUseVSync == PreviewGraphicsDisplay.bVSync;
}

bool URPGGameUserSettings::IsGraphicsPreviewOwner(const UObject* Owner) const
{
	return bGraphicsPreviewActive && Owner && GraphicsPreviewOwner.Get() == Owner;
}

int32 URPGGameUserSettings::GetGraphicsPreviewSecondsRemaining(const UObject* Owner) const
{
	return IsGraphicsPreviewOwner(Owner)
		? FMath::Max(0, FMath::CeilToInt(GraphicsPreviewDeadline - FPlatformTime::Seconds())) : 0;
}

bool URPGGameUserSettings::StartGraphicsPreview(UObject* Owner, FIntPoint Resolution,
	EWindowMode::Type WindowMode, bool bVSync)
{
	if (!IsInGameThread() || !IsValid(Owner) || HasGraphicsPreview() || bSavingSettings || bApplyingGraphicsOptions
		|| GetRPGGameUserSettings() != this || Resolution.X <= 0 || Resolution.Y <= 0
		|| ResolutionSizeX == 0 || ResolutionSizeY == 0) { return false; }
	UWorld* World = Owner->GetWorld();
	if (!IsValid(World) || !World->IsGameWorld() || World->bIsTearingDown
		|| World->GetNetMode() == NM_DedicatedServer
		|| (WindowMode != EWindowMode::Fullscreen && WindowMode != EWindowMode::WindowedFullscreen
			&& WindowMode != EWindowMode::Windowed)) { return false; }

	TStrongObjectPtr<URPGGameUserSettings> KeepAlive(this);
	PreviousGraphicsDisplay = CaptureGraphicsDisplayState();
	GraphicsPreviewOwner = Owner;
	GraphicsPreviewWorld = World;
	GraphicsPreviewDeadline = FPlatformTime::Seconds() + 15.0;
	bGraphicsPreviewActive = true;
	bCancelGraphicsPreviewRequested = false;
	bHasInitializedGraphicsOptions = true;
	GraphicsPreviewTicker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ThisClass::TickGraphicsPreview));
	{
		TGuardValue<bool> ChangingGuard(bChangingGraphicsPreview, true);
		const float ExistingResolutionQuality = ScalabilityQuality.ResolutionQuality;
		SetFullscreenMode(WindowMode);
		SetScreenResolution(Resolution);
		// A display preview must not modify or persist an unrelated render-scale preference.
		ScalabilityQuality.ResolutionQuality = ExistingResolutionQuality;
		SetVSyncEnabled(bVSync);
		ApplyResolutionSettings(false);
		ApplyNonResolutionSettings();
		PreviewGraphicsDisplay = CaptureGraphicsDisplayState();
	}
	if (bCancelGraphicsPreviewRequested || !GraphicsPreviewOwner.IsValid()
		|| !GraphicsPreviewWorld.IsValid() || World->bIsTearingDown
		|| Owner->GetWorld() != World || FPlatformTime::Seconds() >= GraphicsPreviewDeadline)
	{
		RevertGraphicsPreview();
		return false;
	}
	return true;
}

bool URPGGameUserSettings::ConfirmGraphicsPreview(const UObject* Owner)
{
	if (!IsGraphicsPreviewOwner(Owner) || bChangingGraphicsPreview || bSavingSettings) { return false; }
	if (GetRPGGameUserSettings() != this || !MatchesGraphicsPreview()
		|| !GraphicsPreviewWorld.IsValid() || GraphicsPreviewWorld->bIsTearingDown
		|| Owner->GetWorld() != GraphicsPreviewWorld.Get()
		|| FPlatformTime::Seconds() >= GraphicsPreviewDeadline)
	{
		RevertGraphicsPreview();
		return false;
	}
	ClearGraphicsPreview();
	ConfirmVideoMode();
	return true;
}

void URPGGameUserSettings::CancelGraphicsPreview(const UObject* Owner)
{
	if (!IsGraphicsPreviewOwner(Owner)) { return; }
	RevertGraphicsPreview();
}

void URPGGameUserSettings::ClearGraphicsPreview()
{
	bGraphicsPreviewActive = false;
	GraphicsPreviewOwner.Reset();
	GraphicsPreviewWorld.Reset();
	GraphicsPreviewDeadline = 0.0;
	bCancelGraphicsPreviewRequested = false;
	FTSTicker::GetCoreTicker().RemoveTicker(GraphicsPreviewTicker);
	GraphicsPreviewTicker.Reset();
}

void URPGGameUserSettings::RevertGraphicsPreview()
{
	if (!bGraphicsPreviewActive) { return; }
	if (bChangingGraphicsPreview || bSavingSettings)
	{
		bCancelGraphicsPreviewRequested = true;
		return;
	}
	TStrongObjectPtr<URPGGameUserSettings> KeepAlive(this);
	TGuardValue<bool> ChangingGuard(bChangingGraphicsPreview, true);
	const bool bRestoreDisplay = GetRPGGameUserSettings() == this && MatchesGraphicsPreview();
	const FGraphicsDisplayState PreviousDisplay = PreviousGraphicsDisplay;
	ClearGraphicsPreview();
	// A later external display change or replacement settings object owns its current values.
	if (!bRestoreDisplay) { return; }
	SetGraphicsDisplayState(PreviousDisplay);
	ApplyResolutionSettings(false);
	ApplyNonResolutionSettings();
}

bool URPGGameUserSettings::TickGraphicsPreview(float /*DeltaSeconds*/)
{
	if (!bGraphicsPreviewActive) { return false; }
	if (bChangingGraphicsPreview || bSavingSettings) { return true; }
	UObject* Owner = GraphicsPreviewOwner.Get();
	UWorld* World = GraphicsPreviewWorld.Get();
	if (!Owner || !World || World->bIsTearingDown || Owner->GetWorld() != World
		|| GetRPGGameUserSettings() != this || !MatchesGraphicsPreview()
		|| FPlatformTime::Seconds() >= GraphicsPreviewDeadline)
	{
		RevertGraphicsPreview();
	}
	return bGraphicsPreviewActive;
}

float URPGGameUserSettings::GetVolume(ESoundType Type) const
{
	switch (Type)
	{
	case ESoundType::BGM:
		return BGMVolume;
	case ESoundType::Effect:
		return EffectVolume;
	default:
		return 1.0f;
	}
}

void URPGGameUserSettings::SetVolume(ESoundType Type, float InVolume)
{
	switch (Type)
	{
	case ESoundType::BGM:
		BGMVolume = InVolume;
		break;
	case ESoundType::Effect:
		EffectVolume = InVolume;
		break;
	}
}

bool URPGGameUserSettings::IsMuted(ESoundType Type) const
{
	switch (Type)
	{
	case ESoundType::BGM:
		return bBGMMuted;
	case ESoundType::Effect:
		return bEffectMuted;
	default:
		return false;
	}
}

void URPGGameUserSettings::SetMute(ESoundType Type, bool bMute)
{
	switch (Type)
	{
	case ESoundType::BGM:
		bBGMMuted = bMute;
		break;
	case ESoundType::Effect:
		bEffectMuted = bMute;
		break;
	}
}

void URPGGameUserSettings::SetKeyMapping(const FGameplayTag& InTag, const FKey& InKey)
{
	for (FRPGKeyMapping& Mapping : CustomKeyMappings)
	{
		if (Mapping.InputTag.MatchesTagExact(InTag) && !Mapping.DefaultKey.IsValid())
		{
			Mapping.Key = InKey;
			return;
		}
	}

	FRPGKeyMapping NewMapping;
	NewMapping.InputTag = InTag;
	NewMapping.Key = InKey;
	CustomKeyMappings.Add(NewMapping);
}

FKey URPGGameUserSettings::GetKeyMapping(const FGameplayTag& InTag) const
{
	for (const FRPGKeyMapping& Mapping : CustomKeyMappings)
	{
		if (Mapping.InputTag.MatchesTagExact(InTag))
		{
			return Mapping.Key;
		}
	}
	return FKey();
}

void URPGGameUserSettings::SetKeyBinding(const FGameplayTag& InTag, const FKey& DefaultKey, const FKey& NewKey)
{
	if (!InTag.IsValid() || !DefaultKey.IsValid() || !NewKey.IsValid()) { return; }
	bool bUpdated = false;
	for (FRPGKeyMapping& Mapping : CustomKeyMappings)
	{
		if (Mapping.InputTag == InTag && Mapping.DefaultKey == DefaultKey)
		{
			// Update every matching record so an older duplicate cannot win during startup restoration.
			Mapping.Key = NewKey;
			bUpdated = true;
		}
	}
	if (bUpdated) { return; }
	FRPGKeyMapping& Added = CustomKeyMappings.AddDefaulted_GetRef();
	Added.InputTag = InTag;
	Added.DefaultKey = DefaultKey;
	Added.Key = NewKey;
}


bool URPGGameUserSettings::MatchesKeyBinding(const FGameplayTag& InTag, const FKey& DefaultKey,
	const FKey& ExpectedKey) const
{
	if (!InTag.IsValid() || !DefaultKey.IsValid() || !ExpectedKey.IsValid()) { return false; }
	bool bFound = false;
	for (const FRPGKeyMapping& Mapping : CustomKeyMappings)
	{
		if (Mapping.InputTag == InTag && Mapping.DefaultKey == DefaultKey)
		{
			if (Mapping.Key != ExpectedKey) { return false; }
			bFound = true;
		}
	}
	return bFound;
}

bool URPGGameUserSettings::CanSaveAdditionalInputBindings() const
{
	return IsInGameThread() && !bSavingSettings && !IsUnreachable()
		&& !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) && GetRPGGameUserSettings() == this;
}

FKey URPGGameUserSettings::GetFeatureKeyBinding(int32 PlayerIndex, const FSoftObjectPath& Context,
	const FSoftObjectPath& Action, FKey DefaultKey) const
{
	if (PlayerIndex < 0 || !Context.IsValid() || !Action.IsValid() || !DefaultKey.IsValid()) { return DefaultKey; }
	FKey Result;
	for (const FRPGFeatureKeyMapping& Mapping : FeatureKeyMappings)
	{
		if (Mapping.LocalPlayerIndex != PlayerIndex || Mapping.MappingContext != Context
			|| Mapping.Action != Action || Mapping.DefaultKey != DefaultKey) { continue; }
		if (!Mapping.Key.IsValid() || Mapping.Key.IsAnalog() || DefaultKey.IsAnalog()
			|| Mapping.Key.IsGamepadKey() != DefaultKey.IsGamepadKey()
			|| (Mapping.Key == EKeys::Escape && DefaultKey != EKeys::Escape)
			|| (Result.IsValid() && Result != Mapping.Key)) { return DefaultKey; }
		Result = Mapping.Key;
	}
	return Result.IsValid() ? Result : DefaultKey;
}

bool URPGGameUserSettings::SetFeatureKeyBinding(int32 PlayerIndex, const FSoftObjectPath& Context,
	const FSoftObjectPath& Action, FKey DefaultKey, FKey NewKey)
{
	if (PlayerIndex < 0 || !Context.IsValid() || !Action.IsValid() || !DefaultKey.IsValid() || DefaultKey.IsAnalog()
		|| !NewKey.IsValid() || NewKey.IsAnalog() || DefaultKey.IsGamepadKey() != NewKey.IsGamepadKey()
		|| (NewKey == EKeys::Escape && DefaultKey != EKeys::Escape)) { return false; }
	bool bUpdated = false;
	for (FRPGFeatureKeyMapping& Mapping : FeatureKeyMappings)
	{
		if (Mapping.LocalPlayerIndex == PlayerIndex && Mapping.MappingContext == Context
			&& Mapping.Action == Action && Mapping.DefaultKey == DefaultKey)
		{
			Mapping.Key = NewKey;
			bUpdated = true;
		}
	}
	if (!bUpdated)
	{
		FRPGFeatureKeyMapping& Added = FeatureKeyMappings.AddDefaulted_GetRef();
		Added.LocalPlayerIndex = PlayerIndex;
		Added.MappingContext = Context;
		Added.Action = Action;
		Added.DefaultKey = DefaultKey;
		Added.Key = NewKey;
	}
	return true;
}

bool URPGGameUserSettings::MatchesFeatureKeyBinding(int32 PlayerIndex, const FSoftObjectPath& Context,
	const FSoftObjectPath& Action, FKey DefaultKey, FKey ExpectedKey) const
{
	if (PlayerIndex < 0 || !Context.IsValid() || !Action.IsValid() || !DefaultKey.IsValid() || !ExpectedKey.IsValid()) { return false; }
	bool bFound = false;
	for (const FRPGFeatureKeyMapping& Mapping : FeatureKeyMappings)
	{
		if (Mapping.LocalPlayerIndex != PlayerIndex || Mapping.MappingContext != Context
			|| Mapping.Action != Action || Mapping.DefaultKey != DefaultKey) { continue; }
		if (Mapping.Key != ExpectedKey) { return false; }
		bFound = true;
	}
	return bFound;
}
