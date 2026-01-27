// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Private.h"
#include "LatentActions.h"

namespace Coro::Private
{

//-----------------------------------------------------------------------------
// ECoroLatentActionState
//-----------------------------------------------------------------------------

/**
 * Latent Action 상태
 */
enum class ECoroLatentActionState : uint8
{
	/** 정상 실행 중 - Awaiter 폴링 */
	Running,

	/** 완료 대기 - Resume 호출 후 종료 */
	Completing,

	/** Owner 파괴됨 - Resume 없이 즉시 종료 */
	Destroyed,
};

//-----------------------------------------------------------------------------
// ILatentPollable
//-----------------------------------------------------------------------------

/**
 * Latent Awaiter 폴링 인터페이스
 *
 * FCoroLatentAction이 매 틱마다 이 인터페이스를 통해 재개 조건을 확인합니다.
 */
class ILatentPollable
{
public:
	virtual ~ILatentPollable() = default;

	/** 재개해야 하면 true를 반환합니다 */
	virtual bool ShouldResume() = 0;

	/** 취소 시 정리 작업을 수행합니다 */
	virtual void OnCancel() {}
};

//-----------------------------------------------------------------------------
// FCoroLatentAction
//-----------------------------------------------------------------------------

/**
 * 코루틴을 게임 스레드에서 틱하는 Latent Action
 *
 * FLatentActionManager에 등록되어 매 프레임 UpdateOperation()이 호출됩니다.
 * Context의 취소 상태를 확인하고, Awaiter의 ShouldResume()이 true를 반환하면 코루틴을 재개합니다.
 *
 * 동작 방식:
 * - Awaiter가 자신(ILatentPollable)과 Context를 등록
 * - 매 틱 UpdateOperation에서 취소 확인 후 ShouldResume() 호출
 * - 취소 시 OnCancel() 호출 후 종료
 * - ShouldResume() true 반환 시 Context->Resume() 호출
 */
class COMMONCOROUTINE_API FCoroLatentAction : public FPendingLatentAction
{
public:
	FCoroLatentAction(ILatentPollable* InAwaiter, const FCoroContextPtr& InContext);
	virtual ~FCoroLatentAction() override;

	//-----------------------------------------------------------------------------
	// FPendingLatentAction 오버라이드
	//-----------------------------------------------------------------------------

	/** 매 프레임 호출됩니다. 취소 확인 및 ShouldResume() 확인 후 조건 충족 시 재개합니다 */
	virtual void UpdateOperation(FLatentResponse& Response) override;

	/** Owner 오브젝트가 파괴될 때 호출됩니다 */
	virtual void NotifyObjectDestroyed() override;

	/** 액션이 중단될 때 호출됩니다 */
	virtual void NotifyActionAborted() override;

#if WITH_EDITOR
	/** 에디터에서 표시할 설명을 반환합니다 */
	virtual FString GetDescription() const override;
#endif

private:
	/** 현재 상태 */
	ECoroLatentActionState State = ECoroLatentActionState::Running;

	/** 폴링 대상 Awaiter */
	ILatentPollable* Awaiter;

	/** 코루틴 Context (취소 확인 및 재개용) */
	FCoroContextPtr Context;
};

} // namespace Coro::Private