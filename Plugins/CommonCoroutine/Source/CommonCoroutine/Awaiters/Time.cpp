// Copyright Epic Games, Inc. All Rights Reserved.

#include "Awaiters/Time.h"

namespace Coro::Latent
{

//-----------------------------------------------------------------------------
// FSecondsAwaiter
//-----------------------------------------------------------------------------

FSecondsAwaiter::FSecondsAwaiter(UObject* InOwner, double InDuration) : TLatentAwaiterBase(InOwner), TargetTime(FPlatformTime::Seconds() + InDuration)
{
}

bool FSecondsAwaiter::ShouldResume()
{
	return FPlatformTime::Seconds() >= TargetTime;
}

//-----------------------------------------------------------------------------
// FFramesAwaiter
//-----------------------------------------------------------------------------

FFramesAwaiter::FFramesAwaiter(UObject* InOwner, int32 InCount) : TLatentAwaiterBase(InOwner), TargetFrame(GFrameCounter + FMath::Max(InCount, 1))
{
}

bool FFramesAwaiter::ShouldResume()
{
	return GFrameCounter >= TargetFrame;
}

//-----------------------------------------------------------------------------
// FUntilAwaiter
//-----------------------------------------------------------------------------

FUntilAwaiter::FUntilAwaiter(UObject* InOwner, TFunction<bool()> InPredicate) : TLatentAwaiterBase(InOwner), Predicate(MoveTemp(InPredicate))
{
}

bool FUntilAwaiter::ShouldResume()
{
	return Predicate && Predicate();
}

} // namespace Coro::Latent