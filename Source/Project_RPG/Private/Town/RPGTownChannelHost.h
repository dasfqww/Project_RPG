#pragma once

#include "CoreMinimal.h"

class AController;
class UWorld;

/** Seat files are private to one manager-owned server process. */
struct FRPGTownChannelHost
{
	static TSharedPtr<FRPGTownChannelHost> Start(UWorld& World);
	bool HasReservation(const FString& Options) const;
	bool Claim(AController& Controller, const FString& Options, const FString& PlatformId, FString& OutName);
	void Release(AController* Controller);
	void CheckOwner();

private:
	FString PendingPath(const FString& Options) const;
	FString Directory;
	TMap<TWeakObjectPtr<AController>, FString> Active;
	double NextOwnerCheck = 0;
};
