#include "Town/RPGTownChannelHost.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

TSharedPtr<FRPGTownChannelHost> FRPGTownChannelHost::Start(UWorld& World)
{
	if (World.GetNetMode() != NM_DedicatedServer ||
		World.GetOutermost()->GetName() != TEXT("/Game/Maps/RegionTravel/L_Town") ||
		!FParse::Param(FCommandLine::Get(), TEXT("RPGManagedTown"))) return {};

	auto Host = MakeShared<FRPGTownChannelHost>();
	const FString Directory = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_TOWN_DIRECTORY"));
	Host->Directory = Directory;
	int32 Port = 0;
	auto* Driver = World.GetNetDriver();
	if (!FPaths::IsRelative(Directory) && FPaths::FileExists(Directory / TEXT("owner")) &&
		FParse::Value(FCommandLine::Get(), TEXT("port="), Port) && Driver &&
		Driver->LowLevelGetNetworkNumber().EndsWith(FString::Printf(TEXT(":%d"), Port)) &&
		FFileHelper::SaveStringToFile(TEXT("ready"), *(Directory / TEXT("ready")))) return Host;

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
	bool bRequireSteam = true;
#if !UE_BUILD_SHIPPING
	bRequireSteam = !FParse::Param(FCommandLine::Get(), TEXT("nosteam"));
#endif
	if (bRequireSteam && (SteamId.IsEmpty() || SteamId != PlatformId)) return false;
	auto& Files = FPlatformFileManager::Get().GetPlatformFile();
	const FString Claimed = FPaths::ChangeExtension(Pending, TEXT("active"));
	if (!Files.MoveFile(*Claimed, *Pending)) return false;
	Active.Add(&Controller, Claimed);
	return true;
}

void FRPGTownChannelHost::Release(AController* Controller)
{
	FString Claimed;
	if (Active.RemoveAndCopyValue(Controller, Claimed))
		FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Claimed);
}

void FRPGTownChannelHost::CheckOwner()
{
	const double Now = FPlatformTime::Seconds();
	if (Now < NextOwnerCheck) return;
	NextOwnerCheck = Now + 5;
	const auto Heartbeat = FPlatformFileManager::Get().GetPlatformFile().GetTimeStamp(*(Directory / TEXT("owner")));
	if ((FDateTime::UtcNow() - Heartbeat).GetTotalSeconds() > 30)
		FPlatformMisc::RequestExit(false);
}
