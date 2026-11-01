#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
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
	UPROPERTY(Config)
	bool bAutoShowOnlineLobby = true;
	UPROPERTY(Config)
	FString LobbyMapPackage = TEXT("/Game/Maps/LobbyMap");
	UPROPERTY(Transient)
	TObjectPtr<URPGOnlineLobbyWidget> LobbyWidget;
	FTSTicker::FDelegateHandle TickerHandle;
	bool bShuttingDown = false;
};
