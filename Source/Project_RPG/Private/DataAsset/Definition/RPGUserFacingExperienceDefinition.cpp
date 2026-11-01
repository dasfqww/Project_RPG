#include "DataAsset/Definition/RPGUserFacingExperienceDefinition.h"

#include "CommonSessionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogRPGUserFacingExperience, Log, All);

namespace
{
	bool HasExplicitExperience(const FPrimaryAssetId& Id)
	{
		return Id.PrimaryAssetType != NAME_None || !Id.PrimaryAssetName.IsNone();
	}

	bool IsSupportedExperience(const FPrimaryAssetId& Id)
	{
		return Id.IsValid() && Id.PrimaryAssetType == FPrimaryAssetType(TEXT("RPGExperienceDefinition"));
	}
}

UCommonSession_HostSessionRequest* URPGUserFacingExperienceDefinition::CreateHostingRequest(
	const UObject* WorldContextObject) const
{
	const FPrimaryAssetId SelectedExperienceID = ExperienceID;
	if (HasExplicitExperience(SelectedExperienceID) && !IsSupportedExperience(SelectedExperienceID))
	{
		UE_LOG(LogRPGUserFacingExperience, Error,
			TEXT("RPG_EXPERIENCE FRONTEND_SELECTION_REJECTED Asset=%s Type=%s Name=%s"),
			*GetPathName(), *SelectedExperienceID.PrimaryAssetType.ToString(), *SelectedExperienceID.PrimaryAssetName.ToString());
		return nullptr;
	}
	const FString ExperienceName = SelectedExperienceID.PrimaryAssetName.ToString();
	const FString UserFacingExperienceName = GetPrimaryAssetId().PrimaryAssetName.ToString();
	const FPrimaryAssetId SelectedMapID = MapID;
	const TMap<FString, FString> SelectedExtraArgs = ExtraArgs;
	const int32 RequestedPlayerCount = MaxPlayerCount;

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
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

	Request->MapID = SelectedMapID;
	Request->ModeNameForAdvertisement = UserFacingExperienceName;
	Request->ExtraArgs = SelectedExtraArgs;
	// Preserve the legacy short-name option for existing Blueprint consumers.
	Request->ExtraArgs.Add(TEXT("Experience"), ExperienceName);
	if (SelectedExperienceID.IsValid())
	{
		// FURL matches option names without case; keep one authoritative selection.
		for (auto It = Request->ExtraArgs.CreateIterator(); It; ++It)
		{
			if (It.Key().Equals(TEXT("RPGExperience"), ESearchCase::IgnoreCase)) { It.RemoveCurrent(); }
		}
		Request->ExtraArgs.Add(TEXT("RPGExperience"), SelectedExperienceID.ToString());
	}
	Request->MaxPlayerCount = RequestedPlayerCount;

	return Request;
}

#if WITH_EDITOR
EDataValidationResult URPGUserFacingExperienceDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = CombineDataValidationResults(Super::IsDataValid(Context), EDataValidationResult::Valid);
	if (!MapID.IsValid() || MapID.PrimaryAssetType != FPrimaryAssetType(TEXT("Map")))
	{
		Context.AddError(NSLOCTEXT("RPGUserFacingExperience", "InvalidMapID",
			"MapID must be a complete Map:Name primary asset ID."));
		Result = EDataValidationResult::Invalid;
	}
	if (HasExplicitExperience(ExperienceID) && !IsSupportedExperience(ExperienceID))
	{
		Context.AddError(NSLOCTEXT("RPGUserFacingExperience", "InvalidExperienceID",
			"ExperienceID must be empty or a complete RPGExperienceDefinition:Name primary asset ID."));
		Result = EDataValidationResult::Invalid;
	}
	if (MaxPlayerCount < 1)
	{
		Context.AddError(NSLOCTEXT("RPGUserFacingExperience", "InvalidPlayerCount",
			"MaxPlayerCount must be at least one."));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif
