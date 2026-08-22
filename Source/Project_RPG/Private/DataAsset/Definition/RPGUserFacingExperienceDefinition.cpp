#include "DataAsset/Definition/RPGUserFacingExperienceDefinition.h"

#include "CommonSessionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

UCommonSession_HostSessionRequest* URPGUserFacingExperienceDefinition::CreateHostingRequest(
	const UObject* WorldContextObject) const
{
	const FString ExperienceName = ExperienceID.PrimaryAssetName.ToString();
	const FString UserFacingExperienceName = GetPrimaryAssetId().PrimaryAssetName.ToString();

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UCommonSession_HostSessionRequest* Request = nullptr;

	if (UCommonSessionSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UCommonSessionSubsystem>() : nullptr)
	{
		Request = Subsystem->CreateOnlineHostSessionRequest();
	}

	if (!Request)
	{
		Request = NewObject<UCommonSession_HostSessionRequest>();
		Request->OnlineMode = ECommonSessionOnlineMode::Online;
		Request->bUseLobbies = true;
	}

	Request->MapID = MapID;
	Request->ModeNameForAdvertisement = UserFacingExperienceName;
	Request->ExtraArgs = ExtraArgs;
	Request->ExtraArgs.Add(TEXT("Experience"), ExperienceName);
	Request->MaxPlayerCount = MaxPlayerCount;

	return Request;
}
