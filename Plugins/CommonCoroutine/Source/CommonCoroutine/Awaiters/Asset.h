// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Awaiter.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"

namespace Coro::Private
{

//-----------------------------------------------------------------------------
// TLoadObjectAwaiter
//-----------------------------------------------------------------------------

/**
 * TSoftObjectPtr를 로드하는 Awaiter
 *
 * StreamableManager를 사용하여 에셋을 로드합니다.
 * 로드 완료 콜백에서 코루틴을 재개합니다.
 */
template<typename T>
class TLoadObjectAwaiter : public TAsyncAwaiterBase<TLoadObjectAwaiter<T>>
{
	using Super = TAsyncAwaiterBase<TLoadObjectAwaiter<T>>;

public:
	TLoadObjectAwaiter(UObject* InOwner, TSoftObjectPtr<T> InSoftPtr)
		: Super(InOwner)
		, SoftPtr(InSoftPtr)
	{
	}

	//-----------------------------------------------------------------------------
	// TAsyncAwaiterBase 메서드
	//-----------------------------------------------------------------------------

	/** 이미 로드되어 있으면 suspend하지 않습니다 */
	bool Ready() const
	{
		return SoftPtr.IsNull() || SoftPtr.Get() != nullptr;
	}

	/** StreamableManager로 비동기 로드를 시작합니다 */
	void Suspend()
	{
		FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();

		StreamHandle = StreamableManager.RequestAsyncLoad(
			SoftPtr.ToSoftObjectPath(),
			FStreamableDelegate::CreateLambda([CapturedOwner = this->Owner, CapturedContext = this->Context]()
			{
				SafeResume(CapturedOwner, CapturedContext);
			})
		);
	}

	/** 로드된 객체를 반환합니다 */
	T* GetResult() const
	{
		return SoftPtr.Get();
	}

private:
	/** 로드할 SoftObjectPtr */
	TSoftObjectPtr<T> SoftPtr;

	/** 스트리밍 핸들 */
	TSharedPtr<FStreamableHandle> StreamHandle;
};

//-----------------------------------------------------------------------------
// TLoadClassAwaiter
//-----------------------------------------------------------------------------

/**
 * TSoftClassPtr를 로드하는 Awaiter
 *
 * StreamableManager를 사용하여 클래스를 로드합니다.
 * 로드 완료 콜백에서 코루틴을 재개합니다.
 */
template<typename T>
class TLoadClassAwaiter : public TAsyncAwaiterBase<TLoadClassAwaiter<T>>
{
	using Super = TAsyncAwaiterBase<TLoadClassAwaiter<T>>;

public:
	TLoadClassAwaiter(UObject* InOwner, TSoftClassPtr<T> InSoftPtr)
		: Super(InOwner)
		, SoftPtr(InSoftPtr)
	{
	}

	//-----------------------------------------------------------------------------
	// TAsyncAwaiterBase 메서드
	//-----------------------------------------------------------------------------

	/** 이미 로드되어 있으면 suspend하지 않습니다 */
	bool Ready() const
	{
		return SoftPtr.IsNull() || SoftPtr.Get() != nullptr;
	}

	/** StreamableManager로 비동기 로드를 시작합니다 */
	void Suspend()
	{
		FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();

		StreamHandle = StreamableManager.RequestAsyncLoad(
			SoftPtr.ToSoftObjectPath(),
			FStreamableDelegate::CreateLambda([CapturedOwner = this->Owner, CapturedContext = this->Context]()
			{
				SafeResume(CapturedOwner, CapturedContext);
			})
		);
	}

	/** 로드된 클래스를 반환합니다 */
	TSubclassOf<T> GetResult() const
	{
		return SoftPtr.Get();
	}

private:
	/** 로드할 SoftClassPtr */
	TSoftClassPtr<T> SoftPtr;

	/** 스트리밍 핸들 */
	TSharedPtr<FStreamableHandle> StreamHandle;
};

//-----------------------------------------------------------------------------
// TLoadPrimaryAssetAwaiter
//-----------------------------------------------------------------------------

/**
 * FPrimaryAssetId를 로드하는 Awaiter
 *
 * AssetManager를 사용하여 PrimaryAsset을 로드합니다.
 * 로드 완료 콜백에서 코루틴을 재개합니다.
 */
template<typename T>
class TLoadPrimaryAssetAwaiter : public TAsyncAwaiterBase<TLoadPrimaryAssetAwaiter<T>>
{
	using Super = TAsyncAwaiterBase<TLoadPrimaryAssetAwaiter<T>>;

public:
	TLoadPrimaryAssetAwaiter(UObject* InOwner, const FPrimaryAssetId& InAssetId, TArray<FName> InBundles)
		: Super(InOwner)
		, AssetId(InAssetId)
		, Bundles(MoveTemp(InBundles))
	{
	}

