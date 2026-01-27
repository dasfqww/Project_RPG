// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Private.h"
#include "Core/Context.h"
#include "Core/Task.h"
#include <coroutine>

namespace Coro::Private
{

/**
 * 코루틴 Promise 기본 클래스
 *
 * 모든 코루틴 Promise의 공통 기반입니다.
 *
 * 역할:
 * - Context 참조 보유
 * - await_transform을 통한 co_await 변환 (구현은 CoroAwaitTransform.h)
 * - 코루틴 핸들 관리
 *
 * 생성 흐름 (파생 클래스 TPromise의 get_return_object에서):
 * 1. Context 생성 (TCoroContext<T>)
 * 2. Context ↔ Promise 연결
 * 3. Task 생성 및 반환 (Context를 shared_ptr로 공유)
 */
class COMMONCOROUTINE_API FPromise
{
public:
	FPromise() = default;

	virtual ~FPromise()
	{
		// Context의 Promise 포인터를 정리하여 dangling pointer 방지
		if (Context)
		{
			Context->SetPromise(nullptr);
		}
	}

	FPromise(const FPromise&) = delete;
	FPromise& operator=(const FPromise&) = delete;

	//-----------------------------------------------------------------------------
	// 코루틴 인터페이스 (공통)
	//-----------------------------------------------------------------------------

	/** 시작 시 suspend하지 않음 (즉시 실행) */
	std::suspend_never initial_suspend() const noexcept { return {}; }

	/** 종료 시 suspend하지 않음 (자동 정리) */
	std::suspend_never final_suspend() const noexcept { return {}; }

	/** 예외 처리 - 언리얼에서는 예외를 사용하지 않음 */
	void unhandled_exception() const noexcept
	{
		// 언리얼 엔진은 예외를 사용하지 않습니다
		checkNoEntry();
	}

	/**
	 * 코루틴 내에서 co_await X를 만날 때마다 호출자(현재 코루틴)의 Promise에서 호출됩니다.
	 * T는 co_await 뒤에 오는 표현식의 타입입니다.
	 *
	 * 예시: A 코루틴 내부에서
	 *     co_await B();                 // PromiseA.await_transform(TCoroTask<int>) 호출
	 *     co_await FDelayAwaiter(1.0f); // PromiseA.await_transform(FDelayAwaiter) 호출
	 *
	 * 구현은 CoroAwaitTransform.h에 있습니다 (순환 참조 방지)
	 */
	template<typename T>
	decltype(auto) await_transform(T&& Awaitable);

	//-----------------------------------------------------------------------------
	// Context 접근
	//-----------------------------------------------------------------------------

	/** Context를 반환합니다 (raw pointer) */
	FCoroContext* GetContext() const { return Context.Get(); }

	/** Context를 반환합니다 (shared_ptr) */
	FCoroContextPtr GetContextShared() const { return Context; }

	//-----------------------------------------------------------------------------
	// 코루틴 제어
	//-----------------------------------------------------------------------------

	/** 코루틴을 재개합니다 */
	void Resume() const { CoroutineHandle.resume(); }

	/** 코루틴을 파괴합니다 (Resume 없이 즉시 정리) */
	void DestroyCoroutine()
	{
		if (CoroutineHandle)
		{
			CoroutineHandle.destroy();
			CoroutineHandle = nullptr;
		}
	}

protected:
	//-----------------------------------------------------------------------------
	// 파생 클래스용
	//-----------------------------------------------------------------------------

	/** 코루틴 핸들을 설정합니다 */
	void SetCoroutineHandle(std::coroutine_handle<> InHandle) { CoroutineHandle = InHandle; }

	/** 코루틴 핸들을 반환합니다 */
	std::coroutine_handle<> GetCoroutineHandle() const { return CoroutineHandle; }

protected:
	/** 공유 상태 */
	FCoroContextPtr Context;

	/** 코루틴 핸들 */
	std::coroutine_handle<> CoroutineHandle;
};

//-----------------------------------------------------------------------------
// TPromise<T> - 통합 Promise 템플릿
//-----------------------------------------------------------------------------

/**
 * 코루틴 Promise 통합 템플릿
 *
 * 모든 코루틴에서 사용하는 단일 Promise 타입입니다.
 * Async 스타일로 동작하며 (즉시 시작, 자동 정리),
 * Latent 동작은 Awaiter가 담당합니다.
 */
template<typename T>
class TPromise : public FPromise
{
public:
	/** Task 객체 생성 */
	TCoroTask<T> get_return_object()
	{
		Context = MakeShared<TCoroContext<T>, ESPMode::ThreadSafe>();
		Context->SetPromise(this);
		SetCoroutineHandle(std::coroutine_handle<TPromise>::from_promise(*this));

		return TCoroTask<T>(StaticCastSharedPtr<TCoroContext<T>>(Context));
	}

	/** co_return value 처리 */
	void return_value(T Value)
	{
		static_cast<TCoroContext<T>*>(Context.Get())->SetResult(MoveTemp(Value));
		Context->MarkCompleted(true);
	}
};

//-----------------------------------------------------------------------------
// TPromise<void> 특수화
//-----------------------------------------------------------------------------

/**
 * void 타입 Promise 특수화
 */
template<>
class TPromise<void> : public FPromise
{
public:
	/** Task 객체 생성 */
	TCoroTask<void> get_return_object()
	{
		Context = MakeShared<TCoroContext<void>, ESPMode::ThreadSafe>();
		Context->SetPromise(this);
		SetCoroutineHandle(std::coroutine_handle<TPromise>::from_promise(*this));

		return TCoroTask<void>(Context);
	}

	/** co_return; 처리 */
	void return_void()
	{
		Context->MarkCompleted(true);
	}
};

} // namespace Coro::Private