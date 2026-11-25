#pragma once

#include "CoreMinimal.h"

class AController;
class APawn;
class APlayerController;
class ARPGRegionGate;
class UWorld;

/** Seat files are private to one manager-owned server process. */
struct FRPGTownChannelHost
{
	static TSharedPtr<FRPGTownChannelHost> Start(UWorld& World);
	bool HasReservation(const FString& Options) const;
	bool Claim(AController& Controller, const FString& Options, const FString& PlatformId, FString& OutName);
	void Release(AController* Controller);
	void CheckOwner();
	void BeginTransfer(APlayerController& Controller, ARPGRegionGate& Gate);
	void PollTransfers();
	bool TakeTownArrival(AController* Controller, FTransform& OutArrival);

private:
	struct FTransfer
	{
		TWeakObjectPtr<ARPGRegionGate> Gate;
		TWeakObjectPtr<APawn> Pawn;
		FString RequestPath;
		FString ReplyPath;
		double Deadline = 0;
		bool bDeparting = false;
	};

	FString PendingPath(const FString& Options) const;
	FString Directory;
	bool bField = false;
	bool bPublishedReady = false;
	TMap<TWeakObjectPtr<AController>, FString> Active;
	TMap<TWeakObjectPtr<APlayerController>, FTransfer> Transfers;
	TSet<TWeakObjectPtr<AController>> TownArrivals;
	double NextOwnerCheck = 0;
	double NextTransferCheck = 0;
};
