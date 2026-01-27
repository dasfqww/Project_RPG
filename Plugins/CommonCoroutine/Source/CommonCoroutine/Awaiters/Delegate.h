// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Awaiter.h"

namespace Coro::Private
{

//-----------------------------------------------------------------------------
// FSimpleDelegateAwaiter
//-----------------------------------------------------------------------------

/**
 * FSimpleMulticastDelegate를 대기하는 Awaiter
 *
 * 델리게이트가 Broadcast될 때까지 대기합니다.
 * Broadcast 후 자동으로 바인딩이 해제됩니다.
 */
class COMMONCOROUTINE_API FSimpleDelegateAwaiter : public TAsyncAwaiterBase<FSimpleDelegateAwaiter>
{
	using Super = TAsyncAwaiterBase<FSimpleDelegateAwaiter>;

public:
	FSimpleDelegateAwaiter(UObject* InOwner, FSimpleMulticastDelegate& InDelegate);
	~FSimpleDelegateAwaiter();

	//-----------------------------------------------------------------------------
	// TAsyncAwaiterBase 메서드
	//-----------------------------------------------------------------------------

	/** 델리게이트에 바인딩하고 대기합니다 */
	void Suspend();

private:
	/** 대기 중인 델리게이트 */
	FSimpleMulticastDelegate* Delegate;

	/** 바인딩 핸들 */
	FDelegateHandle Handle;
};

//-----------------------------------------------------------------------------
// TDelegateAwaiter (파라미터 여러 개)
//-----------------------------------------------------------------------------

/**
 * TMulticastDelegate를 대기하는 Awaiter
 *
 * 델리게이트가 Broadcast될 때까지 대기하고,
 * Broadcast 시 전달된 파라미터를 튜플로 반환합니다.
 */
template<typename... Args>
class TDelegateAwaiter : public TAsyncAwaiterBase<TDelegateAwaiter<Args...>>
{
	using Super = TAsyncAwaiterBase<TDelegateAwaiter<Args...>>;

public:
	using FDelegateType = TMulticastDelegate<void(Args...)>;
	using FResultType = TTuple<std::decay_t<Args>...>;

	TDelegateAwaiter(UObject* InOwner, FDelegateType& InDelegate)
		: Super(InOwner)
		, Delegate(&InDelegate)
	{
	}

	~TDelegateAwaiter()
	{
		if (Handle.IsValid())
		{
			Delegate->Remove(Handle);
		}
	}

	//-----------------------------------------------------------------------------
	// TAsyncAwaiterBase 메서드
	//-----------------------------------------------------------------------------

	/** 델리게이트에 바인딩하고 대기합니다 */
	void Suspend()
	{
		Handle = Delegate->AddLambda([CapturedOwner = this->Owner, CapturedContext = this->Context, DelegatePtr = Delegate, HandlePtr = &Handle, ResultPtr = &Result](Args... InArgs)
		{
			// Owner가 유효하면 Awaiter도 유효합니다 (코루틴 프레임에 저장됨)
			if (CapturedOwner.IsValid())
			{
				*ResultPtr = FResultType(Forward<Args>(InArgs)...);
				DelegatePtr->Remove(*HandlePtr);
				HandlePtr->Reset();
			}
			SafeResume(CapturedOwner, CapturedContext);
		});
	}

	/** Broadcast 시 전달된 파라미터를 반환합니다 */
	FResultType GetResult()
	{
		return MoveTemp(Result);
	}

private:
	/** 대기 중인 델리게이트 */
	FDelegateType* Delegate;

	/** 바인딩 핸들 */
	FDelegateHandle Handle;

	/** Broadcast 시 전달된 결과 */
	FResultType Result;
};

//-----------------------------------------------------------------------------
// TDelegateAwaiter (파라미터 1개 특수화)
//-----------------------------------------------------------------------------

/**
 * TMulticastDelegate를 대기하는 Awaiter (파라미터 1개)
 *
 * 파라미터가 하나인 경우 튜플 대신 값을 직접 반환합니다.
 */
template<typename T>
class TDelegateAwaiter<T> : public TAsyncAwaiterBase<TDelegateAwaiter<T>>
{
	using Super = TAsyncAwaiterBase<TDelegateAwaiter<T>>;

public:
	using FDelegateType = TMulticastDelegate<void(T)>;
	using FResultType = std::decay_t<T>;

