// Copyright Epic Games, Inc. All Rights Reserved.

#include "Awaiters/Asset.h"
#include "GameFeaturePluginOperationResult.h"
#include "GameFeaturesSubsystem.h"

namespace Coro::Private
{

//-----------------------------------------------------------------------------
// FLoadPrimaryAssetsAwaiter
//-----------------------------------------------------------------------------

FLoadPrimaryAssetsAwaiter::FLoadPrimaryAssetsAwaiter(UObject* InOwner, TArray<FPrimaryAssetId> InAssetIds, TArray<FName> InBundles)
	: Super(InOwner)
	, AssetIds(MoveTemp(InAssetIds))
	, Bundles(MoveTemp(InBundles))
{
}

bool FLoadPrimaryAssetsAwaiter::Ready() const
{
	if (AssetIds.IsEmpty())
	{
		return true;
	}

	UAssetManager& AssetManager = UAssetManager::Get();

	for (const FPrimaryAssetId& AssetId : AssetIds)
	{
		if (!AssetManager.GetPrimaryAssetObject(AssetId))
		{
			return false;
		}
	}

	return true;
}

void FLoadPrimaryAssetsAwaiter::Suspend()
{
	UAssetManager& AssetManager = UAssetManager::Get();

	StreamHandle = AssetManager.LoadPrimaryAssets(
		AssetIds,
		Bundles,
		FStreamableDelegate::CreateLambda([CapturedOwner = Owner, CapturedContext = Context]()
		{
			SafeResume(CapturedOwner, CapturedContext);
		})
	);
}

//-----------------------------------------------------------------------------
// FLoadGameFeatureAwaiter
//-----------------------------------------------------------------------------

FLoadGameFeatureAwaiter::FLoadGameFeatureAwaiter(UObject* InOwner, FString InPluginURL)
	: Super(InOwner)
	, PluginURL(MoveTemp(InPluginURL))
{
}

bool FLoadGameFeatureAwaiter::Ready() const
{
	return false;
}

void FLoadGameFeatureAwaiter::Suspend()
{
	UGameFeaturesSubsystem::Get().LoadAndActivateGameFeaturePlugin(
		PluginURL,
		FGameFeaturePluginLoadComplete::CreateLambda([CapturedOwner = Owner, CapturedContext = Context, SuccessPtr = &bSuccess](const UE::GameFeatures::FResult& Result)
		{
			// Owner가 유효하면 Awaiter도 유효합니다 (코루틴 프레임에 저장됨)
			if (CapturedOwner.IsValid())
			{
				*SuccessPtr = Result.HasValue();
			}
			SafeResume(CapturedOwner, CapturedContext);
		})
	);
}

bool FLoadGameFeatureAwaiter::GetResult() const
{
	return bSuccess;
}

} // namespace Coro::Private
