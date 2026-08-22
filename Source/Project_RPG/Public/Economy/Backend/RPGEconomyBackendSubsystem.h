#pragma once

#include "CoreMinimal.h"
#include "Economy/Backend/RPGEconomyBackendGateway.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RPGEconomyBackendSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(
	FRPGEconomyTransactionCommitted,
	const FRPGEconomyCommitResult&);

/** Dedicated-server owner of the server-authoritative economy gateway. */
UCLASS(Config = Game, DefaultConfig)
class PROJECT_RPG_API URPGEconomyBackendSubsystem final
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	bool IsAvailable() const { return Gateway.IsValid(); }

	bool LoadWallet(
		const FString& CharacterId,
		FRPGEconomyWalletCompletion Completion) const;

	bool Commit(
		const FRPGEconomyTransactionRequest& Request,
		FRPGEconomyCommitCompletion Completion);

	FRPGEconomyTransactionCommitted& OnTransactionCommitted()
	{
		return TransactionCommittedEvent;
	}

private:
	UPROPERTY(Config, EditAnywhere, Category = "Economy Backend")
	FString ApiUrl = TEXT("http://localhost:3000/api");

	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Economy Backend",
		meta = (ClampMin = "1.0"))
	float RequestTimeoutSeconds = 10.0f;

	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Economy Backend",
		meta = (ClampMin = "1", ClampMax = "5"))
	int32 MaximumAttempts = 2;

	FString GameServerToken;
	FString DungeonSessionId;
	TSharedPtr<FRPGEconomyBackendGateway> Gateway;
	FRPGEconomyTransactionCommitted TransactionCommittedEvent;
};
