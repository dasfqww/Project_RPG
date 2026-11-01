#pragma once

#include "CoreMinimal.h"
#include "Component/PawnExtensionComponentBase.h"
#include "Progression/RPGCharacterProgressionSnapshot.h"
#include "RPGCharacterProgressionComponent.generated.h"

class ARPGPlayerState;
class URPGPlayerClassComponent;
class URPGPlayerClassDefinition;
struct FStreamableHandle;

UENUM(BlueprintType)
enum class ERPGProgressionRestoreState : uint8 { Waiting, Loading, Applying, Ready, Failed };

USTRUCT(BlueprintType)
struct FRPGCharacterProgressionView
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) ERPGProgressionRestoreState State = ERPGProgressionRestoreState::Waiting;
	UPROPERTY(BlueprintReadOnly) FString ProfileId;
	UPROPERTY(BlueprintReadOnly) int64 BackendRevision = 0;
	UPROPERTY(BlueprintReadOnly) int32 Experience = 0;
	UPROPERTY(BlueprintReadOnly) int32 Level = 0;
	UPROPERTY(BlueprintReadOnly) int32 Milestones = 0;
	UPROPERTY(BlueprintReadOnly) FText Error;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRPGCharacterProgressionChanged);

/** Supplies a verified backend record to the existing growth/grant/slot owners once per Pawn. */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGCharacterProgressionComponent final : public UPawnExtensionComponentBase
{
	GENERATED_BODY()
public:
	URPGCharacterProgressionComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnUnregister() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** Server possession/startup hook. No caller-supplied record or character identity. */
	void NotifyOwnerContextChanged();
	UFUNCTION(BlueprintPure, Category = "RPG|Progression")
	FRPGCharacterProgressionView GetProgressionView() const { return View; }
	UPROPERTY(BlueprintAssignable, Category = "RPG|Progression")
	FRPGCharacterProgressionChanged OnProgressionChanged;

private:
	void BindPlayerState();
	void TryInitialize();
	bool HasCurrentContext() const;
	bool CanRebindReadyContext() const;
	void CaptureContext();
	void HandleRecord(uint64 Serial, FRPGCharacterProgressionResult Result);
	void HandleClassLoaded(uint64 Serial);
	void ApplyRecord();
	void Fail(const FText& Error);
	void StopPending();
	UFUNCTION() void HandleIdentityChanged(const FString& Identity);
	UFUNCTION() void HandlePawnRestarted(APawn* Pawn);
	UFUNCTION() void OnRep_View();
	UPROPERTY(ReplicatedUsing = OnRep_View) FRPGCharacterProgressionView View;
	UPROPERTY(Transient) TObjectPtr<URPGPlayerClassDefinition> LoadedDefinition;
	TWeakObjectPtr<URPGPlayerClassComponent> ClassOwner;
	TWeakObjectPtr<ARPGPlayerState> ObservedState;
	TWeakObjectPtr<ARPGPlayerState> ExpectedState;
	TWeakObjectPtr<AController> ExpectedController;
	TWeakObjectPtr<UWorld> ExpectedWorld;
	FString ExpectedCharacterId, ExpectedSteamId, ExpectedSessionId;
	int64 ExpectedConnectionGeneration = 0;
	FRPGCharacterProgressionSnapshot Snapshot;
	TSharedPtr<FStreamableHandle> LoadHandle;
	FDelegateHandle ApplicationChangedHandle;
	uint64 RequestSerial = 0;
	uint32 SkillRestoreRequestId = 0;
	bool bStarted = false, bFinished = false, bApplying = false, bRecordReceived = false, bStopped = false;
};
