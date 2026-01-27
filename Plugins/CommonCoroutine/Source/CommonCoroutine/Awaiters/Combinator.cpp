// Copyright Epic Games, Inc. All Rights Reserved.

#include "Awaiters/Combinator.h"

namespace Coro::Private
{

//-----------------------------------------------------------------------------
// FWhenAllAwaiter
//-----------------------------------------------------------------------------

FWhenAllAwaiter::FWhenAllAwaiter(UObject* InOwner, TArray<TCoroTask<void>> InTasks)
	: Super(InOwner)
	, Tasks(MoveTemp(InTasks))
{
}

bool FWhenAllAwaiter::Ready() const
{
	for (const auto& Task : Tasks)
	{
		if (!Task.IsDone())
		{
			return false;
		}
	}
	return true;
}

void FWhenAllAwaiter::Suspend()
{
	RemainingCount.store(Tasks.Num());

	for (auto& Task : Tasks)
	{
		Task.ContinueWith([CapturedOwner = Owner, CapturedContext = Context, Counter = &RemainingCount]()
		{
			// 마지막 Task가 완료되면 재개
			if (Counter->fetch_sub(1) == 1)
			{
				SafeResume(CapturedOwner, CapturedContext);
			}
		});
	}
}

//-----------------------------------------------------------------------------
// FWhenAnyAwaiter
//-----------------------------------------------------------------------------

FWhenAnyAwaiter::FWhenAnyAwaiter(UObject* InOwner, TArray<TCoroTask<void>> InTasks)
	: Super(InOwner)
	, Tasks(MoveTemp(InTasks))
{
}

bool FWhenAnyAwaiter::Ready()
{
	for (int32 i = 0; i < Tasks.Num(); ++i)
	{
		if (Tasks[i].IsDone())
		{
			CompletedIndex = i;
			return true;
		}
	}
	return false;
}

void FWhenAnyAwaiter::Suspend()
{
	bResumed.store(false);

	for (int32 i = 0; i < Tasks.Num(); ++i)
	{
		Tasks[i].ContinueWith([CapturedOwner = Owner, CapturedContext = Context, Resumed = &bResumed, Index = i, ResultIndex = &CompletedIndex]()
		{
			// 첫 번째 완료만 처리
			bool bExpected = false;
			if (Resumed->compare_exchange_strong(bExpected, true))
			{
				*ResultIndex = Index;
				SafeResume(CapturedOwner, CapturedContext);
			}
		});
	}
}

int32 FWhenAnyAwaiter::GetResult() const
{
	return CompletedIndex;
}

} // namespace Coro::Private
