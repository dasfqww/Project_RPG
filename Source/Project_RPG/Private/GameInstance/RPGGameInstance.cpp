// Fill out your copyright notice in the Description page of Project Settings.


#include "GameInstance/RPGGameInstance.h"
#include "UI/Option/RPGGraphicsOptionsRuntime.h"
#include "UI/Option/RPGSoundOptionsRuntime.h"
#include "MoviePlayer.h"
#include "Manager/DataManager.h"
#include "Manager/SoundManager.h"
#include "FunctionLibrary/RPGCoreFunctionLibrary.h"
#include "GameFramework/GameUserSettings.h"
#include "DataTable/SkillData.h"
#include "Framework/Application/SlateApplication.h"

#include "RPGDebugHelper.h"
#if !UE_BUILD_SHIPPING
#include "Tests/RPGAirborneJoinGate.h"
#endif

bool URPGGameInstance::DelayCompletionOfPendingNetGameTravel()
{
	if (Super::DelayCompletionOfPendingNetGameTravel()) return true;
#if !UE_BUILD_SHIPPING
	return RPGAirborneJoinGate::DelayCompletion(*this);
#else
	return false;
#endif
}


URPGGameInstance::URPGGameInstance()
{
	
}

void URPGGameInstance::Init()
{
	Super::Init();

    FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &ThisClass::OnPreLoadMap);
    FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::OnDestinationWorldLoaded);
    FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &ThisClass::OnPostWorldInitialization);

    USoundManager::Get(this)->Init(GetWorld());

}

void URPGGameInstance::Shutdown()
{
	FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
	FWorldDelegates::OnPostWorldInitialization.RemoveAll(this);
	Super::Shutdown();

	
}

void URPGGameInstance::OnPreLoadMap(const FString& MapName)
{
    // Dedicated servers have no Slate application. Keep loading-screen widgets
    // on presentation instances while retaining the authored gameplay data.
    if (IsDedicatedServerInstance() || !FSlateApplication::IsInitialized())
    {
        return;
    }
    FLoadingScreenAttributes LoadingScreenAttributes;
    LoadingScreenAttributes.bAutoCompleteWhenLoadingCompletes = true;
    LoadingScreenAttributes.MinimumLoadingScreenDisplayTime = 2.f;
    LoadingScreenAttributes.WidgetLoadingScreen = FLoadingScreenAttributes::NewTestLoadingScreenWidget();

    GetMoviePlayer()->SetupLoadingScreen(LoadingScreenAttributes);

    
}

void URPGGameInstance::OnDestinationWorldLoaded(UWorld* LoadedWorld)
{
    if (LoadedWorld && LoadedWorld->GetGameInstance() == this
        && !IsDedicatedServerInstance() && FSlateApplication::IsInitialized())
    {
        GetMoviePlayer()->StopMovie();
    }
}

void URPGGameInstance::OnPostWorldInitialization(UWorld* InWorld, const UWorld::InitializationValues IVS)
{
    if (InWorld && InWorld->GetGameInstance() == this)
    {
        USoundManager::Get(this)->Init(InWorld);
    }
}

void URPGGameInstance::InitializeGraphicOption()
{
    if (IRPGGraphicsOptionsRuntime* Runtime = IRPGGraphicsOptionsRuntime::Get())
    {
        Runtime->InitializeForWorld(GetWorld());
    }
}

void URPGGameInstance::InitializeSoundOption()
{
    if (IRPGSoundOptionsRuntime* Runtime = IRPGSoundOptionsRuntime::Get())
    {
        Runtime->InitializeForWorld(GetWorld());
    }
}

void URPGGameInstance::PlayBGM(USoundBase* InBGM)
{
    USoundManager::Get(this)->Play(InBGM, ESoundType::BGM);
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
