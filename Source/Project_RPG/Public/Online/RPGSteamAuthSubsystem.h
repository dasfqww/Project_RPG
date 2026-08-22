#pragma once

#include "CoreMinimal.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RPGSteamAuthSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGSteamBackendLoginCompleted,
	bool, bSuccess);

/**
 * Obtains a Steam Web API ticket through OnlineSubsystemSteam and exchanges it
 * for the Project RPG backend access token owned by UHttpWebManager.
 */
UCLASS(Config = Game, DefaultConfig)
class PROJECT_RPG_API URPGSteamAuthSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Authentication")
	void LoginToBackendWithSteam(int32 LocalUserNum = 0);

	/** Local editor/backend integration path. It is disabled in Shipping builds. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Authentication")
	void LoginToDevelopmentBackend(const FString& SteamId);

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Authentication")
	bool IsLoginRequestInFlight() const { return bLoginRequestInFlight; }

	UPROPERTY(BlueprintAssignable, Category = "RPG|Online|Authentication")
	FRPGSteamBackendLoginCompleted OnLoginCompleted;

private:
	void HandleSteamTicketReceived(
		int32 LocalUserNum,
		bool bWasSuccessful,
		const FExternalAuthToken& AuthToken);

	UFUNCTION()
	void HandleBackendAuthenticationCompleted(bool bSuccess);

	void BeginBackendExchange(const FString& Ticket);
	void CompleteLogin(bool bSuccess);

	UPROPERTY(Config, EditAnywhere, Category = "RPG|Online|Authentication")
	FString RemoteServiceIdentity = TEXT("ProjectRpgBackend");

	bool bLoginRequestInFlight = false;
};
