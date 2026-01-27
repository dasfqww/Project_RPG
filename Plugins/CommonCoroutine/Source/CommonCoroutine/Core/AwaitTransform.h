// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Core/Private.h"
#include "Core/Promise.h"
#include <coroutine>

namespace Coro::Private
{

//-----------------------------------------------------------------------------
// 타입 트레잇
//-----------------------------------------------------------------------------

/** TCoroTask인지 확인하는 트레잇 */
template<typename T>
struct TIsCoroTask : std::false_type {};

template<typename T>
struct TIsCoroTask<TCoroTask<T>> : std::true_type {};

//-----------------------------------------------------------------------------
// TCoroTaskAwaiter - Task 체이닝
//-----------------------------------------------------------------------------

/**
 * TCoroTask<T> 체이닝용 Awaiter
 *
 * 다른 코루틴(TCoroTask)을 co_await할 때 사용됩니다.
 * 취소 전파를 지원합니다 (부모 취소 시 자식도 취소).
 */
template<typename T>
class TCoroTaskAwaiter
{
public:
	explicit TCoroTaskAwaiter(const FPromise& InPromise, TCoroTask<T> InTask)
		: ParentContext(InPromise.GetContextShared())
		, Task(MoveTemp(InTask))
	{
	}

	/** 이미 완료됐으면 suspend하지 않습니다 */
	bool await_ready() const
	{
		return Task.IsDone();
	}

	/** 완료 콜백을 등록하고 suspend합니다 */
	void await_suspend(std::coroutine_handle<> CallerHandle)
	{
		// 부모 Context가 취소되면 자식 Task도 취소
		if (ParentContext)
		{
			ParentContext->AddCancellationCallback([Task = this->Task]() mutable
			{
				Task.Cancel();
			});
		}

		// 자식 Task 완료 시 부모 코루틴 재개
		Task.ContinueWith([ParentContext = this->ParentContext]()
		{
			if (ParentContext)
			{
				ParentContext->Resume();
			}
		});
	}

	/** 결과값을 반환합니다 (취소된 경우 기본값 반환) */
	auto await_resume()
	{
		if constexpr (std::is_void_v<T>)
		{
			// void 타입은 반환값 없음
		}
		else
		{
			// 취소된 경우 기본값 반환 (예: nullptr, 0 등)
			if (!Task.IsDone())
			{
				return T{};
			}

			return Task.GetResult();
		}
	}

private:
	/** 부모 Context (취소 전파용) */
	FCoroContextPtr ParentContext;

	/** 자식: co_await 대상 Task */
	TCoroTask<T> Task;
};

} // namespace Coro::Private

//-----------------------------------------------------------------------------
// FPromise::await_transform 구현
//-----------------------------------------------------------------------------

/**
 * co_await 표현식을 변환합니다
 *
 * - TCoroTask<T>: TCoroTaskAwaiter로 변환 (취소 전파 지원)
 * - 그 외: 그대로 통과
 */
template<typename T>
decltype(auto) Coro::Private::FPromise::await_transform(T&& Awaitable)
{
	using AwaitableType = std::decay_t<T>;

	// TCoroTask<U> 체이닝 - 취소 전파 지원
	if constexpr (TIsCoroTask<AwaitableType>::value)
	{
		using ResultType = typename AwaitableType::FResultType;
		return TCoroTaskAwaiter<ResultType>(*this, std::forward<T>(Awaitable));
	}
	// 그 외는 그대로 통과
	else
	{
		return std::forward<T>(Awaitable);
	}
}