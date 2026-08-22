#pragma once

#include "Engine/DataAsset.h"
#include "RPGUserFacingExperienceDefinition.generated.h"

class UCommonSession_HostSessionRequest;
class UTexture2D;
class UUserWidget;

/** Display and session settings for an experience shown in the front end. */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGUserFacingExperienceDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Experience", meta = (AllowedTypes = "Map"))
	FPrimaryAssetId MapID;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Experience", meta = (AllowedTypes = "RPGExperienceDefinition"))
	FPrimaryAssetId ExperienceID;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Experience")
	TMap<FString, FString> ExtraArgs;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Experience")
	FText TileTitle;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Experience")
	FText TileSubTitle;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Experience")
	FText TileDescription;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Experience")
	TObjectPtr<UTexture2D> TileIcon;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LoadingScreen")
	TSoftClassPtr<UUserWidget> LoadingScreenWidget;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Experience")
	bool bIsDefaultExperience = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Experience")
	bool bShowInFrontEnd = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Experience")
	bool bRecordReplay = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Experience", meta = (ClampMin = 1))
	int32 MaxPlayerCount = 4;

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"))
	UCommonSession_HostSessionRequest* CreateHostingRequest(const UObject* WorldContextObject) const;
};
