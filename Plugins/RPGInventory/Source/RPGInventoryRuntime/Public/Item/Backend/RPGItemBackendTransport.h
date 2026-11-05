#pragma once

#include "CoreMinimal.h"
#include "Item/Backend/RPGItemBackendTypes.h"

/** Narrow async transport port. Tests can replace HTTP without a web server. */
class RPGINVENTORYRUNTIME_API IRPGItemBackendTransport
{
public:
	virtual ~IRPGItemBackendTransport() = default;

	virtual void Send(
		const FRPGItemBackendHttpRequest& Request,
		FRPGItemBackendHttpCompletion Completion) = 0;
};

/** Unreal HTTP implementation. The bearer token is retained in memory only. */
class RPGINVENTORYRUNTIME_API FRPGHttpItemBackendTransport final
	: public IRPGItemBackendTransport
{
public:
	FRPGHttpItemBackendTransport(
		FString InApiUrl,
		FString InBearerToken,
		float InTimeoutSeconds);

	virtual void Send(
		const FRPGItemBackendHttpRequest& Request,
		FRPGItemBackendHttpCompletion Completion) override;

private:
	FString ApiUrl;
	FString BearerToken;
	float TimeoutSeconds = 10.0f;
};