	TDelegateAwaiter(UObject* InOwner, FDelegateType& InDelegate)
		: Super(InOwner)
		, Delegate(&InDelegate)
	{
	}

	~TDelegateAwaiter()
	{
		if (Handle.IsValid())
		{
			Delegate->Remove(Handle);
		}
	}

	//-----------------------------------------------------------------------------
	// TAsyncAwaiterBase 메서드
	//-----------------------------------------------------------------------------

	/** 델리게이트에 바인딩하고 대기합니다 */
	void Suspend()
	{
		Handle = Delegate->AddLambda([CapturedOwner = this->Owner, CapturedContext = this->Context, DelegatePtr = Delegate, HandlePtr = &Handle, ResultPtr = &Result](T InArg)
		{
			// Owner가 유효하면 Awaiter도 유효합니다 (코루틴 프레임에 저장됨)
			if (CapturedOwner.IsValid())
			{
				*ResultPtr = Forward<T>(InArg);
				DelegatePtr->Remove(*HandlePtr);
				HandlePtr->Reset();
			}
			SafeResume(CapturedOwner, CapturedContext);
		});
	}

	/** Broadcast 시 전달된 파라미터를 반환합니다 */
	FResultType GetResult()
	{
		return MoveTemp(Result);
	}

private:
	/** 대기 중인 델리게이트 */
	FDelegateType* Delegate;

	/** 바인딩 핸들 */
	FDelegateHandle Handle;

	/** Broadcast 시 전달된 결과 */
	FResultType Result;
};

} // namespace Coro::Private

namespace Coro::Async
{

//-----------------------------------------------------------------------------
// 사용자 함수
//-----------------------------------------------------------------------------

/**
 * FSimpleMulticastDelegate가 Broadcast될 때까지 대기합니다
 *
 * @param Owner 코루틴을 소유한 UObject (파괴 시 안전하게 정리됨)
 * @param Delegate 대기할 델리게이트
 *
 * @code
 * TCoroTask<void> WaitForReadyCoroutine(FSimpleMulticastDelegate& OnReady)
 * {
 *     co_await Coro::Async::WaitForDelegate(this, OnReady);
 *     // OnReady가 Broadcast됨
 * }
 * @endcode
 */
inline Private::FSimpleDelegateAwaiter WaitForDelegate(UObject* Owner, FSimpleMulticastDelegate& Delegate)
{
	return Private::FSimpleDelegateAwaiter(Owner, Delegate);
}

/**
 * TMulticastDelegate가 Broadcast될 때까지 대기합니다
 *
 * @tparam Args 델리게이트 파라미터 타입들
 * @param Owner 코루틴을 소유한 UObject (파괴 시 안전하게 정리됨)
 * @param Delegate 대기할 델리게이트
 * @return Broadcast 시 전달된 파라미터 (1개면 값, 여러 개면 튜플)
 *
 * @code
 * TCoroTask<void> WaitForDamageCoroutine(TMulticastDelegate<void(float, AActor*)>& OnDamage)
 * {
 *     auto [Damage, Instigator] = co_await Coro::Async::WaitForDelegate(this, OnDamage);
 *     // Damage와 Instigator 사용
 * }
 *
 * TCoroTask<void> WaitForHealthCoroutine(TMulticastDelegate<void(float)>& OnHealthChanged)
 * {
 *     float NewHealth = co_await Coro::Async::WaitForDelegate(this, OnHealthChanged);
 *     // NewHealth 사용
 * }
 * @endcode
 */
template<typename... Args>
Private::TDelegateAwaiter<Args...> WaitForDelegate(UObject* Owner, TMulticastDelegate<void(Args...)>& Delegate)
{
	return Private::TDelegateAwaiter<Args...>(Owner, Delegate);
}

} // namespace Coro::Async