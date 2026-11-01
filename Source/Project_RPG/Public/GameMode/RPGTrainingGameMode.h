#pragma once

#include "CoreMinimal.h"
#include "Combat/RPGApprovedHitContext.h"
#include "Component/Progression/RPGTrainingObjectiveComponent.h"
#include "GameMode/RPGGameModeBase.h"
#include "RPGTrainingGameMode.generated.h"

class ARPGPlayer;
class URPGTrainingObjectiveComponent;

/** Training-only server completion. Uses the existing membership read and settlement owner. */
UCLASS()
class PROJECT_RPG_API ARPGTrainingGameMode : public ARPGGameModeBase
{
	GENERATED_BODY()
public:
	ARPGTrainingGameMode();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	/** Legacy server clear calls must pass the same training gate. */
	virtual void ReportConfiguredDungeonClear() override;

private:
	struct FMemberObservation
	{
		TWeakObjectPtr<ARPGPlayer> Pawn;
		TWeakObjectPtr<URPGTrainingObjectiveComponent> Objectives;
		TWeakObjectPtr<ARPGPlayerState> PlayerState;
		TWeakObjectPtr<AController> Controller;
		FString SessionId, CharacterId, SteamId;
		int64 ConnectionGeneration = 0;
		uint32 RestoreGeneration = 0;
		FRPGApprovedHitContext Lifetime;
		double CompletedAtSeconds = 0.0;
		bool bLastComplete = false;
	};

	bool IsTrainingScopeCurrent(const UHttpWebManager*& OutWeb) const;
	bool IsObservedLifetimeCurrent(const FMemberObservation& Observation) const;
	void CaptureObservedLifetime(FMemberObservation& Observation);
	void RefreshBindings();
	void ReadProgress(bool bFromProgressEvent);
	UFUNCTION()
	void HandleTrainingProgressChanged(const FRPGTrainingObjectiveProgress& Progress);
	UFUNCTION()
	void HandleTrainingSessionUpdated(const FRPGDungeonSession& Session, bool bSuccess);

	TArray<FMemberObservation> Observations;
	TMap<FString, FString> KnownMembers;
	FString KnownSessionId, KnownServerId;
	TWeakObjectPtr<UHttpWebManager> ObservedWeb;
	FTimerHandle TrainingCompletionTimer;
	bool bCompletionSubmitted = false;
	bool bReadingProgress = false;
	bool bTrainingEnding = false;
	bool bHasStartRoster = false;
	bool bRosterConflict = false;
};
