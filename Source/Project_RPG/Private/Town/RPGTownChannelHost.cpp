#include "Town/RPGTownChannelHost.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Manager/HttpWebManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RegionTravel/RPGRegionGate.h"
#include "RegionTravel/RPGRegionTravelComponent.h"

TSharedPtr<FRPGTownChannelHost> FRPGTownChannelHost::Start(UWorld& World)
{
	const FString Map = World.GetOutermost()->GetName();
	const bool bIsField = Map == TEXT("/Game/Maps/RegionTravel/L_PortalField");
	if (World.GetNetMode() != NM_DedicatedServer ||
		(!bIsField && Map != TEXT("/Game/Maps/RegionTravel/L_Town") && Map != TEXT("/Game/Maps/L_Bellmarket")) ||
		!FParse::Param(FCommandLine::Get(), TEXT("RPGManagedTown"))) return {};

	auto Host = MakeShared<FRPGTownChannelHost>();
	Host->bField = bIsField;
	const FString Directory = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_TOWN_DIRECTORY"));
	Host->Directory = Directory;
	int32 Port = 0;
	auto* Driver = World.GetNetDriver();
	if (!FPaths::IsRelative(Directory) && FPaths::FileExists(Directory / TEXT("owner")) &&
		FParse::Value(FCommandLine::Get(), TEXT("port="), Port) && Driver &&
		Driver->LowLevelGetNetworkNumber().EndsWith(FString::Printf(TEXT(":%d"), Port))) return Host;

	FPlatformMisc::RequestExit(false);
	return {};
}

FString FRPGTownChannelHost::PendingPath(const FString& Options) const
{
	FGuid Ticket;
	if (!FGuid::ParseExact(UGameplayStatics::ParseOption(Options, TEXT("JoinTicket")),
		EGuidFormats::DigitsWithHyphens, Ticket)) return {};
	return Directory / (Ticket.ToString(EGuidFormats::Digits).ToLower() + TEXT(".pending"));
}

bool FRPGTownChannelHost::HasReservation(const FString& Options) const
{
	const FString Pending = PendingPath(Options);
	return !Pending.IsEmpty() && FPaths::FileExists(Pending);
}

bool FRPGTownChannelHost::Claim(AController& Controller, const FString& Options, const FString& PlatformId, FString& OutName)
{
	const FString Pending = PendingPath(Options);
	FString Identity, SteamId;
	if (Pending.IsEmpty() || !FFileHelper::LoadFileToString(Identity, *Pending) ||
		!Identity.Split(TEXT("\n"), &SteamId, &OutName) || OutName.IsEmpty()) return false;
	const bool bRegionArrival = OutName.RemoveFromEnd(TEXT("\nregion"));
	if (OutName.IsEmpty()) return false;
	bool bRequireSteam = true;
#if !UE_BUILD_SHIPPING
	bRequireSteam = !FParse::Param(FCommandLine::Get(), TEXT("nosteam"));
#endif
	if (bRequireSteam && (SteamId.IsEmpty() || SteamId != PlatformId)) return false;
	auto& Files = FPlatformFileManager::Get().GetPlatformFile();
	const FString Claimed = FPaths::ChangeExtension(Pending, TEXT("active"));
	if (!Files.MoveFile(*Claimed, *Pending)) return false;
	Active.Add(&Controller, Claimed);
	if (bRegionArrival && !bField) TownArrivals.Add(&Controller);
	return true;
}

void FRPGTownChannelHost::Release(AController* Controller)
{
	TownArrivals.Remove(Controller);
	if (auto* Player = Cast<APlayerController>(Controller))
	{
		FTransfer Transfer;
		if (Transfers.RemoveAndCopyValue(Player, Transfer))
		{
			auto& Files = FPlatformFileManager::Get().GetPlatformFile();
			Files.DeleteFile(*Transfer.RequestPath);
			Files.DeleteFile(*Transfer.ReplyPath);
		}
	}
	FString Claimed;
	if (Active.RemoveAndCopyValue(Controller, Claimed))
		FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Claimed);
}

bool FRPGTownChannelHost::TakeTownArrival(AController* Controller, FTransform& OutArrival)
{
	if (TownArrivals.Remove(Controller) == 0 || !IsValid(Controller)) return false;
	for (TActorIterator<ARPGRegionGate> Gate(Controller->GetWorld()); Gate; ++Gate)
	{
		if (Gate->RegionLevel.IsNull() && IsValid(Gate->Destination) && !Gate->Destination->RegionLevel.IsNull())
		{
			OutArrival = Gate->Arrival->GetComponentTransform();
			return true;
		}
	}
	return false;
}

