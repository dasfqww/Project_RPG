// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Private.h"
#include "HAL/Event.h"
#include <atomic>

namespace Coro::Private
{

//-----------------------------------------------------------------------------
// ECoroState
//-----------------------------------------------------------------------------

/**
 * 코루틴 상태
 */
enum class ECoroState : uint8
{
	/** 대기 중 (시작 전 또는 co_await 중) */
	Pending,

	/** 실행 중 */
	Running,

	/** 정상 완료 (co_return 도달) */
	Completed,

	/** 취소됨 (Cancel() 호출 또는 Owner 파괴) */
	Canceled
};

//-----------------------------------------------------------------------------
// FCoroContext
//-----------------------------------------------------------------------------

/**
 * 코루틴 공유 상태 (Context)
 *
 * Task와 Promise가 shared_ptr로 공유하며, 코루틴 완료 후에도
 * 결과 접근이 가능하도록 합니다.
 *
 * 역할:
 * - 완료/취소 상태 관리 및 추적
 * - 취소 요청 처리 및 자식 코루틴으로 취소 전파
 * - 완료 대기 (Wait)
 * - 완료/취소 콜백 관리
 * - 스레드 안전한 상태 접근
 *
 * 메모리 구조:
 *     [TCoroTask] ──shared_ptr──► [FCoroContext] ◄──shared_ptr── [Promise]
 *                                      │
 *                                      └─► Promise* (실행 중) 또는 nullptr (완료 후)
 */
struct COMMONCOROUTINE_API FCoroContext
{
public:
	FCoroContext();
	virtual ~FCoroContext() = default;

	FCoroContext(const FCoroContext&) = delete;
	FCoroContext& operator=(const FCoroContext&) = delete;

	//-----------------------------------------------------------------------------
	// 상태 조회
	//-----------------------------------------------------------------------------

	/** 완료되었는지 확인합니다 (정상 완료 또는 취소) */
	bool IsDone() const;

	/** 성공적으로 완료되었는지 확인합니다 (취소/예외 아님) */
	bool WasSuccessful() const;

	/** 현재 상태를 반환합니다 */
	ECoroState GetState() const;

	//-----------------------------------------------------------------------------
	// 완료/취소
	//-----------------------------------------------------------------------------

	/** 완료 상태로 설정합니다 */
	void MarkCompleted(bool bSuccess);

	/** 완료 콜백을 등록합니다 */
	void AddCompletionCallback(TFunction<void()> Callback);

	/** 코루틴 취소를 요청합니다 (등록된 취소 콜백도 실행) */
	void Cancel();

	/** 취소가 요청되었는지 확인합니다 */
	bool IsCancelRequested() const;

	/** 취소 시 호출될 콜백을 등록합니다 (자식 Task 취소 전파용) */
	void AddCancellationCallback(TFunction<void()> Callback);

	//-----------------------------------------------------------------------------
	// 대기
	//-----------------------------------------------------------------------------

	/**
	 * 코루틴 완료까지 현재 스레드를 블록합니다
	 *
	 * @param TimeoutMs 최대 대기 시간 (밀리초). MAX_uint32면 무한 대기
	 * @return 완료되면 true, 타임아웃이면 false
	 */
	bool Wait(uint32 TimeoutMs = MAX_uint32) const;

	//-----------------------------------------------------------------------------
	// 코루틴 제어
	//-----------------------------------------------------------------------------

	/** 코루틴을 재개합니다 */
	void Resume() const;

	/** 코루틴을 파괴합니다 (Resume 없이 즉시 정리) */
	void Destroy();

	//-----------------------------------------------------------------------------
	// 내부용 (Promise에서 호출)
	//-----------------------------------------------------------------------------

	/** Promise 포인터를 설정합니다 */
	void SetPromise(FPromise* InPromise);

	/** Promise 포인터를 반환합니다 (완료 후에는 nullptr) */
	FPromise* GetPromise() const;

protected:
	/** 완료 이벤트 (Wait에서 사용) */
	FEventRef CompletedEvent;

	/** 성공 완료 여부 */
	std::atomic<bool> bWasSuccessful{false};

	/** 취소 요청 여부 */
	std::atomic<bool> bCancelRequested{false};

	/** 현재 상태 */
	std::atomic<ECoroState> State{ECoroState::Pending};

	/** 동기화용 Lock */
	mutable FCriticalSection Lock;

	/** Promise 포인터 (실행 중에만 유효) */
	FPromise* Promise = nullptr;

	/** 완료 콜백 목록 */
	TArray<TFunction<void()>> CompletionCallbacks;

	/** 취소 콜백 목록 */
	TArray<TFunction<void()>> CancellationCallbacks;
};

//-----------------------------------------------------------------------------
// TCoroContext<T>
//-----------------------------------------------------------------------------

/**
 * 결과값을 저장하는 코루틴 Context
 *
 * TCoroTask<T>에서 사용되며, co_return value의 결과를 저장합니다.
 */
template<typename T>
struct TCoroContext : public FCoroContext
{
public:
	/** 결과값을 설정합니다 (Promise에서 호출) */
	void SetResult(T&& Value)
	{
		Result = MoveTemp(Value);
	}

	/** 결과값을 반환합니다 (완료 후에만 유효) */
	const T& GetResult() const
	{
		return Result;
	}

	/** 결과값을 이동합니다 (완료 후에만 유효) */
	T&& MoveResult()
	{
		return MoveTemp(Result);
	}

private:
	/** co_return으로 반환된 결과값 */
	T Result{};
};

//-----------------------------------------------------------------------------
// TCoroContext<void> 특수화
//-----------------------------------------------------------------------------

/**
 * void 타입 특수화 (결과값 없음)
 */
template<>
struct TCoroContext<void> : public FCoroContext
{
};

} // namespace Coro::Private