#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/ActionBar/RPGActionBarProvider.h"
#include "RPGActionBarContext.generated.h"

class UQuickSlotComponent;
DECLARE_MULTICAST_DELEGATE(FRPGActionBarChanged);

/** Pawn-scoped local routing/projection of a feature's authoritative state. */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGActionBarContext : public UObject
{
    GENERATED_BODY()
public:
    /** Only the current provider can retire its profile; stale callbacks cannot remove a successor. */
    bool Activate(TSharedRef<IRPGActionBarProvider> InProvider);
    bool Deactivate(const TSharedRef<IRPGActionBarProvider>& ExpectedProvider);
    void Shutdown();
    bool HasOverride() const { return Provider.IsValid(); }
    bool ConsumesCombatInput() const { return HasOverride() || bTransitioning || bShuttingDown; }
    uint64 GetRevision() const { return Revision; }
    UFUNCTION(BlueprintPure) const URPGActionBarProfile* GetProfile() const { return Profile; }
    UFUNCTION(BlueprintPure) FRPGActionBarActionView ReadAction(FName ActionId) const;
    UFUNCTION(BlueprintPure) FRPGActionBarIdentityView ReadIdentity() const;
    /** Unique Source per physical input/pointer; ExpectedRevision prevents stale widgets executing. */
    void PressAction(FName Source, FName ActionId, uint64 ExpectedRevision);
    void ReleaseInput(FName Source);
    void PressSlot(int32 Slot);
    void ReleaseSlot(int32 Slot);
    bool PressTag(FGameplayTag Tag);
    void ReleaseTag(FGameplayTag Tag);
    void ReleaseAll(bool bKeepPhysicalState = false);
    FRPGActionBarChanged OnChanged;
private:
    bool ChangeProvider(TSharedPtr<IRPGActionBarProvider> Next);
    bool IsProviderCurrent() const;
    const FRPGActionBarEntry* FindAction(FName Id) const;
    UQuickSlotComponent* GetSlots() const;
    TSharedPtr<IRPGActionBarProvider> Provider;
    UPROPERTY(Transient) TObjectPtr<URPGActionBarProfile> Profile;
    TMap<FName, uint64> DownInputs;
    TSet<FGameplayTag> BaseHeldTags;
    TMap<FName, FName> PressedActions;
    uint64 Revision = 0;
    uint64 InputSerial = 0;
    uint64 CancellationRevision = 0;
    bool bTransitioning = false;
    bool bShuttingDown = false;
};
