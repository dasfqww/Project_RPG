#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Interfaces/IHttpRequest.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "RPGAchievementSubsystem.generated.h"

struct FRPGAchievementEntry
{
    FString Id, Title;
    int32 Progress = 0, Target = 0;
    TOptional<FDateTime> EarnedAt;
};

struct FRPGAchievementSnapshot
{
    FGuid Roster;
    TArray<FRPGAchievementEntry> Entries;
    static bool Parse(const FString& Body, FGuid Character, FRPGAchievementSnapshot& Out);
    TArray<FString> NewlyEarnedSince(const FRPGAchievementSnapshot& Previous) const;
};

UCLASS()
class PROJECT_RPG_API URPGAchievementSubsystem : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    void Refresh();
    FText GetSummary() const;
private:
    UFUNCTION() void OnSessionChanged(const struct FRPGDungeonSession& Session);
    UFUNCTION() void OnStoryUpdated(bool bSuccess, const FString& Error, bool bUncertain);
    bool Tick(float Delta);
    bool ReadOwner(FString& OutEndpoint, FString& OutAccount, FGuid& OutCharacter) const;
    bool MatchesOwner() const;
    void ResetView();
    void Load();
    void ShowNotice(const FString& Titles);
    void HideNotice();
    FString Endpoint, Account;
    FGuid Character;
    FRPGAchievementSnapshot Snapshot;
    FHttpRequestPtr Request;
    FTSTicker::FDelegateHandle TickHandle;
    TSharedPtr<SWidget> Notice;
    TWeakObjectPtr<class UGameViewportClient> NoticeViewport;
    double NextRead = 0, HideAt = 0;
    uint64 Generation = 0;
    bool bReadFailed = false;
};
