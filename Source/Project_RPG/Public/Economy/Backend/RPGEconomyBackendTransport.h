#pragma once

#include "CoreMinimal.h"
#include "Economy/Backend/RPGEconomyBackendTypes.h"

class PROJECT_RPG_API IRPGEconomyBackendTransport
{
public:
	virtual ~IRPGEconomyBackendTransport() = default;

	virtual void Send(
		const FRPGEconomyHttpRequest& Request,
		FRPGEconomyHttpCompletion Completion) = 0;
};

class PROJECT_RPG_API FRPGHttpEconomyBackendTransport final
	: public IRPGEconomyBackendTransport
{
public:
	FRPGHttpEconomyBackendTransport(
		FString InApiUrl,
		FString InBearerToken,
		float InTimeoutSeconds);

	virtual void Send(
		const FRPGEconomyHttpRequest& Request,
		FRPGEconomyHttpCompletion Completion) override;

private:
	FString ApiUrl;
	FString BearerToken;
	float TimeoutSeconds = 10.0f;
};