	//-----------------------------------------------------------------------------
	// TAsyncAwaiterBase 메서드
	//-----------------------------------------------------------------------------

	/** 이미 로드되어 있으면 suspend하지 않습니다 */
	bool Ready() const
	{
		if (!AssetId.IsValid())
		{
			return true;
		}

		return UAssetManager::Get().GetPrimaryAssetObject(AssetId) != nullptr;
	}

	/** AssetManager로 비동기 로드를 시작합니다 */
	void Suspend()
	{
		UAssetManager& AssetManager = UAssetManager::Get();

		StreamHandle = AssetManager.LoadPrimaryAsset(
			AssetId,
			Bundles,
			FStreamableDelegate::CreateLambda([CapturedOwner = this->Owner, CapturedContext = this->Context]()
			{
				SafeResume(CapturedOwner, CapturedContext);
			})
		);
	}

	/** 로드된 에셋을 반환합니다 */
	T* GetResult() const
	{
		return Cast<T>(UAssetManager::Get().GetPrimaryAssetObject(AssetId));
	}

private:
	/** 로드할 PrimaryAssetId */
	FPrimaryAssetId AssetId;

	/** 로드할 번들 이름 배열 */
	TArray<FName> Bundles;

	/** 스트리밍 핸들 */
	TSharedPtr<FStreamableHandle> StreamHandle;
};

//-----------------------------------------------------------------------------
// FLoadPrimaryAssetsAwaiter
//-----------------------------------------------------------------------------

/**
 * 여러 FPrimaryAssetId를 로드하는 Awaiter
 *
 * AssetManager를 사용하여 여러 PrimaryAsset을 로드합니다.
 * 모든 에셋 로드 완료 후 코루틴을 재개합니다.
 */
class COMMONCOROUTINE_API FLoadPrimaryAssetsAwaiter : public TAsyncAwaiterBase<FLoadPrimaryAssetsAwaiter>
{
	using Super = TAsyncAwaiterBase<FLoadPrimaryAssetsAwaiter>;

public:
	FLoadPrimaryAssetsAwaiter(UObject* InOwner, TArray<FPrimaryAssetId> InAssetIds, TArray<FName> InBundles);

	//-----------------------------------------------------------------------------
	// TAsyncAwaiterBase 메서드
	//-----------------------------------------------------------------------------

	/** 모든 에셋이 이미 로드되어 있으면 suspend하지 않습니다 */
	bool Ready() const;

	/** AssetManager로 비동기 로드를 시작합니다 */
	void Suspend();

private:
	/** 로드할 PrimaryAssetId 배열 */
	TArray<FPrimaryAssetId> AssetIds;

	/** 로드할 번들 이름 배열 */
	TArray<FName> Bundles;

	/** 스트리밍 핸들 */
	TSharedPtr<FStreamableHandle> StreamHandle;
};

//-----------------------------------------------------------------------------
// FLoadGameFeatureAwaiter
//-----------------------------------------------------------------------------

/**
 * GameFeature 플러그인을 로드하고 활성화하는 Awaiter
 *
 * GameFeaturesSubsystem을 사용하여 GameFeature를 로드/활성화합니다.
 * 로드 완료 콜백에서 코루틴을 재개합니다.
 */
class COMMONCOROUTINE_API FLoadGameFeatureAwaiter : public TAsyncAwaiterBase<FLoadGameFeatureAwaiter>
{
	using Super = TAsyncAwaiterBase<FLoadGameFeatureAwaiter>;

public:
	FLoadGameFeatureAwaiter(UObject* InOwner, FString InPluginURL);

	//-----------------------------------------------------------------------------
	// TAsyncAwaiterBase 메서드
	//-----------------------------------------------------------------------------

	/** GameFeature는 항상 비동기 로드입니다 */
	bool Ready() const;

	/** GameFeature 로드를 시작합니다 */
	void Suspend();

	/** 로드 성공 여부를 반환합니다 */
	bool GetResult() const;

private:
	/** 로드할 플러그인 URL */
	FString PluginURL;

	/** 로드 성공 여부 */
	mutable bool bSuccess = false;
};

} // namespace Coro::Private

