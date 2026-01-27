// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "GameplayTagContainer.h"
#include "Type/RPGStructTypes.h"
#include "Type/RPGEnumTypes.h"
#include "DataTable/SkillData.h"
#include "Containers/Map.h"
#include "RPGGameInstance.generated.h"

class USoundMix;

USTRUCT(BlueprintType)
struct FRPGGameLevelSet
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, meta = (Categories = "GameData.Level"))
    FGameplayTag LevelTag;

    UPROPERTY(EditDefaultsOnly)
    TSoftObjectPtr<UWorld> Level;

    bool IsValid() const
    {
        return LevelTag.IsValid() && !Level.IsNull();
    }
};

USTRUCT(BlueprintType)
struct FSoundClassPair
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    TObjectPtr<USoundClass> SoundClass = nullptr;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    TObjectPtr<USoundMix> SoundMix = nullptr;
};

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGGameInstance : public UGameInstance
{
	GENERATED_BODY()
public:
	URPGGameInstance();

	virtual void Init() override;

	virtual void Shutdown() override;

    virtual void OnPreLoadMap(const FString& MapName);
    virtual void OnDestinationWorldLoaded(UWorld* LoadedWorld);
    virtual void OnPostWorldInitialization(UWorld* InWorld, const UWorld::InitializationValues IVS);

    UFUNCTION(BlueprintCallable)
    void InitializeGraphicOption();

    UFUNCTION(BlueprintCallable)
    void InitializeSoundOption();

    UFUNCTION(BlueprintCallable)
    void PlayBGM(USoundBase* InBGM);

    const FRPGSkillDataTable* GetSkillData(FName SkillRowName) const;

protected:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    TArray<FRPGGameLevelSet> GameLevelSets;

    ERPGGameDifficulty PendingGameDifficulty;

    UPROPERTY(EditDefaultsOnly, Category = "Skill Data")
    TObjectPtr<UDataTable> SkillDataTable;

private:

    // 각 타입별 사운드 클래스를 관리하는 TMap
    UPROPERTY(EditDefaultsOnly, Category = "Audio")
    TMap<ESoundType, FSoundClassPair> SoundClassMap;

    UPROPERTY(EditDefaultsOnly, Category = "Audio")
    FSoundClassPair MasterSound;

public:
    UFUNCTION(BlueprintPure, meta = (GameplayTagFilter = "GameData.Level"))
    TSoftObjectPtr<UWorld> GetGameLevelByTag(FGameplayTag InTag) const;

    FORCEINLINE const FSoundClassPair* GetSoundClassPair(ESoundType Type) const { return SoundClassMap.Find(Type); }
    FORCEINLINE const FSoundClassPair& GetMasterSound() const { return MasterSound; }
    FORCEINLINE ERPGGameDifficulty GetPendingGameDifficulty() const{ return PendingGameDifficulty; }
    FORCEINLINE void SetPendingGameDifficulty(ERPGGameDifficulty InGameDifficulty) { PendingGameDifficulty = InGameDifficulty; }
    //FORCEINLINE UDataTable* GetSkillDataTable()const { return SkillDataTable; }
};