void FRPGTownChannelHost::BeginTransfer(APlayerController& Controller, ARPGRegionGate& Gate)
{
	const FString* Seat = Active.Find(&Controller);
	auto* Pawn = Cast<ACharacter>(Controller.GetPawn());
	auto* Travel = Controller.FindComponentByClass<URPGRegionTravelComponent>();
	const bool bPortal = bField
		? !Gate.StandaloneDestinationMap.IsNull()
		: Gate.RegionLevel.IsNull() && IsValid(Gate.Destination) && !Gate.Destination->RegionLevel.IsNull();
	if (!Seat || !bPortal || !IsValid(Pawn) || !Gate.Trigger->IsOverlappingActor(Pawn) || !Travel ||
		Transfers.Contains(&Controller) || !Travel->HoldForServerTravel(*Pawn)) return;

	const FString Nonce = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
	const FString Request = FPaths::ChangeExtension(*Seat, TEXT("travel"));
	const FString Writing = FPaths::ChangeExtension(*Seat, TEXT("writing"));
	auto& Files = FPlatformFileManager::Get().GetPlatformFile();
	if (!FFileHelper::SaveStringToFile(Nonce, *Writing, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
		!Files.MoveFile(*Request, *Writing))
	{
		Files.DeleteFile(*Writing);
		Travel->ReleaseServerTravel();
		Travel->ClientManagedRegionNotice(NSLOCTEXT("RPGRegionTravel", "CannotRequest", "지역 이동을 요청할 수 없습니다. 통로로 다시 들어가 주세요."));
		return;
	}
	Transfers.Add(&Controller, { &Gate, Pawn, Request, Directory / (Nonce + TEXT(".arrival")),
		FPlatformTime::Seconds() + 140.0 });
	Travel->ClientManagedRegionNotice(NSLOCTEXT("RPGRegionTravel", "PreparingChannel", "목적지 서버를 준비하고 있습니다."));
}

void FRPGTownChannelHost::PollTransfers()
{
	const double Now = FPlatformTime::Seconds();
	if (Transfers.IsEmpty() || Now < NextTransferCheck) return;
	NextTransferCheck = Now + 0.1;
	auto& Files = FPlatformFileManager::Get().GetPlatformFile();
	for (auto It = Transfers.CreateIterator(); It; ++It)
	{
		auto& Transfer = It.Value();
		if (Transfer.bDeparting && Now < Transfer.Deadline) continue;
		APlayerController* Controller = It.Key().Get();
		const bool bOnPortal = Controller && Transfer.Pawn.IsValid() && Controller->GetPawn() == Transfer.Pawn.Get() &&
			Transfer.Gate.IsValid() && Transfer.Gate->Trigger->IsOverlappingActor(Transfer.Pawn.Get());
		FString Result;
		if (bOnPortal && Now < Transfer.Deadline && !FFileHelper::LoadFileToString(Result, *Transfer.ReplyPath))
			continue;

		FString Address, Ticket;
		FGuid Parsed;
		if (bOnPortal && Result.Split(TEXT("\n"), &Address, &Ticket) &&
			FGuid::ParseExact(Ticket, EGuidFormats::DigitsWithHyphens, Parsed) &&
			UHttpWebManager::ConnectWithJoinTicket(Controller, Address, Ticket))
		{
			Transfer.bDeparting = true;
			Transfer.Deadline = Now + 20.0;
			Files.DeleteFile(*Transfer.RequestPath);
			Files.DeleteFile(*Transfer.ReplyPath);
			continue;
		}
		Files.DeleteFile(*Transfer.RequestPath);
		Files.DeleteFile(*Transfer.ReplyPath);
		if (Controller)
		{
			if (auto* Travel = Controller->FindComponentByClass<URPGRegionTravelComponent>())
			{
				Travel->ReleaseServerTravel();
				Travel->ClientManagedRegionNotice(bOnPortal
					? NSLOCTEXT("RPGRegionTravel", "ChannelUnavailable", "목적지 서버에 입장할 수 없습니다. 잠시 후 다시 시도해 주세요.")
					: NSLOCTEXT("RPGRegionTravel", "LeftChannelPortal", "지역 이동이 취소되었습니다. 통로로 다시 들어가 주세요."));
			}
		}
		It.RemoveCurrent();
	}
}

void FRPGTownChannelHost::CheckOwner()
{
	if (!bPublishedReady)
	{
		// BeginPlay can run before the engine can tick its listener and accept connections.
		if (!GEngine || !GEngine->IsInitialized()) return;
		bPublishedReady = FFileHelper::SaveStringToFile(TEXT("ready"), *(Directory / TEXT("ready")));
		if (!bPublishedReady)
		{
			FPlatformMisc::RequestExit(false);
			return;
		}
	}
	const double Now = FPlatformTime::Seconds();
	if (Now < NextOwnerCheck) return;
	NextOwnerCheck = Now + 5;
	const auto Heartbeat = FPlatformFileManager::Get().GetPlatformFile().GetTimeStamp(*(Directory / TEXT("owner")));
	if ((FDateTime::UtcNow() - Heartbeat).GetTotalSeconds() > 30)
		FPlatformMisc::RequestExit(false);
}