namespace Coro::Async
{

//-----------------------------------------------------------------------------
// 사용자 함수
//-----------------------------------------------------------------------------

/**
 * TSoftObjectPtr를 로드합니다
 *
 * @param Owner 코루틴을 소유한 UObject (파괴 시 안전하게 정리됨)
 * @param SoftPtr 로드할 SoftObjectPtr
 * @return 로드된 객체 포인터 (nullptr일 수 있음)
 *
 * @code
 * TCoroTask<void> LoadWeaponCoroutine(TSoftObjectPtr<UWeaponData> WeaponDataPtr)
 * {
 *     UWeaponData* WeaponData = co_await Coro::Async::LoadObject(this, WeaponDataPtr);
 *     if (WeaponData)
 *     {
 *         // 로드 완료
 *     }
 * }
 * @endcode
 */
template<typename T>
Private::TLoadObjectAwaiter<T> LoadObject(UObject* Owner, TSoftObjectPtr<T> SoftPtr)
{
	return Private::TLoadObjectAwaiter<T>(Owner, SoftPtr);
}

/**
 * TSoftClassPtr를 로드합니다
 *
 * @param Owner 코루틴을 소유한 UObject (파괴 시 안전하게 정리됨)
 * @param SoftPtr 로드할 SoftClassPtr
 * @return 로드된 클래스 (nullptr일 수 있음)
 *
 * @code
 * TCoroTask<void> LoadActorClassCoroutine(TSoftClassPtr<AActor> ActorClassPtr)
 * {
 *     TSubclassOf<AActor> ActorClass = co_await Coro::Async::LoadClass(this, ActorClassPtr);
 *     if (ActorClass)
 *     {
 *         // 로드 완료
 *     }
 * }
 * @endcode
 */
template<typename T>
Private::TLoadClassAwaiter<T> LoadClass(UObject* Owner, TSoftClassPtr<T> SoftPtr)
{
	return Private::TLoadClassAwaiter<T>(Owner, SoftPtr);
}

/**
 * FPrimaryAssetId를 로드합니다
 *
 * @tparam T 반환할 에셋 타입
 * @param Owner 코루틴을 소유한 UObject (파괴 시 안전하게 정리됨)
 * @param AssetId 로드할 PrimaryAssetId
 * @param Bundles 로드할 번들 이름 배열
 * @return 로드된 에셋 포인터 (nullptr일 수 있음)
 *
 * @code
 * TCoroTask<void> LoadWeaponCoroutine(FPrimaryAssetId WeaponId)
 * {
 *     UWeaponData* Weapon = co_await Coro::Async::LoadPrimaryAsset<UWeaponData>(this, WeaponId, {TEXT("Game")});
 *     if (Weapon)
 *     {
 *         // 로드 완료
 *     }
 * }
 * @endcode
 */
template<typename T>
Private::TLoadPrimaryAssetAwaiter<T> LoadPrimaryAsset(UObject* Owner, FPrimaryAssetId AssetId, TArray<FName> Bundles)
{
	return Private::TLoadPrimaryAssetAwaiter<T>(Owner, AssetId, MoveTemp(Bundles));
}

/**
 * 여러 FPrimaryAssetId를 로드합니다
 *
 * 다양한 타입의 에셋을 한 번에 로드할 때 사용합니다.
 *
 * @param Owner 코루틴을 소유한 UObject (파괴 시 안전하게 정리됨)
 * @param AssetIds 로드할 PrimaryAssetId 배열
 * @param Bundles 로드할 번들 이름 배열
 *
 * @code
 * TCoroTask<void> LoadAssetsCoroutine(TArray<FPrimaryAssetId> AssetIds)
 * {
 *     co_await Coro::Async::LoadPrimaryAssets(this, AssetIds, {TEXT("Game")});
 *
 *     // 로드 완료 후 각각 가져오기
 *     UAssetManager& AM = UAssetManager::Get();
 *     UWeaponData* Weapon = Cast<UWeaponData>(AM.GetPrimaryAssetObject(WeaponId));
 *     UMapData* Map = Cast<UMapData>(AM.GetPrimaryAssetObject(MapId));
 * }
 * @endcode
 */
inline Private::FLoadPrimaryAssetsAwaiter LoadPrimaryAssets(UObject* Owner, TArray<FPrimaryAssetId> AssetIds, TArray<FName> Bundles)
{
	return Private::FLoadPrimaryAssetsAwaiter(Owner, MoveTemp(AssetIds), MoveTemp(Bundles));
}

/**
 * GameFeature 플러그인을 로드하고 활성화합니다
 *
 * @param Owner 코루틴을 소유한 UObject (파괴 시 안전하게 정리됨)
 * @param PluginURL 로드할 GameFeature 플러그인 URL
 * @return 로드 성공 여부
 *
 * @code
 * TCoroTask<void> LoadFeaturesCoroutine(const FString& PluginURL)
 * {
 *     bool bSuccess = co_await Coro::Async::LoadGameFeature(this, PluginURL);
 *     if (bSuccess)
 *     {
 *         // GameFeature 활성화 완료
 *     }
 * }
 * @endcode
 */
inline Private::FLoadGameFeatureAwaiter LoadGameFeature(UObject* Owner, FString PluginURL)
{
	return Private::FLoadGameFeatureAwaiter(Owner, MoveTemp(PluginURL));
}

} // namespace Coro::Async