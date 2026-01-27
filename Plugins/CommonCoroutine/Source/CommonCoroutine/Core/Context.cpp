// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/Context.h"
#include "Core/Promise.h"

namespace Coro::Private
{

FCoroContext::FCoroContext() : CompletedEvent(EEventMode::ManualReset)
{
}

bool FCoroContext::IsDone() const
{
	ECoroState CurrentState = State.load(std::memory_order_acquire);
	return CurrentState == ECoroState::Completed || CurrentState == ECoroState::Canceled;
}

bool FCoroContext::WasSuccessful() const
{
	return bWasSuccessful.load(std::memory_order_acquire);
}

ECoroState FCoroContext::GetState() const
{
	return State.load(std::memory_order_acquire);
}

void FCoroContext::Cancel()
{
	TArray<TFunction<void()>> CallbacksToInvoke;

	{
		FScopeLock ScopeLock(&Lock);

		// 이미 취소 요청됐으면 무시
		if (bCancelRequested.load(std::memory_order_acquire))
		{
			return;
		}

		bCancelRequested.store(true, std::memory_order_release);

		// 취소 콜백 복사 후 정리
		CallbacksToInvoke = MoveTemp(CancellationCallbacks);
	}

	// 취소 콜백 실행 (Lock 밖에서) - 자식 Task 취소 등
	for (const TFunction<void()>& Callback : CallbacksToInvoke)
	{
		Callback();
	}
}

bool FCoroContext::IsCancelRequested() const
{
	return bCancelRequested.load(std::memory_order_acquire);
}

void FCoroContext::AddCancellationCallback(TFunction<void()> Callback)
{
	bool bShouldInvokeImmediately = false;

	{
		FScopeLock ScopeLock(&Lock);

		// 이미 취소 요청됐으면 즉시 실행
		if (bCancelRequested.load(std::memory_order_acquire))
		{
			bShouldInvokeImmediately = true;
		}
		else
		{
			CancellationCallbacks.Add(MoveTemp(Callback));
		}
	}

	if (bShouldInvokeImmediately)
	{
		Callback();
	}
}

bool FCoroContext::Wait(uint32 TimeoutMs) const
{
	if (IsDone())
	{
		return true;
	}

	return CompletedEvent->Wait(TimeoutMs);
}

void FCoroContext::MarkCompleted(bool bSuccess)
{
	TArray<TFunction<void()>> CallbacksToInvoke;

	{
		FScopeLock ScopeLock(&Lock);

		bWasSuccessful.store(bSuccess, std::memory_order_release);
		State.store(bSuccess ? ECoroState::Completed : ECoroState::Canceled, std::memory_order_release);

		// Promise 포인터 정리
		Promise = nullptr;

		// 콜백 목록 복사 후 정리
		CallbacksToInvoke = MoveTemp(CompletionCallbacks);
	}

	// 콜백 실행 (Lock 밖에서)
	for (const TFunction<void()>& Callback : CallbacksToInvoke)
	{
		Callback();
	}

	// 이벤트 시그널 (Wait 해제)
	CompletedEvent->Trigger();
}

void FCoroContext::SetPromise(FPromise* InPromise)
{
	FScopeLock ScopeLock(&Lock);
	Promise = InPromise;
}

FPromise* FCoroContext::GetPromise() const
{
	FScopeLock ScopeLock(&Lock);
	return Promise;
}

void FCoroContext::Resume() const
{
	// 이미 완료/취소된 코루틴은 Resume하지 않음
	if (IsDone())
	{
		return;
	}

	if (Promise)
	{
		Promise->Resume();
	}
}

void FCoroContext::Destroy()
{
	// 이미 완료/취소된 코루틴은 파괴하지 않음
	if (IsDone())
	{
		return;
	}

	if (Promise)
	{
		Promise->DestroyCoroutine();
	}

	bCancelRequested.store(true, std::memory_order_release);
	State.store(ECoroState::Canceled, std::memory_order_release);
	CompletedEvent->Trigger();
}

void FCoroContext::AddCompletionCallback(TFunction<void()> Callback)
{
	bool bShouldInvokeImmediately = false;

	{
		FScopeLock ScopeLock(&Lock);

		if (IsDone())
		{
			// 이미 완료됨 - Lock 밖에서 즉시 실행
			bShouldInvokeImmediately = true;
		}
		else
		{
			CompletionCallbacks.Add(MoveTemp(Callback));
		}
	}

	if (bShouldInvokeImmediately)
	{
		Callback();
	}
}

} // namespace Coro::Private