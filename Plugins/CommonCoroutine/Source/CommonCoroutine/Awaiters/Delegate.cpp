// Copyright Epic Games, Inc. All Rights Reserved.

#include "Awaiters/Delegate.h"

namespace Coro::Private
{

//-----------------------------------------------------------------------------
// FSimpleDelegateAwaiter
//-----------------------------------------------------------------------------

FSimpleDelegateAwaiter::FSimpleDelegateAwaiter(UObject* InOwner, FSimpleMulticastDelegate& InDelegate)
	: Super(InOwner)
	, Delegate(&InDelegate)
{
}

FSimpleDelegateAwaiter::~FSimpleDelegateAwaiter()
{
	if (Handle.IsValid())
	{
		Delegate->Remove(Handle);
	}
}

void FSimpleDelegateAwaiter::Suspend()
{
	Handle = Delegate->AddLambda([CapturedOwner = Owner, CapturedContext = Context, DelegatePtr = Delegate, HandlePtr = &Handle]()
	{
		// Owner가 유효하면 Awaiter도 유효합니다 (코루틴 프레임에 저장됨)
		if (CapturedOwner.IsValid())
		{
			DelegatePtr->Remove(*HandlePtr);
			HandlePtr->Reset();
		}
		SafeResume(CapturedOwner, CapturedContext);
	});
}

} // namespace Coro::Private
