#pragma once

#include "CoreMinimal.h"
#include "Item/Backend/RPGItemBackendGateway.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RPGItemBackendSubsystem.generated.h"

/** Dedicated-server owner of the Item V2 HTTP gateway. */
UCLASS(Config = Game, DefaultConfig)
class PROJECT_RPG_API URPGItemBackendSubsystem final
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	bool IsAvailable() const { return Gateway.IsValid(); }

	bool LoadCharacterItems(
		const FString& CharacterId,
		FRPGItemBackendLoadCompletion Completion) const;

	bool Commit(
		const FRPGItemRepositoryCommitRequest& Request,
		FRPGItemBackendCommitCompletion Completion) const;

private:
	UPROPERTY(Config, EditAnywhere, Category = "Item Backend")
	FString ApiUrl = TEXT("http://localhost:3000/api");

	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Item Backend",
		meta = (ClampMin = "1.0"))
	float RequestTimeoutSeconds = 10.0f;

	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Item Backend",
		meta = (ClampMin = "1", ClampMax = "5"))
	int32 MaximumAttempts = 2;

	FString ServiceToken;
	TSharedPtr<FRPGItemBackendGateway> Gateway;
};
