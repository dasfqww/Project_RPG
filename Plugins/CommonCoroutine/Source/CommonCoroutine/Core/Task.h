// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Private.h"
#include "Core/Context.h"

//-----------------------------------------------------------------------------
// TCoroTask<void> 특수화
//-----------------------------------------------------------------------------

/**
 * 코루틴 Task 핸들
 *
 * 코루틴 함수의 반환 타입으로 사용되며, FCoroContext를 shared_ptr로 참조합니다.
 * 복사 가능하고 코루틴 완료 후에도 결과에 접근할 수 있습니다.
 */
template<>
class TCoroTask<void>
{
public:
	/** 결과 타입 (await_transform에서 사용) */
	using FResultType = void;

	TCoroTask() = default;

	explicit TCoroTask(FCoroContextPtr InContext)
		: Context(MoveTemp(InContext))
	{
	}

	/** 복사 가능 */
	TCoroTask(const TCoroTask&) = default;
	TCoroTask& operator=(const TCoroTask&) = default;

	/** 이동 가능 */
	TCoroTask(TCoroTask&&) = default;
	TCoroTask& operator=(TCoroTask&&) = default;

	//-----------------------------------------------------------------------------
	// 상태 조회
	//-----------------------------------------------------------------------------

	/** Task가 유효한지 확인합니다 */
	bool IsValid() const { return Context != nullptr; }

	/** 코루틴이 완료되었는지 확인합니다 (정상 완료 또는 취소) */
	bool IsDone() const { return Context && Context->IsDone(); }

	/** 코루틴이 성공적으로 완료되었는지 확인합니다 */
	bool WasSuccessful() const { return Context && Context->WasSuccessful(); }

	//-----------------------------------------------------------------------------
	// 제어
	//-----------------------------------------------------------------------------

	/** 코루틴 취소를 요청합니다 */
	void Cancel() const
	{
		if (Context)
		{
			Context->Cancel();
		}
	}

	/**
	 * 코루틴 완료까지 현재 스레드를 블록합니다
	 *
	 * @param TimeoutMs 최대 대기 시간 (밀리초). MAX_uint32면 무한 대기
	 * @return 완료되면 true, 타임아웃이면 false
	 */
	bool Wait(uint32 TimeoutMs = MAX_uint32) const
	{
		return Context && Context->Wait(TimeoutMs);
	}

	//-----------------------------------------------------------------------------
	// 완료 콜백
	//-----------------------------------------------------------------------------

	/** 완료 시 콜백을 실행합니다 */
	void ContinueWith(TFunction<void()> Callback) const
	{
		if (Context)
		{
			Context->AddCompletionCallback(MoveTemp(Callback));
		}
	}

protected:
	/** Context 접근 (파생 클래스용) */
	Coro::Private::FCoroContext* GetContext() const { return Context.Get(); }

	/** Context 공유 포인터 접근 (파생 클래스용) */
	template<typename T>
	TCoroContextPtr<T> GetSharedContext() const
	{
		return StaticCastSharedPtr<Coro::Private::TCoroContext<T>>(Context);
	}

protected:
	/** 공유 상태 */
	FCoroContextPtr Context;
};

//-----------------------------------------------------------------------------
// TCoroTask<T>
//-----------------------------------------------------------------------------

/**
 * 결과값을 반환하는 코루틴 Task
 *
 * TCoroTask<void>를 상속하며, 결과값 조회 기능을 추가합니다.
 */
template<typename T>
class TCoroTask : public TCoroTask<void>
{
public:
	/** 결과 타입 (await_transform에서 사용) */
	using FResultType = T;

	/** 부모 클래스의 ContinueWith도 사용 가능하게 합니다 */
	using TCoroTask<void>::ContinueWith;

	TCoroTask() = default;

	explicit TCoroTask(TCoroContextPtr<T> InContext)
		: TCoroTask<void>(InContext)
	{
	}

	//-----------------------------------------------------------------------------
	// 결과값 접근
	//-----------------------------------------------------------------------------

	/** 결과값을 반환합니다 (완료 후에만 유효) */
	const T& GetResult() const
	{
		check(IsDone());
		return this->template GetSharedContext<T>()->GetResult();
	}

	/** 결과값을 이동합니다 (완료 후에만 유효, 한 번만 호출 가능) */
	T&& MoveResult()
	{
		check(IsDone());
		return this->template GetSharedContext<T>()->MoveResult();
	}

	//-----------------------------------------------------------------------------
	// 완료 콜백 (결과값 포함)
	//-----------------------------------------------------------------------------

	/** 완료 시 결과값과 함께 콜백을 실행합니다 */
	void ContinueWith(TFunction<void(const T&)> Callback) const
	{
		if (Context)
		{
			// TWeakPtr로 캡처하여 순환 참조 방지
			TCoroContextWeakPtr<T> WeakContext = this->template GetSharedContext<T>();

			Context->AddCompletionCallback([WeakContext, Callback = MoveTemp(Callback)]()
			{
				if (TCoroContextPtr<T> StrongContext = WeakContext.Pin())
				{
					Callback(StrongContext->GetResult());
				}
			});
		}
	}
};