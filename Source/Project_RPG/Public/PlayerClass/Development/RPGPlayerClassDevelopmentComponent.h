#pragma once

#include "CoreMinimal.h"
#include "Component/PawnExtensionComponentBase.h"
#include "UObject/SoftObjectPath.h"
#include "RPGPlayerClassDevelopmentComponent.generated.h"

class URPGPlayerClassComponent;
class URPGPlayerClassDevelopmentProfile;
class AController;
class APlayerState;
struct FStreamableHandle;

/** Opt-in editor-build server profile supplier. No client RPC, URL option or production fallback. */
UCLASS()
class PROJECT_RPG_API URPGPlayerClassDevelopmentComponent final : public UPawnExtensionComponentBase
{
	GENERATED_BODY()
public:
	URPGPlayerClassDevelopmentComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnUnregister() override;

private:
	void TryInitialize();
	UFUNCTION() void HandlePawnRestarted(APawn* Pawn);
	void HandleProfileLoaded(uint64 Serial);
	bool HasCurrentOwnerContext() const;
	void FailInitialization(const FText& Error);
	void StopPendingInitialization();
	UPROPERTY(Transient)
	TObjectPtr<URPGPlayerClassDevelopmentProfile> LoadedProfile;
	TWeakObjectPtr<URPGPlayerClassComponent> ClassOwner;
	TWeakObjectPtr<UWorld> ExpectedWorld;
	TWeakObjectPtr<AController> ExpectedController;
	TWeakObjectPtr<APlayerState> ExpectedPlayerState;
	FSoftObjectPath RequestedProfile;
	TSharedPtr<FStreamableHandle> LoadHandle;
	FDelegateHandle ApplicationChangedHandle;
	uint64 LoadSerial = 0;
	uint32 RestoreRequestId = 0;
	bool bRequestStarted = false;
	bool bInitializing = false;
	bool bFinished = false;
};
