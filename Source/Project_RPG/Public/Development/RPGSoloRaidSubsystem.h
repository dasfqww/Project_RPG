#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RPGSoloRaidSubsystem.generated.h"

class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;

/** Explicit local developer workbench. Never created in Shipping or normal sessions. */
UCLASS()
class PROJECT_RPG_API URPGSoloRaidSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& World) override;
    virtual void Tick(float DeltaTime) override;
    virtual void Deinitialize() override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(RPGSoloRaid, STATGROUP_Tickables); }
private:
    void ReleaseInput();
    void TickServer();
    void TickPlayer();
    FString ControlPath;
    FString LastCommand;
    FString Mode = TEXT("prepare");
    double NextPoll = 0;
    double NextReport = 0;
    int32 BotIndex = -1;
    bool bReadyReported = false;
    bool bBrainHeld = false;
    TWeakObjectPtr<class ARPGBossCharacter> HeldBoss;
    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> Input;
    TWeakObjectPtr<const UInputAction> MoveAction;
    TWeakObjectPtr<const UInputAction> AttackAction;
};
