// Fill out your copyright notice in the Description page of Project Settings.


#include "GameInstance/RPGGameInstance.h"
#include "MoviePlayer.h"
#include "Manager/DataManager.h"
#include "Manager/SoundManager.h"
#include "FunctionLibrary/RPGCoreFunctionLibrary.h"
#include "GameFramework/GameUserSettings.h"
#include "DataTable/SkillData.h"

#include "RPGDebugHelper.h"

URPGGameInstance::URPGGameInstance()
{
	
}

void URPGGameInstance::Init()
{
	Super::Init();

    FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &ThisClass::OnPreLoadMap);
    FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::OnDestinationWorldLoaded);
    FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &ThisClass::OnPostWorldInitialization);

    USoundManager::Get()->Init(GetWorld());

    //InitializeSoundOption();
}

void URPGGameInstance::Shutdown()
{
	Super::Shutdown();

	
}

void URPGGameInstance::OnPreLoadMap(const FString& MapName)
{
    FLoadingScreenAttributes LoadingScreenAttributes;
    LoadingScreenAttributes.bAutoCompleteWhenLoadingCompletes = true;
    LoadingScreenAttributes.MinimumLoadingScreenDisplayTime = 2.f;
    LoadingScreenAttributes.WidgetLoadingScreen = FLoadingScreenAttributes::NewTestLoadingScreenWidget();

    GetMoviePlayer()->SetupLoadingScreen(LoadingScreenAttributes);

    
}

void URPGGameInstance::OnDestinationWorldLoaded(UWorld* LoadedWorld)
{
    GetMoviePlayer()->StopMovie();
}

void URPGGameInstance::OnPostWorldInitialization(UWorld* InWorld, const UWorld::InitializationValues IVS)
{
    if (InWorld)
    {
        USoundManager::Get()->Init(GetWorld());
    }
}

void URPGGameInstance::InitializeGraphicOption()
{
    FGraphicSaveData SavedData;
    if (UDataManager::Get()->LoadGraphicOptionsFromJson(SavedData))
    {
        // 1. ���� ���� ���� �ҷ�����
        UGameUserSettings* Settings = GEngine->GetGameUserSettings();
        if (!Settings) return;

        // 2. �ػ� �Ľ� (��: "1920x1080" ����)
        FString ResolutionStr = SavedData.Resolution; // "1920x1080"
        FString LeftStr, RightStr;
        if (ResolutionStr.Split("x", &LeftStr, &RightStr))
        {
            int32 Width = FCString::Atoi(*LeftStr);
            int32 Height = FCString::Atoi(*RightStr);
            Settings->SetScreenResolution(FIntPoint(Width, Height));
        }

        // 3. â ��� ����
        FString WindowModeStr = SavedData.WindowMode; // "Fullscreen", "Windowed", etc.
        EWindowMode::Type WindowMode = EWindowMode::Windowed;
        if (WindowModeStr == "Fullscreen")
            WindowMode = EWindowMode::Fullscreen;
        else if (WindowModeStr == "WindowedFullscreen")
            WindowMode = EWindowMode::WindowedFullscreen;
        else if (WindowModeStr == "Windowed")
            WindowMode = EWindowMode::Windowed;

        Settings->SetFullscreenMode(WindowMode);

        // 4. VSync
        bool bVSyncEnabled = SavedData.bVSync;
        Settings->SetVSyncEnabled(bVSyncEnabled);

        // 5. ���� ����
        Settings->ApplySettings(false); // true�� �ε� ȭ�� ���� �� ����

        // ���������� ����
        Settings->SaveSettings();

        /*Debug::Print(TEXT("Loaded Resolution: ")+SavedData.Resolution);
        Debug::Print(TEXT("Loaded WindowMode: %s")+SavedData.WindowMode);
        Debug::Print("Loaded VSync: ", SavedData.bVSync);*/
    }
}

void URPGGameInstance::InitializeSoundOption()
{
    FSoundSaveData SavedData;
    if (UDataManager::Get()->LoadSoundOptionsFromJson(SavedData))
    {
        USoundManager::Get()->SetMasterVolume(SavedData.MasterVolume);
        USoundManager::Get()->SetMasterMute(SavedData.bMasterMuted, SavedData.MasterVolume);

        //Debug::Print("Loaded MasterVolume: ", SavedData.MasterVolume);
        //Debug::Print("Loaded MasterMute: ", SavedData.bMasterMuted);

        // ���� �ʱ�ȭ
        for (const auto& Pair : SavedData.Volumes)
        {
            ESoundType SoundType;
            if(URPGCoreFunctionLibrary::TryConvertStringToEnum(Pair.Key, SoundType))  // ���ڿ� �� enum ��ȯ
            {
                USoundManager::Get()->SetVolume(SoundType, Pair.Value);

                //Debug::Print("Loaded  %s Volume: %f: ", SavedData.Volumes[Pair.Key]);
            }
        }

        // ��Ʈ �ʱ�ȭ
        for (const auto& Pair : SavedData.Mutes)
        {
            ESoundType SoundType;
            if (URPGCoreFunctionLibrary::TryConvertStringToEnum(Pair.Key, SoundType))  // ���ڿ� �� enum ��ȯ
            {
                USoundManager::Get()->SetMute(SoundType, Pair.Value, SavedData.Volumes[Pair.Key]);

                //Debug::Print("Loaded  %s Mute: %f: ", SavedData.Mutes[Pair.Key]);
            }
        }
    }
}

void URPGGameInstance::PlayBGM(USoundBase* InBGM)
{
    USoundManager::Get()->Play(InBGM, ESoundType::BGM);
}

const FRPGSkillDataTable* URPGGameInstance::GetSkillData(FName SkillRowName) const
{
    if (!SkillDataTable) return nullptr;
    return SkillDataTable->FindRow<FRPGSkillDataTable>(SkillRowName, TEXT("Skill Lookup"));
}

TSoftObjectPtr<UWorld> URPGGameInstance::GetGameLevelByTag(FGameplayTag InTag) const
{
    for (const FRPGGameLevelSet& GameLevelSet : GameLevelSets)
    {
        if (!GameLevelSet.IsValid()) continue;

        if (GameLevelSet.LevelTag == InTag)
        {
            return GameLevelSet.Level;
        }
    }

    return TSoftObjectPtr<UWorld>();
}
