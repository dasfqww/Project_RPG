#pragma once

#include "CoreMinimal.h"
#include "Components/GameStateComponent.h"
#include "TimerManager.h"
#include "UObject/PrimaryAssetId.h"
#include "RPGExperienceManagerComponent.generated.h"

class ULoadingProcessTask;
class ULoadingScreenManager;
class URPGExperienceDefinition;
class URPGExperienceFailureNotice;
struct FStreamableHandle;
struct FRPGExperienceSession;

UENUM(BlueprintType)
enum class ERPGExperienceLoadState : uint8
{
	Unassigned,
	Loading,
	Loaded,
	Failed,
	Deactivating
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGExperienceLoaded, const URPGExperienceDefinition*, Experience);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGExperienceLoadFailed, const FString&, Reason);
DECLARE_MULTICAST_DELEGATE(FRPGExperienceEnded);

/** An optional, server-selected Experience for this GameState's world. */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGExperienceManagerComponent : public UGameStateComponent
{
	GENERATED_BODY()
public:
	URPGExperienceManagerComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "RPG|Experience")
	bool SetExperience(TSoftObjectPtr<URPGExperienceDefinition> Experience);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "RPG|Experience")
	bool SetExperienceById(FPrimaryAssetId ExperienceId);
	UFUNCTION(BlueprintPure, Category = "RPG|Experience")
	bool IsExperienceLoaded() const { return LoadState == ERPGExperienceLoadState::Loaded; }
	UFUNCTION(BlueprintPure, Category = "RPG|Experience")
	ERPGExperienceLoadState GetLoadState() const { return LoadState; }
	UFUNCTION(BlueprintPure, Category = "RPG|Experience")
	const URPGExperienceDefinition* GetCurrentExperience() const { return CurrentExperience; }
	UFUNCTION(BlueprintPure, Category = "RPG|Experience")
	const FString& GetLastError() const { return LastError; }

	UFUNCTION(BlueprintPure, Category = "RPG|Experience")
	FSoftObjectPath GetSelectedExperiencePath() const { return SelectedExperiencePath; }

	void CallOrRegisterOnExperienceLoaded(FSimpleDelegate Callback);

	/** Native notification that this manager ended; pending feature teardown may still continue. */
	FRPGExperienceEnded OnExperienceEnded;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Experience")
	FRPGExperienceLoaded OnExperienceLoaded;
	UPROPERTY(BlueprintAssignable, Category = "RPG|Experience")
	FRPGExperienceLoadFailed OnExperienceLoadFailed;

private:
	bool SetExperiencePath(const FSoftObjectPath& Path);
	void StartLoading();
	void AcquireLoadingScreen();
	void ReleaseLoadingScreen(const TCHAR* Reason);
	void QueueFailureNotices();
	void RefreshFailureNotices();
	void ClearFailureNotices();
	void HideFailureNoticesForLoadingScreen();
	void OnDefinitionLoaded(uint64 Generation);
	void LoadFeatures(uint64 Generation);
	void LoadAssets(uint64 Generation);
	void ActivateActions(uint64 Generation);
	void Fail(const FString& Reason);
	void Stop();
	bool IsCurrent(uint64 Generation) const;

	UFUNCTION()
	void OnRep_SelectedExperiencePath();

	UFUNCTION()
	void OnRep_ServerExperienceFailed();

	UPROPERTY(ReplicatedUsing = OnRep_SelectedExperiencePath)
	FSoftObjectPath SelectedExperiencePath;
	/** Replicate only the failure state; internal diagnostics stay on the server. */
	UPROPERTY(ReplicatedUsing = OnRep_ServerExperienceFailed)
	bool bServerExperienceFailed = false;
	UPROPERTY(Transient)
	TObjectPtr<const URPGExperienceDefinition> CurrentExperience;
	/** Owns only this world's Experience loading request in the GameInstance subsystem. */
	UPROPERTY(Transient)
	TObjectPtr<ULoadingProcessTask> LoadingScreenTask;
	UPROPERTY(Transient)
	ERPGExperienceLoadState LoadState = ERPGExperienceLoadState::Unassigned;
	FString LastError;
	uint64 LoadGeneration = 0;
	FTimerHandle TimeoutTimer;
	FTimerHandle FailureNoticeTimer;
	TWeakObjectPtr<ULoadingScreenManager> FailureNoticeLoadingManager;
	FDelegateHandle LoadingVisibilityHandle;
	UPROPERTY(Transient)
	TArray<TObjectPtr<URPGExperienceFailureNotice>> FailureNotices;
	bool bRefreshingFailureNotices = false;
	TArray<FSimpleDelegate> ReadyCallbacks;
	TSharedPtr<FStreamableHandle> DefinitionHandle;
	TSharedPtr<FRPGExperienceSession> Session;
};
