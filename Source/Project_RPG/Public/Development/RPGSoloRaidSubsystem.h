#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RPGSoloRaidSubsystem.generated.h"

class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class URPGSoloRaidPanel;
class FRPGWindowInputState;
struct FKey;

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
    bool RequestMode(const FString& NewMode);
    bool SetBotPolicy(int32 Slot, const FString& Policy);
    FString GetBotPolicy(int32 Slot) const;
    FString GetSlotStatus(int32 Slot) const;
    FString GetOverview() const { return Overview; }
    int32 GetPartySize() const { return PartySize; }
    bool IsHumanLeader() const { return BotIndex < 0 && PlayerSlot == 0; }
    void TogglePanel();
    bool HandleWorkbenchKey(const FKey& Key);
private:
    void ReleaseInput();
    void TickServer();
    void TickPlayer();
    void UpdateObservation();
    void ClosePanel();
    FString ControlPath;
    FString LastCommand;
    FString Mode = TEXT("prepare");
    FString Policy = TEXT("assist");
    FString Activity = TEXT("준비 중");
    FString Overview;
    FString ControlError;
    FString LastMechanic;
    TArray<FString> SlotStatuses;
    TArray<FString> SlotPolicies;
    int32 PlayerSlot = 0;
    int32 PartySize = 4;
    int32 LeaderPlayerId = INDEX_NONE;
    double NextObservation = 0;
    double EvadeUntil = 0;
    FVector EvadeGoal = FVector::ZeroVector;
    TWeakObjectPtr<AActor> LastTarget;
    TWeakObjectPtr<class ARPGPlayer> LastPlayerPawn;
    UPROPERTY(Transient) TObjectPtr<URPGSoloRaidPanel> Panel;
    TSharedPtr<FRPGWindowInputState> PanelInput;
    double NextPoll = 0;
    double NextReport = 0;
    int32 BotIndex = -1;
    bool bReadyReported = false;
    bool bPlayerReady = false;
    bool bWorldStarted = false;
    bool bBrainHeld = false;
    TWeakObjectPtr<class ARPGBossCharacter> HeldBoss;
    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> Input;
    TWeakObjectPtr<const UInputAction> MoveAction;
    TWeakObjectPtr<const UInputAction> AttackAction;
};
