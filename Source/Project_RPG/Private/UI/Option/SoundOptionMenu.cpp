// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Option/SoundOptionMenu.h"
#include "Components/Slider.h"
#include "Components/CheckBox.h"
#include "Manager/DataManager.h"
#include "Manager/SoundManager.h"
#include "Components/TextBlock.h"
#include "UI/CheckBox/RPGCheckBox.h"
#include "FunctionLibrary/RPGCoreFunctionLibrary.h"

#include "RPGDebugHelper.h"

USoundOptionMenu::USoundOptionMenu()
{

}

void USoundOptionMenu::NativeConstruct()
{
	Super::NativeConstruct();

	InitWidgetMaps();

	MasterVolumeSlider->OnValueChanged.AddDynamic(this, &ThisClass::OnMasterVolumeChanged);
	BGMVolumeSlider->OnValueChanged.AddDynamic(this, &ThisClass::OnVolumeChanged);
	EffectVolumeSlider->OnValueChanged.AddDynamic(this, &ThisClass::OnVolumeChanged);

	MasterMuteCheckBox->OnCheckStateChanged.AddDynamic(this, &ThisClass::OnMasterMuteChanged);
	BGMMuteCheckBox->OnRPGMuteChanged.AddDynamic(this, &ThisClass::OnMuteChanged);
	EffectMuteCheckBox->OnRPGMuteChanged.AddDynamic(this, &ThisClass::OnMuteChanged);
	
	//TODO:Load OptionSettings
	InitializeFromSavedSettings();
}

void USoundOptionMenu::InitWidgetMaps()
{
	SoundTypeToSliderMap.Add(ESoundType::BGM, BGMVolumeSlider);
	SoundTypeToSliderMap.Add(ESoundType::Effect, EffectVolumeSlider);

	SoundTypeToCheckBoxMap.Add(ESoundType::BGM, BGMMuteCheckBox);
	SoundTypeToCheckBoxMap.Add(ESoundType::Effect, EffectMuteCheckBox);
}

void USoundOptionMenu::InitializeFromSavedSettings()
{
	FSoundSaveData SavedData;
	if (UDataManager::Get()->LoadSoundOptionsFromJson(SavedData))
	{
		MasterVolumeSlider->SetValue(SavedData.MasterVolume);
		MasterVolumeSlider->OnValueChanged.Broadcast(SavedData.MasterVolume);
		
		//Debug::Print("Loaded MasterVolume: ", SavedData.MasterVolume);

		MasterMuteCheckBox->SetIsChecked(SavedData.bMasterMuted);
		MasterMuteCheckBox->OnCheckStateChanged.Broadcast(SavedData.bMasterMuted);

		//Debug::Print("Loaded MasterMute: ", SavedData.bMasterMuted);

		for (const auto& Pair : SoundTypeToSliderMap)
		{
			FString Key = URPGCoreFunctionLibrary::GetEnumNameString(Pair.Key);
			if (SavedData.Volumes.Contains(Key))
			{
				Pair.Value->SetValue(SavedData.Volumes[Key]);
				Pair.Value->OnValueChanged.Broadcast(SavedData.Volumes[Key]);

				//Debug::Print("Loaded  %s Volume: %f: ", SavedData.Volumes[Key]);
			}
		}

		for (const auto& Pair : SoundTypeToCheckBoxMap)
		{
			FString Key = URPGCoreFunctionLibrary::GetEnumNameString(Pair.Key);
			if (SavedData.Mutes.Contains(Key))
			{
				Pair.Value->SetIsChecked(SavedData.Mutes[Key]);
				Pair.Value->OnRPGMuteChanged.Broadcast(SavedData.Mutes[Key], Pair.Value);

				//Debug::Print("Loaded  %s Mute: %f: ", SavedData.Mutes[Key]);
			}
		}
	}
}

void USoundOptionMenu::OnMasterVolumeChanged(float Value)
{
	USoundManager::Get()->SetMasterVolume(Value);
}

void USoundOptionMenu::OnMasterMuteChanged(bool bMute)
{
	float CurrentSliderValue = MasterVolumeSlider->GetValue();

	USoundManager::Get()->SetMasterMute(bMute, CurrentSliderValue);
}

void USoundOptionMenu::OnVolumeChanged(float Value)
{
	for (const TPair<ESoundType, USlider*>& Pair : SoundTypeToSliderMap)
	{
		if (Pair.Value->GetValue() == Value) // �����̴��� ���� ����Ǿ��� ��
		{
			USoundManager::Get()->SetVolume(Pair.Key, Value); // ������ ����

			break;
		}
	}
}

void USoundOptionMenu::SetVolumeRatioText(UTextBlock* TextBlock, float Value)
{
	if (TextBlock)
	{
		int32 Percent= FMath::RoundToInt(Value * 100.f);
		FText VolumeText = FText::Format(FText::FromString(TEXT("{0}%")), FText::AsNumber(Percent));
		TextBlock->SetText(VolumeText);
	}
}

void USoundOptionMenu::OnMuteChanged(bool bMute, URPGCheckBox* CheckBox)
{
	if (!CheckBox) return;

	ESoundType Type = CheckBox->GetSoundType();

	if (USlider** SliderPtr = SoundTypeToSliderMap.Find(Type))
	{
		USlider* Slider = *SliderPtr;
		float Volume = bMute ? 0.0f : Slider->GetValue();
		USoundManager::Get()->SetMute(Type, bMute, Volume);
	}
}

float USoundOptionMenu::GetSliderValue(USlider* InSlider)
{
	float Value = 0.f;

	Value = InSlider->GetValue();

	return Value;
}

void USoundOptionMenu::ApplySettings()
{
	FSoundSaveData SaveData;
	SaveData.MasterVolume = MasterVolumeSlider->GetValue();
	SaveData.bMasterMuted = MasterMuteCheckBox->IsChecked();
	SaveData.Volumes.Add("BGM", BGMVolumeSlider->GetValue());
	SaveData.Volumes.Add("Effect", EffectVolumeSlider->GetValue());
	SaveData.Mutes.Add("BGM", BGMMuteCheckBox->IsChecked());
	SaveData.Mutes.Add("Effect", EffectMuteCheckBox->IsChecked());

	UDataManager::Get()->SaveOptionToJson(SaveData, "sound_options.json");
}
