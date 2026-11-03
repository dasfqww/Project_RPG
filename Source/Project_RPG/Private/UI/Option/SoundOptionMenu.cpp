// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Option/SoundOptionMenu.h"

#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "UI/CheckBox/RPGCheckBox.h"
#include "UI/Option/RPGSoundOptionsRuntime.h"
#include "Settings/RPGGameUserSettings.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

USoundOptionMenu::USoundOptionMenu()
{
}

TSharedRef<SWidget> USoundOptionMenu::RebuildWidget()
{
    TSharedRef<SWidget> AuthoredContent = Super::RebuildWidget();
    if (IsDesignTime()) { return AuthoredContent; }

    const TWeakObjectPtr<USoundOptionMenu> WeakThis(this);
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().FillHeight(1.f)
        [
            AuthoredContent
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
        [
            SNew(SVerticalBox)
            .Visibility_Lambda([WeakThis]()
            {
                const USoundOptionMenu* Menu = WeakThis.Get();
                return Menu && Menu->bShowNativeSoundActions ? EVisibility::Visible : EVisibility::Collapsed;
            })
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(STextBlock)
                .Text_Lambda([WeakThis]()
                {
                    const USoundOptionMenu* Menu = WeakThis.Get();
                    return Menu ? Menu->GetVolumeSummary() : FText::GetEmpty();
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
            [
                SNew(SHorizontalBox)
                .IsEnabled_Lambda([WeakThis]()
                {
                    const USoundOptionMenu* Menu = WeakThis.Get();
                    return Menu && Menu->CanInteractWithOptions();
                })
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(NSLOCTEXT("RPGSoundOptions", "SaveButton", "저장"))
                    .ContentPadding(FMargin(14.f, 6.f))
                    .OnClicked_UObject(this, &ThisClass::HandleSaveClicked)
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(8.f, 0.f, 0.f, 0.f))
                [
                    SNew(SButton)
                    .Text(NSLOCTEXT("RPGSoundOptions", "DefaultsButton", "기본값"))
                    .ToolTipText(NSLOCTEXT("RPGSoundOptions", "DefaultsTooltip", "모든 음량을 100%로 복원하고 음소거를 해제합니다."))
                    .ContentPadding(FMargin(14.f, 6.f))
                    .OnClicked_UObject(this, &ThisClass::HandleDefaultsClicked)
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
            [
                SNew(STextBlock)
                .Text_Lambda([WeakThis]()
                {
                    const USoundOptionMenu* Menu = WeakThis.Get();
                    return Menu ? Menu->LastSaveMessage : FText::GetEmpty();
                })
                .Visibility_Lambda([WeakThis]()
                {
                    const USoundOptionMenu* Menu = WeakThis.Get();
                    return Menu && !Menu->LastSaveMessage.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                .ColorAndOpacity_Lambda([WeakThis]()
                {
                    const USoundOptionMenu* Menu = WeakThis.Get();
                    return Menu && Menu->bLastSaveFailed
                        ? FSlateColor(FLinearColor(1.f, 0.35f, 0.3f)) : FSlateColor::UseForeground();
                })
                .AutoWrapText(true)
            ]
        ];
}

bool USoundOptionMenu::CanInteractWithOptions() const
{
    return bMenuConstructed && !bSynchronizingOptions && !bApplyingOptions
        && !IsRemovingFromParent() && !IsDesignTime();
}

void USoundOptionMenu::ClearSaveFeedback()
{
    LastSaveMessage = FText::GetEmpty();
    bLastSaveFailed = false;
}

void USoundOptionMenu::UnbindControls()
{
    if (MasterVolumeSlider) { MasterVolumeSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::OnMasterVolumeChanged); }
    if (BGMVolumeSlider)
    {
        BGMVolumeSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::OnVolumeChanged);
        BGMVolumeSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::OnBGMVolumeChanged);
    }
    if (EffectVolumeSlider)
    {
        EffectVolumeSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::OnVolumeChanged);
        EffectVolumeSlider->OnValueChanged.RemoveDynamic(this, &ThisClass::OnEffectVolumeChanged);
    }
    if (MasterMuteCheckBox) { MasterMuteCheckBox->OnCheckStateChanged.RemoveDynamic(this, &ThisClass::OnMasterMuteChanged); }
    if (BGMMuteCheckBox) { BGMMuteCheckBox->OnRPGMuteChanged.RemoveDynamic(this, &ThisClass::OnMuteChanged); }
    if (EffectMuteCheckBox) { EffectMuteCheckBox->OnRPGMuteChanged.RemoveDynamic(this, &ThisClass::OnMuteChanged); }
}

void USoundOptionMenu::NativeConstruct()
{
    Super::NativeConstruct();
    UnbindControls();
    InitWidgetMaps();
    bMenuConstructed = true;
    if (MasterVolumeSlider) { MasterVolumeSlider->OnValueChanged.AddUniqueDynamic(this, &ThisClass::OnMasterVolumeChanged); }
    if (BGMVolumeSlider) { BGMVolumeSlider->OnValueChanged.AddUniqueDynamic(this, &ThisClass::OnBGMVolumeChanged); }
    if (EffectVolumeSlider) { EffectVolumeSlider->OnValueChanged.AddUniqueDynamic(this, &ThisClass::OnEffectVolumeChanged); }
    if (MasterMuteCheckBox) { MasterMuteCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &ThisClass::OnMasterMuteChanged); }
    if (BGMMuteCheckBox) { BGMMuteCheckBox->OnRPGMuteChanged.AddUniqueDynamic(this, &ThisClass::OnMuteChanged); }
    if (EffectMuteCheckBox) { EffectMuteCheckBox->OnRPGMuteChanged.AddUniqueDynamic(this, &ThisClass::OnMuteChanged); }
    InitializeFromSavedSettings();
}

void USoundOptionMenu::NativeDestruct()
{
    bMenuConstructed = false;
    UnbindControls();
    SoundTypeToSliderMap.Reset();
    SoundTypeToCheckBoxMap.Reset();
    Super::NativeDestruct();
}

FRPGSoundOptionsControls USoundOptionMenu::GetRuntimeControls() const
{
    return {MasterVolumeSlider.Get(), MasterMuteCheckBox.Get(), BGMVolumeSlider.Get(),
        EffectVolumeSlider.Get(), BGMMuteCheckBox.Get(), EffectMuteCheckBox.Get()};
}

void USoundOptionMenu::InitWidgetMaps()
{
    SoundTypeToSliderMap.Reset();
    SoundTypeToCheckBoxMap.Reset();
    SoundTypeToSliderMap.Add(ESoundType::BGM, BGMVolumeSlider);
    SoundTypeToSliderMap.Add(ESoundType::Effect, EffectVolumeSlider);
    SoundTypeToCheckBoxMap.Add(ESoundType::BGM, BGMMuteCheckBox);
    SoundTypeToCheckBoxMap.Add(ESoundType::Effect, EffectMuteCheckBox);
}

void USoundOptionMenu::InitializeFromSavedSettings()
{
    if (!CanInteractWithOptions()) { return; }
    ClearSaveFeedback();
    TStrongObjectPtr<USoundOptionMenu> KeepAlive(this);
    TGuardValue<bool> Guard(bSynchronizingOptions, true);
    if (IRPGSoundOptionsRuntime* Runtime = IRPGSoundOptionsRuntime::Get())
    {
        const FRPGSoundSettings Settings = Runtime->GetCurrentSettings();
        if (!bMenuConstructed || IsRemovingFromParent()) { return; }
        IRPGSoundOptionsRuntime::SynchronizeMenuControls(Settings, GetRuntimeControls(), SoundTypeToSliderMap, SoundTypeToCheckBoxMap);
    }
}

void USoundOptionMenu::OnMasterVolumeChanged(float Value)
{
    ApplySoundChange([Value](FRPGSoundSettings& Values) { Values.MasterVolume = Value; });
}

void USoundOptionMenu::OnMasterMuteChanged(bool bMute)
{
    ApplySoundChange([bMute](FRPGSoundSettings& Values) { Values.bMasterMuted = bMute; });
}

void USoundOptionMenu::ApplyChannelVolume(ESoundType Type, float Value)
{
    if (Type != ESoundType::BGM && Type != ESoundType::Effect) { return; }
    ApplySoundChange([Type, Value](FRPGSoundSettings& Values)
    {
        if (Type == ESoundType::BGM) { Values.BGMVolume = Value; }
        else { Values.EffectVolume = Value; }
    });
}

void USoundOptionMenu::ApplySoundChange(TFunctionRef<void(FRPGSoundSettings&)> Update)
{
    if (!CanInteractWithOptions()) { return; }
    TStrongObjectPtr<USoundOptionMenu> KeepAlive(this);
    TGuardValue<bool> Guard(bApplyingOptions, true);
    ClearSaveFeedback();
    IRPGSoundOptionsRuntime* Runtime = IRPGSoundOptionsRuntime::Get();
    FText Error;
    bool bApplied = false;
    if (Runtime)
    {
        FRPGSoundSettings Requested = Runtime->GetCurrentSettings();
        Update(Requested);
        bApplied = Runtime->TryApplySettings(Requested, this, Error);
    }
    if (!bMenuConstructed || IsRemovingFromParent()) { return; }
    if (!bApplied)
    {
        // Re-read effective values when an owner change or a busy request rejected the edit.
        if (const URPGGameUserSettings* Settings = URPGGameUserSettings::GetRPGGameUserSettings())
        {
            TGuardValue<bool> SynchronizingGuard(bSynchronizingOptions, true);
            IRPGSoundOptionsRuntime::SynchronizeMenuControls(Settings->GetSoundOptions(), GetRuntimeControls(),
                SoundTypeToSliderMap, SoundTypeToCheckBoxMap);
        }
        LastSaveMessage = Error.IsEmpty()
            ? NSLOCTEXT("RPGSoundOptions", "ApplyUnavailable", "사운드 설정을 적용할 준비가 되지 않았습니다.") : Error;
        bLastSaveFailed = true;
    }
}

void USoundOptionMenu::OnBGMVolumeChanged(float Value)
{
    ApplyChannelVolume(ESoundType::BGM, Value);
}

void USoundOptionMenu::OnEffectVolumeChanged(float Value)
{
    ApplyChannelVolume(ESoundType::Effect, Value);
}

void USoundOptionMenu::OnVolumeChanged(float Value)
{
    if (!CanInteractWithOptions()) { return; }
    ESoundType Matched = ESoundType::Max;
    for (const auto& Pair : SoundTypeToSliderMap)
    {
        if (!IsValid(Pair.Value) || Pair.Value->GetValue() != Value) { continue; }
        if (Matched != ESoundType::Max) { return; }
        Matched = Pair.Key;
    }
    ApplyChannelVolume(Matched, Value);
}

void USoundOptionMenu::OnMuteChanged(bool bMute, URPGCheckBox* CheckBox)
{
    if (!CanInteractWithOptions()) { return; }
    ESoundType Type = ESoundType::Max;
    USlider* ChannelSlider = nullptr;
    if (CheckBox == BGMMuteCheckBox.Get()) { Type = ESoundType::BGM; ChannelSlider = BGMVolumeSlider.Get(); }
    else if (CheckBox == EffectMuteCheckBox.Get()) { Type = ESoundType::Effect; ChannelSlider = EffectVolumeSlider.Get(); }
    if (!IsValid(CheckBox) || !IsValid(ChannelSlider)) { return; }
    ApplySoundChange([Type, bMute](FRPGSoundSettings& Values)
    {
        if (Type == ESoundType::BGM) { Values.bBGMMuted = bMute; }
        else { Values.bEffectMuted = bMute; }
    });
}

void USoundOptionMenu::ApplySettings()
{
    if (!CanInteractWithOptions()) { return; }
    TStrongObjectPtr<USoundOptionMenu> KeepAlive(this);
    TGuardValue<bool> Guard(bApplyingOptions, true);
    IRPGSoundOptionsRuntime* Runtime = IRPGSoundOptionsRuntime::Get();
    FText SaveError;
    FRPGSoundSettings Requested;
    const bool bSettingsSaved = Runtime
        && IRPGSoundOptionsRuntime::ReadMenuSettings(GetRuntimeControls(), Requested, SaveError)
        && Runtime->TrySaveSettings(Requested, this, SaveError);
    if (!bMenuConstructed || IsRemovingFromParent()) { return; }
    if (bSettingsSaved)
    {
        LastSaveMessage = NSLOCTEXT("RPGSoundOptions", "SavedMessage", "사운드 설정을 저장했습니다.");
        bLastSaveFailed = false;
    }
    else
    {
        LastSaveMessage = SaveError.IsEmpty()
            ? NSLOCTEXT("RPGSoundOptions", "SaveRuntimeUnavailable", "사운드 설정을 저장할 준비가 되지 않았습니다.")
            : SaveError;
        bLastSaveFailed = true;
    }
}

void USoundOptionMenu::ResetToDefaults()
{
    ApplySoundChange([](FRPGSoundSettings& Values) { Values = FRPGSoundSettings(); });
    if (bLastSaveFailed) { return; }
    InitializeFromSavedSettings();
}

FText USoundOptionMenu::GetVolumeSummary() const
{
    const auto FormatVolume = [](const USlider* Slider, const UCheckBox* Mute)
    {
        const float Value = Slider ? Slider->GetValue() : 0.f;
        const int32 Percent = FMath::IsFinite(Value) ? FMath::RoundToInt(FMath::Clamp(Value, 0.f, 1.f) * 100.f) : 0;
        return Mute && Mute->IsChecked()
            ? FText::Format(NSLOCTEXT("RPGSoundOptions", "MutedVolume", "{0}% (음소거)"), FText::AsNumber(Percent))
            : FText::Format(NSLOCTEXT("RPGSoundOptions", "VolumePercent", "{0}%"), FText::AsNumber(Percent));
    };
    return FText::Format(NSLOCTEXT("RPGSoundOptions", "VolumeSummary", "전체 {0} · 배경음 {1} · 효과음 {2}"),
        FormatVolume(MasterVolumeSlider.Get(), MasterMuteCheckBox.Get()),
        FormatVolume(BGMVolumeSlider.Get(), BGMMuteCheckBox.Get()),
        FormatVolume(EffectVolumeSlider.Get(), EffectMuteCheckBox.Get()));
}

FReply USoundOptionMenu::HandleSaveClicked()
{
    ApplySettings();
    return FReply::Handled();
}

FReply USoundOptionMenu::HandleDefaultsClicked()
{
    ResetToDefaults();
    return FReply::Handled();
}

void USoundOptionMenu::SetVolumeRatioText(UTextBlock* TextBlock, float Value)
{
    if (TextBlock)
    {
        const int32 Percent = FMath::IsFinite(Value) ? FMath::RoundToInt(FMath::Clamp(Value, 0.f, 1.f) * 100.f) : 0;
        TextBlock->SetText(FText::Format(NSLOCTEXT("RPGSoundOptions", "VolumePercent", "{0}%"), FText::AsNumber(Percent)));
    }
}

float USoundOptionMenu::GetSliderValue(USlider* InSlider)
{
    return InSlider ? InSlider->GetValue() : 0.f;
}
