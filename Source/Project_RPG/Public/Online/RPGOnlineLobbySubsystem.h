#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RPGOnlineLobbySubsystem.generated.h"

class APlayerController;
class URPGOnlineLobbyWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGBackendLogoutCompleted, bool, bSuccess);

/** Local frontend only. Does not create accounts, sessions or travel without user input. */
UCLASS(Config = Game)
class PROJECT_RPG_API URPGOnlineLobbySubsystem final : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	bool IsLobbyController(const APlayerController* Controller) const;
	bool IsCharacterSelectionController(const APlayerController* Controller) const;
	bool IsSeparateGameplayLobbyController(const APlayerController* Controller) const;
	bool OpenCharacterSelection(APlayerController* Controller);
	bool EnterGameplayLobby(APlayerController* Controller);
	bool IsFrontendTravelPending() const;
	bool HasFrontendTravelFailed() const;
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Lobby")
	URPGOnlineLobbyWidget* GetLobbyWidget() const { return LobbyWidget; }
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Authentication")
	bool CanLogoutFromBackend(const APlayerController* Controller) const;
	/** Backend credential logout only; never abandons a dungeon or logs out of Steam itself. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Authentication")
	bool LogoutFromBackend(APlayerController* Controller);
	UPROPERTY(BlueprintAssignable, Category = "RPG|Online|Authentication")
	FRPGBackendLogoutCompleted OnLogoutCompleted;
	static bool IsLogoutAllowed(bool bLocalLobby, bool bHasCredential, bool bActiveDungeon,
		bool bUnresolvedAbandonment, bool bParty, bool bBusy);
private:
	bool TickLobby(float DeltaTime);
	bool IsLocalControllerInMap(const APlayerController* Controller, const FString& Package) const;
	bool OpenFrontendMap(APlayerController* Controller, const FString& Package);
	void HandleFrontendTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error);
	UPROPERTY(Config)
	bool bAutoShowOnlineLobby = true;
	UPROPERTY(Config)
	FString LobbyMapPackage = TEXT("/Game/Maps/LobbyMap");
	UPROPERTY(Config)
	FString CharacterSelectionMapPackage;
	UPROPERTY(Transient)
	TObjectPtr<URPGOnlineLobbyWidget> LobbyWidget;
	FTSTicker::FDelegateHandle TickerHandle;
	TWeakObjectPtr<UWorld> PendingFrontendTravel;
	TWeakObjectPtr<UWorld> FailedFrontendTravel;
	bool bShuttingDown = false;
};
