// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Awaiter.h"
#include "Async/Async.h"

namespace Coro::Private
{

//-----------------------------------------------------------------------------
// FThreadAwaiter
//-----------------------------------------------------------------------------

/**
 * 지정된 스레드로 실행을 전환하는 Awaiter
 *
 * await_suspend에서 대상 스레드에 재개를 예약합니다.
 */
class COMMONCOROUTINE_API FThreadAwaiter : public TAsyncAwaiterBase<FThreadAwaiter>
{
	using Super = TAsyncAwaiterBase<FThreadAwaiter>;

public:
	FThreadAwaiter(UObject* InOwner, ENamedThreads::Type InTargetThread);

	//-----------------------------------------------------------------------------
	// TAsyncAwaiterBase 메서드
	//-----------------------------------------------------------------------------

	/** 이미 대상 스레드에 있으면 true를 반환합니다 */
	bool Ready() const;

	/** 대상 스레드에서 코루틴을 재개합니다 */
	void Suspend();

private:
	/** 대상 스레드 */
	ENamedThreads::Type TargetThread;
};

//-----------------------------------------------------------------------------
// FTaskAwaiter
//-----------------------------------------------------------------------------

/**
 * Task Graph를 통해 백그라운드 스레드로 전환하는 Awaiter
 *
 * UE의 Task Graph 시스템을 사용하여 효율적인 스레드 풀 활용을 합니다.
 */
class COMMONCOROUTINE_API FTaskAwaiter : public TAsyncAwaiterBase<FTaskAwaiter>
{
	using Super = TAsyncAwaiterBase<FTaskAwaiter>;

public:
	explicit FTaskAwaiter(UObject* InOwner);

	//-----------------------------------------------------------------------------
	// TAsyncAwaiterBase 메서드
	//-----------------------------------------------------------------------------

	/** Task Graph를 통해 백그라운드에서 재개합니다 */
	void Suspend();
};

} // namespace Coro::Private

namespace Coro::Async
{

//-----------------------------------------------------------------------------
// 사용자 함수
//-----------------------------------------------------------------------------

/**
 * 지정된 Named Thread로 실행을 전환합니다
 *
 * @param Owner 코루틴을 소유한 UObject (파괴 시 안전하게 정리됨)
 * @param Thread 대상 스레드 (ENamedThreads::Type)
 * @return Thread Awaiter
 *
 * @code
 * TCoroTask<void> MyCoroutine()
 * {
 *     co_await Coro::Async::MoveToThread(this, ENamedThreads::GameThread);
 * }
 * @endcode
 */
inline Private::FThreadAwaiter MoveToThread(UObject* Owner, ENamedThreads::Type Thread)
{
	return Private::FThreadAwaiter(Owner, Thread);
}

/**
 * 게임 스레드로 실행을 전환합니다
 *
 * @param Owner 코루틴을 소유한 UObject (파괴 시 안전하게 정리됨)
 * @return Thread Awaiter
 *
 * @code
 * TCoroTask<void> MyCoroutine()
 * {
 *     // 백그라운드 작업 후
 *     co_await Coro::Async::MoveToGameThread(this);
 *     // 이제 게임 스레드에서 실행
 * }
 * @endcode
 */
inline Private::FThreadAwaiter MoveToGameThread(UObject* Owner)
{
	return Private::FThreadAwaiter(Owner, ENamedThreads::GameThread);
}

/**
 * Task Graph를 통해 백그라운드 스레드로 전환합니다
 *
 * UE의 Task Graph 시스템을 사용하여 효율적으로 스레드 풀을 활용합니다.
 *
 * @param Owner 코루틴을 소유한 UObject (파괴 시 안전하게 정리됨)
 * @return Task Awaiter
 *
 * @code
 * TCoroTask<void> MyCoroutine()
 * {
 *     co_await Coro::Async::MoveToTask(this);
 *     // 이제 백그라운드 스레드에서 실행
 *     DoHeavyWork();
 * }
 * @endcode
 */
inline Private::FTaskAwaiter MoveToTask(UObject* Owner)
{
	return Private::FTaskAwaiter(Owner);
}

} // namespace Coro::Async