#pragma once

#include "Components/ActorComponent.h"
#include "Riding/RPGRidingTypes.h"
#include "RPGRidingPresentationComponent.generated.h"

class UAnimInstance;
class UAnimMontage;
class UStaticMesh;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class USceneComponent;
class URPGMountDefinition;
class URPGRidingComponent;
struct FStreamableHandle;

UENUM(BlueprintType)
enum class ERPGRidingVisualStatus : uint8 { Hidden, Loading, Ready, Failed };

/** Read-only animation input. Weapon AnimLayers and the character's component transform remain owned by their writers. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGRidingPoseView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bHasAnchor = false;
    UPROPERTY(BlueprintReadOnly) FTransform RiderWorldTransform;
    UPROPERTY(BlueprintReadOnly) TSubclassOf<UAnimInstance> PoseLayer;
    UPROPERTY(BlueprintReadOnly) FRPGRidingStateKey Key;
};

/** Local cosmetics driven only by current public Riding state. No authority, movement or attachment of the rider. */
UCLASS(ClassGroup=(RPG))
class PROJECT_RPG_API URPGRidingPresentationComponent final : public UActorComponent
{
    GENERATED_BODY()
public:
    URPGRidingPresentationComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
    virtual void TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function) override;
    UFUNCTION(BlueprintPure, Category="Riding") ERPGRidingVisualStatus GetVisualStatus() const { return Status; }
    UFUNCTION(BlueprintPure, Category="Riding") FRPGRidingPoseView GetPoseView() const;
    /** Rebuild after a local cosmetic load failure. Does not issue a mount request. */
    UFUNCTION(BlueprintCallable, Category="Riding") void RetryPresentation();

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FRPGRidingPresentationTestAccess;
#endif
    void Refresh();
    void Detach();
    void ClearVisuals();
    void HandleState(const FRPGRidingRepState& State);
    bool CanPresent() const;
    bool MatchesCurrent(const FGuid& Token) const;
    bool MatchesDefinition(const URPGMountDefinition& Definition) const;
    bool EnsureRoot();
    void BeginLoad(const URPGMountDefinition& Definition);
    void CompleteLoad(const FGuid& Token);
    void LoadPlaceholder();
    void UpdateVisibility();
    void RefreshEmote();
    void CompleteEmote(FGuid Token);
    void StopEmote();
    TWeakObjectPtr<URPGRidingComponent> Riding;
    TWeakObjectPtr<UWorld> BoundWorld;
    TWeakObjectPtr<USkeletalMeshComponent> RiderMesh;
    FDelegateHandle ChangedHandle;
    FGuid LoadToken;
    FRPGRidingRepState Requested;
    FSoftObjectPath MeshPath, AnimPath, PosePath;
    FTransform MountOffset, RiderOffset;
    FName SaddleSocket;
    TSharedPtr<FStreamableHandle> LoadHandle, PlaceholderHandle;
    TSharedPtr<FStreamableHandle> EmoteLoadHandle;
    FGuid EmoteToken;
    FRPGRidingRepState EmoteState;
    TWeakObjectPtr<UAnimInstance> EmoteAnim;
    int32 EmoteInstanceId = INDEX_NONE;
    bool bUpdatingEmote = false, bPlayingEmote = false;
    UPROPERTY(Transient) TObjectPtr<UAnimMontage> EmoteMontage;
    ERPGRidingVisualStatus Status = ERPGRidingVisualStatus::Hidden;
    bool bEnding = false, bInstalling = false, bClearPending = false, bRefreshPending = false;
    UPROPERTY(Transient) TObjectPtr<USceneComponent> VisualRoot;
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> MountMesh;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Placeholder;
    UPROPERTY(Transient) TSubclassOf<UAnimInstance> LoadedPoseLayer;
    UPROPERTY(EditDefaultsOnly, Category="Riding|Presentation") TSoftObjectPtr<UStaticMesh> PlaceholderMesh;
};
