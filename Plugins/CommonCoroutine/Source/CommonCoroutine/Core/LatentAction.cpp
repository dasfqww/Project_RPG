// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/LatentAction.h"
#include "Core/Context.h"

namespace Coro::Private
{

FCoroLatentAction::FCoroLatentAction(ILatentPollable* InAwaiter, const FCoroContextPtr& InContext)
	: Awaiter(InAwaiter)
	, Context(InContext)
{
}

FCoroLatentAction::~FCoroLatentAction()
{
	// Destroyed 상태면 이미 NotifyObjectDestroyed에서 처리됨
	if (State == ECoroLatentActionState::Destroyed)
	{
		return;
	}

	// 코루틴이 정상 완료되지 않고 강제로 파괴되는 것입니다 (예: World 파괴, 레벨 전환)
	// Resume()을 호출하면 파괴 중인 UObject에 접근할 수 있어 코루틴 프레임을 직접 파괴합니다
	if (Context && !Context->IsDone())
	{
		Context->Destroy();
	}
}

void FCoroLatentAction::NotifyObjectDestroyed()
{
	// Owner가 파괴되면 코루틴 프레임을 직접 파괴합니다
	// Resume()을 호출하면 파괴 중인 UObject에 접근할 수 있어 코루틴 프레임을 직접 파괴합니다
	if (Context && !Context->IsDone())
	{
		Context->Destroy();
	}

	State = ECoroLatentActionState::Destroyed;
}

void FCoroLatentAction::NotifyActionAborted()
{
	// 액션이 중단되면 코루틴 프레임을 직접 파괴합니다
	if (Context && !Context->IsDone())
	{
		Context->Destroy();
	}

	State = ECoroLatentActionState::Destroyed;
}

void FCoroLatentAction::UpdateOperation(FLatentResponse& Response)
{
	// Owner가 파괴된 경우 즉시 종료 (Resume 호출 안함)
	if (State == ECoroLatentActionState::Destroyed)
	{
		Response.DoneIf(true);
		return;
	}

	// Awaiter가 없으면 대기할 것이 없으므로 즉시 종료
	if (State == ECoroLatentActionState::Running && !Awaiter)
	{
		Response.DoneIf(true);
		return;
	}

	// Context 취소 확인
	if (State == ECoroLatentActionState::Running && Context->IsCancelRequested())
	{
		if (Awaiter)
		{
			Awaiter->OnCancel();
			Awaiter = nullptr;
		}
		State = ECoroLatentActionState::Completing;
	}

	// 재개 조건 확인
	if (State == ECoroLatentActionState::Running && Awaiter->ShouldResume())
	{
		Awaiter = nullptr;
		State = ECoroLatentActionState::Completing;
	}

	// 완료 대기 상태면 Resume 호출 후 종료
	if (State == ECoroLatentActionState::Completing)
	{
		Context->Resume();
		Context = nullptr;
		Response.DoneIf(true);
	}
}

#if WITH_EDITOR
FString FCoroLatentAction::GetDescription() const
{
	return TEXT("Coroutine");
}
#endif

} // namespace Coro::Private