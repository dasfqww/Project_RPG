#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "UIManager.generated.h"

class URPGWidgetBase;

/** Popup ordering owned by one local player. */
UCLASS()
class PROJECT_RPG_API UUIManager : public ULocalPlayerSubsystem
{
    GENERATED_BODY()

public:
    /** Game-thread access. Always returns a receiver; prefer Get(Context) for player ownership. */
    static UUIManager* Get();
    /** Missing owners use a stateless receiver. It never retains widgets. */
    static UUIManager* Get(const UObject* Context);

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

    void PushUI(URPGWidgetBase* NewUI);
    void PopUI();
    void RemoveUI(URPGWidgetBase* TargetUI);
    /** Remove a stack reference after an external RemoveFromParent/feature teardown. */
    void ForgetUI(URPGWidgetBase* TargetUI);
    void ClearUI();
    URPGWidgetBase* GetTopUI() const;
    bool HasUIOpen() const { return GetTopUI() != nullptr; }

private:
    UPROPERTY()
    TArray<TObjectPtr<URPGWidgetBase>> PopUpUIStack;

    TMap<TWeakObjectPtr<URPGWidgetBase>, TWeakObjectPtr<UWorld>> WidgetWorlds;
    bool bClearingUI = false;
    bool bDeinitializing = true;

    /** Nullable lookup for routing widget requests without recursing through the fallback. */
    static UUIManager* Find(const UObject* Context);

    void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
    void ClearUIForWorld(UWorld* World);
    void ActivateTopUI();
};
