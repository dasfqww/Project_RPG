// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Awaiter.h"

namespace Coro::Latent
{

//-----------------------------------------------------------------------------
// FSecondsAwaiter
//-----------------------------------------------------------------------------

/**
 * 지정된 시간(초) 동안 대기하는 Awaiter
 *
 * FPlatformTime::Seconds() 기준의 실제 시간을 사용합니다.
 */
class COMMONCOROUTINE_API FSecondsAwaiter : public Private::TLatentAwaiterBase<FSecondsAwaiter>
{
public:
	FSecondsAwaiter(UObject* InOwner, double InDuration);

	//-----------------------------------------------------------------------------
	// ILatentPollable 오버라이드
	//-----------------------------------------------------------------------------

	/** 목표 시간에 도달했는지 확인합니다 */
	virtual bool ShouldResume() override;

private:
	/** 목표 시간 (FPlatformTime::Seconds 기준) */
	double TargetTime;
};

//-----------------------------------------------------------------------------
// FFramesAwaiter
//-----------------------------------------------------------------------------

/**
 * 지정된 횟수의 프레임 동안 대기하는 Awaiter
 *
 * GFrameCounter 기준으로 프레임을 카운트합니다.
 */
class COMMONCOROUTINE_API FFramesAwaiter : public Private::TLatentAwaiterBase<FFramesAwaiter>
{
public:
	FFramesAwaiter(UObject* InOwner, int32 InCount);

	//-----------------------------------------------------------------------------
	// ILatentPollable 오버라이드
	//-----------------------------------------------------------------------------

	/** 목표 프레임에 도달했는지 확인합니다 */
	virtual bool ShouldResume() override;

private:
	/** 목표 프레임 번호 */
	uint64 TargetFrame;
};

//-----------------------------------------------------------------------------
// FUntilAwaiter
//-----------------------------------------------------------------------------

/**
 * 조건이 충족될 때까지 대기하는 Awaiter
 *
 * 매 틱마다 Predicate를 호출하여 true가 반환되면 재개합니다.
 */
class COMMONCOROUTINE_API FUntilAwaiter : public Private::TLatentAwaiterBase<FUntilAwaiter>
{
public:
	FUntilAwaiter(UObject* InOwner, TFunction<bool()> InPredicate);

	//-----------------------------------------------------------------------------
	// ILatentPollable 오버라이드
	//-----------------------------------------------------------------------------

	/** Predicate가 true를 반환하는지 확인합니다 */
	virtual bool ShouldResume() override;

private:
	/** 재개 조건 함수 */
	TFunction<bool()> Predicate;
};

//-----------------------------------------------------------------------------
// 사용자 함수
//-----------------------------------------------------------------------------

/**
 * 지정된 시간(초) 동안 대기합니다
 *
 * @param Owner Latent Action을 등록할 Owner
 * @param Duration 대기할 시간 (초)
 * @return Latent Awaiter
 *
 * @code
 * TCoroTask<void> MyCoroutine(UObject* Owner)
 * {
 *     co_await Coro::Latent::Seconds(Owner, 2.0);  // 2초 대기
 * }
 * @endcode
 */
inline FSecondsAwaiter Seconds(UObject* Owner, double Duration)
{
	return FSecondsAwaiter(Owner, Duration);
}

/**
 * 지정된 횟수의 프레임 동안 대기합니다
 *
 * @param Owner Latent Action을 등록할 Owner
 * @param Count 대기할 프레임 횟수
 * @return Latent Awaiter
 *
 * @code
 * TCoroTask<void> MyCoroutine(UObject* Owner)
 * {
 *     co_await Coro::Latent::Frames(Owner, 5);  // 5프레임 대기
 * }
 * @endcode
 */
inline FFramesAwaiter Frames(UObject* Owner, int32 Count)
{
	return FFramesAwaiter(Owner, Count);
}

/**
 * 다음 게임 틱까지 대기합니다
 *
 * @param Owner Latent Action을 등록할 Owner
 * @return Latent Awaiter
 *
 * @code
 * TCoroTask<void> MyCoroutine(UObject* Owner)
 * {
 *     co_await Coro::Latent::NextTick(Owner);  // 다음 틱까지 대기
 * }
 * @endcode
 */
inline FFramesAwaiter NextTick(UObject* Owner)
{
	return FFramesAwaiter(Owner, 1);
}

/**
 * 조건이 충족될 때까지 대기합니다
 *
 * 매 틱마다 Predicate를 호출하여 true가 반환되면 재개합니다.
 *
 * @param Owner Latent Action을 등록할 Owner
 * @param Predicate 재개 조건 함수 (true 반환 시 재개)
 * @return Latent Awaiter
 *
 * @code
 * TCoroTask<void> MyCoroutine(UObject* Owner)
 * {
 *     co_await Coro::Latent::Until(Owner, [this]() { return bIsReady; });
 * }
 * @endcode
 */
inline FUntilAwaiter Until(UObject* Owner, TFunction<bool()> Predicate)
{
	return FUntilAwaiter(Owner, MoveTemp(Predicate));
}

} // namespace Coro::Latent