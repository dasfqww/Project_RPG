// Copyright Epic Games, Inc. All Rights Reserved.

#include "Awaiters/Thread.h"
#include "Async/TaskGraphInterfaces.h"

namespace Coro::Private
{

//-----------------------------------------------------------------------------
// FThreadAwaiter
//-----------------------------------------------------------------------------

FThreadAwaiter::FThreadAwaiter(UObject* InOwner, ENamedThreads::Type InTargetThread)
	: Super(InOwner)
	, TargetThread(InTargetThread)
{
}

bool FThreadAwaiter::Ready() const
{
	const ENamedThreads::Type CurrentThread = FTaskGraphInterface::Get().GetCurrentThreadIfKnown();

	// 현재 스레드를 알 수 없으면 suspend (안전하게)
	if (CurrentThread == ENamedThreads::AnyThread)
	{
		return false;
	}

	// GameThread 체크 시 특별 처리 (IsInGameThread() 사용)
	if ((TargetThread & ENamedThreads::ThreadIndexMask) == ENamedThreads::GameThread)
	{
		return IsInGameThread();
	}

	return (CurrentThread & ENamedThreads::ThreadIndexMask) == (TargetThread & ENamedThreads::ThreadIndexMask);
}

void FThreadAwaiter::Suspend()
{
	// Owner와 Context를 캡처하여 대상 스레드에서 재개
	AsyncTask(TargetThread, [CapturedOwner = Owner, CapturedContext = Context]()
	{
		SafeResume(CapturedOwner, CapturedContext);
	});
}

//-----------------------------------------------------------------------------
// FTaskAwaiter
//-----------------------------------------------------------------------------

FTaskAwaiter::FTaskAwaiter(UObject* InOwner) : Super(InOwner)
{
}

void FTaskAwaiter::Suspend()
{
	// Owner와 Context를 캡처하여 Task Graph에서 재개
	FFunctionGraphTask::CreateAndDispatchWhenReady(
		[CapturedOwner = Owner, CapturedContext = Context]()
		{
			SafeResume(CapturedOwner, CapturedContext);
		},
		TStatId(),
		nullptr,
		ENamedThreads::AnyBackgroundThreadNormalTask
	);
}

} // namespace Coro::Private