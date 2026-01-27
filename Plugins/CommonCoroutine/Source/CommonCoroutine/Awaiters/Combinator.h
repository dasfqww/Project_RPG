// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Task.h"
#include "Core/Awaiter.h"
#include <atomic>

namespace Coro::Private
{

//-----------------------------------------------------------------------------
// FWhenAllAwaiter
//-----------------------------------------------------------------------------

/**
 * 모든 Task가 완료될 때까지 대기하는 Awaiter
 *
 * 여러 Task를 병렬로 실행하고 모두 완료될 때까지 대기합니다.
 * atomic 카운터로 완료된 Task 수를 추적합니다.
 */
class COMMONCOROUTINE_API FWhenAllAwaiter : public TAsyncAwaiterBase<FWhenAllAwaiter>
{
	using Super = TAsyncAwaiterBase<FWhenAllAwaiter>;

public:
	FWhenAllAwaiter(UObject* InOwner, TArray<TCoroTask<void>> InTasks);

	//-----------------------------------------------------------------------------
	// TAsyncAwaiterBase 메서드
	//-----------------------------------------------------------------------------

	/** 모든 Task가 이미 완료됐으면 suspend하지 않습니다 */
	bool Ready() const;

	/** 모든 Task 완료 시 재개합니다 */
	void Suspend();

private:
	/** 대기 중인 Task 목록 */
	TArray<TCoroTask<void>> Tasks;

	/** 남은 Task 수 */
	std::atomic<int32> RemainingCount{0};
};

//-----------------------------------------------------------------------------
// FWhenAnyAwaiter
//-----------------------------------------------------------------------------

/**
 * 하나의 Task라도 완료되면 대기를 종료하는 Awaiter
 *
 * 여러 Task 중 가장 먼저 완료되는 것을 대기합니다.
 * CAS 연산으로 첫 번째 완료만 처리합니다.
 */
class COMMONCOROUTINE_API FWhenAnyAwaiter : public TAsyncAwaiterBase<FWhenAnyAwaiter>
{
	using Super = TAsyncAwaiterBase<FWhenAnyAwaiter>;

public:
	FWhenAnyAwaiter(UObject* InOwner, TArray<TCoroTask<void>> InTasks);

	//-----------------------------------------------------------------------------
	// TAsyncAwaiterBase 메서드
	//-----------------------------------------------------------------------------

	/** 이미 완료된 Task가 있으면 suspend하지 않습니다 */
	bool Ready();

	/** 첫 번째 Task 완료 시 재개합니다 */
	void Suspend();

	/** 완료된 Task의 인덱스를 반환합니다 */
	int32 GetResult() const;

private:
	/** 대기 중인 Task 목록 */
	TArray<TCoroTask<void>> Tasks;

	/** 이미 재개되었는지 여부 */
	std::atomic<bool> bResumed{false};

	/** 완료된 Task 인덱스 */
	int32 CompletedIndex = INDEX_NONE;
};

} // namespace Coro::Private

namespace Coro::Async
{

//-----------------------------------------------------------------------------
// 사용자 함수
//-----------------------------------------------------------------------------

/**
 * 모든 Task가 완료될 때까지 대기합니다
 *
 * @param Owner 코루틴을 소유한 UObject (파괴 시 안전하게 정리됨)
 * @param Tasks 대기할 Task 배열
 * @return Awaiter
 *
 * @code
 * TCoroTask<void> MyCoroutine()
 * {
 *     TCoroTask<void> Task1 = DoSomething1();
 *     TCoroTask<void> Task2 = DoSomething2();
 *
 *     co_await Coro::Async::WhenAll(this, { Task1, Task2 });
 *     // 모두 완료됨
 * }
 * @endcode
 */
inline Private::FWhenAllAwaiter WhenAll(UObject* Owner, TArray<TCoroTask<void>> Tasks)
{
	return Private::FWhenAllAwaiter(Owner, MoveTemp(Tasks));
}

/**
 * 하나의 Task라도 완료되면 대기를 종료합니다
 *
 * @param Owner 코루틴을 소유한 UObject (파괴 시 안전하게 정리됨)
 * @param Tasks 대기할 Task 배열
 * @return 완료된 Task의 인덱스
 *
 * @code
 * TCoroTask<void> MyCoroutine()
 * {
 *     TCoroTask<void> Task1 = DoSomething1();
 *     TCoroTask<void> Task2 = DoSomething2();
 *
 *     int32 CompletedIndex = co_await Coro::Async::WhenAny(this, { Task1, Task2 });
 *     // CompletedIndex 번째 Task가 먼저 완료됨
 * }
 * @endcode
 */
inline Private::FWhenAnyAwaiter WhenAny(UObject* Owner, TArray<TCoroTask<void>> Tasks)
{
	return Private::FWhenAnyAwaiter(Owner, MoveTemp(Tasks));
}

} // namespace Coro::Async