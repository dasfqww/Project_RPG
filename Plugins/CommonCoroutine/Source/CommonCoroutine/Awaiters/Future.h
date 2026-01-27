// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Awaiter.h"
#include "Async/Future.h"

namespace Coro::Private
{

//-----------------------------------------------------------------------------
// TFutureAwaiter
//-----------------------------------------------------------------------------

/**
 * TFuture를 대기하는 Awaiter
 *
 * TFuture가 완료될 때까지 논블로킹으로 대기하고,
 * 완료 시 결과값을 반환합니다.
 */
template<typename T>
class TFutureAwaiter : public TAsyncAwaiterBase<TFutureAwaiter<T>>
{
	using Super = TAsyncAwaiterBase<TFutureAwaiter<T>>;

public:
	TFutureAwaiter(UObject* InOwner, TFuture<T>&& InFuture)
		: Super(InOwner)
		, Future(MoveTemp(InFuture))
	{
	}

	//-----------------------------------------------------------------------------
	// TAsyncAwaiterBase 메서드
	//-----------------------------------------------------------------------------

	/** 이미 완료되었으면 suspend하지 않습니다 */
	bool Ready()
	{
		return Future.IsReady();
	}

	/** Future 완료 시 코루틴을 재개합니다 */
	void Suspend()
	{
		Future.Then([CapturedOwner = this->Owner, CapturedContext = this->Context](TFuture<T>)
		{
			SafeResume(CapturedOwner, CapturedContext);
		});
	}

	/** Future 결과를 반환합니다 */
	T GetResult()
	{
		return Future.Get();
	}

private:
	/** 대기 중인 Future */
	TFuture<T> Future;
};

} // namespace Coro::Private

namespace Coro::Async
{

//-----------------------------------------------------------------------------
// 사용자 함수
//-----------------------------------------------------------------------------

/**
 * TFuture가 완료될 때까지 대기합니다
 *
 * @tparam T Future 결과 타입
 * @param Owner 코루틴을 소유한 UObject (파괴 시 안전하게 정리됨)
 * @param Future 대기할 Future (move 필수)
 * @return Future 결과값
 *
 * @code
 * TCoroTask<void> ProcessDataCoroutine()
 * {
 *     TFuture<int> Future = Async(EAsyncExecution::ThreadPool, []()
 *     {
 *         // 무거운 계산
 *         return 42;
 *     });
 *
 *     int Result = co_await Coro::Async::WaitForFuture(this, MoveTemp(Future));
 *     // Result == 42
 * }
 * @endcode
 */
template<typename T>
Private::TFutureAwaiter<T> WaitForFuture(UObject* Owner, TFuture<T>&& Future)
{
	return Private::TFutureAwaiter<T>(Owner, MoveTemp(Future));
}

} // namespace Coro::Async