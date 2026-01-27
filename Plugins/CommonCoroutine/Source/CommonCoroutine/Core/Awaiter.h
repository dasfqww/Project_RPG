// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Private.h"
#include "Core/LatentAction.h"
#include "Core/Context.h"
#include "Engine/World.h"
#include <coroutine>

namespace Coro::Private
{

//-----------------------------------------------------------------------------
// TLatentAwaiterBase
//-----------------------------------------------------------------------------

/**
 * Latent Awaiter 기본 클래스 (폴링 방식)
 *
 * FLatentActionManager에 등록되어 매 틱 ShouldResume()을 폴링합니다.
 * 조건이 충족되면 코루틴을 재개합니다.
 *
 * 파생 클래스 필수 메서드:
 * - bool ShouldResume() override: 재개 조건 확인 (매 틱 호출)
 *
 * 파생 클래스 선택적 메서드:
 * - void OnCancel() override: 취소 시 정리 작업
 * - ResultType GetResult(): 결과값 반환 (await_resume에서 호출)
 *
 * @tparam Derived 파생 클래스 타입 (await_resume에서 GetResult 호출용)
 */
template<typename Derived>
class TLatentAwaiterBase : public ILatentPollable
{
public:
	explicit TLatentAwaiterBase(UObject* InOwner) : Owner(InOwner)
	{
	}

	//-----------------------------------------------------------------------------
	// Awaiter 인터페이스
	//-----------------------------------------------------------------------------

	/** 항상 suspend합니다 */
	bool await_ready() const { return false; }

	/** LatentAction을 생성하고 등록합니다 */
	template<typename Promise>
	void await_suspend(std::coroutine_handle<Promise> Handle)
	{
		// Promise에서 Context 획득
		FCoroContextPtr Context = Handle.promise().GetContextShared();

		if (!Owner.IsValid())
		{
			Context->Resume();
			return;
		}

		UWorld* World = Owner->GetWorld();
		if (!World)
		{
			Context->Resume();
			return;
		}

		// LatentAction 생성 및 등록
		FLatentActionManager& LatentManager = World->GetLatentActionManager();
		LatentManager.AddNewAction(Owner.Get(), GetLatentUUID(), new FCoroLatentAction(this, Context));
	}

	/** 파생 클래스에 GetResult()가 있으면 호출, 없으면 void */
	auto await_resume()
	{
		if constexpr (requires { static_cast<Derived*>(this)->GetResult(); })
		{
			return static_cast<Derived*>(this)->GetResult();
		}
	}

protected:
	/** Owner를 반환합니다 */
	UObject* GetOwner() const { return Owner.Get(); }

	/** Owner의 World를 반환합니다 */
	UWorld* GetWorld() const { return Owner.IsValid() ? Owner->GetWorld() : nullptr; }

	/** LatentAction용 고유 UUID를 반환합니다 */
	int32 GetLatentUUID() const { return static_cast<int32>(reinterpret_cast<UPTRINT>(static_cast<const Derived*>(this))); }

private:
	/** Owner (약한 참조) */
	TWeakObjectPtr<UObject> Owner;
};

//-----------------------------------------------------------------------------
// TAsyncAwaiterBase
//-----------------------------------------------------------------------------

/**
 * Async Awaiter 기본 클래스 (콜백 방식)
 *
 * 비동기 작업 완료 시 콜백으로 코루틴을 재개합니다.
 * 폴링하지 않고 작업 완료 시점에 직접 Resume()을 호출합니다.
 *
 * Owner가 파괴되면 Resume() 호출 시 코루틴을 파괴하여 안전하게 정리합니다.
 *
 * 파생 클래스 필수 메서드:
 * - void Suspend(): 비동기 작업 시작 (완료 시 Resume() 호출)
 *
 * 파생 클래스 선택적 메서드:
 * - bool Ready(): await_ready에서 호출 (즉시 완료 가능하면 true)
 * - ResultType GetResult(): 결과값 반환 (await_resume에서 호출)
 *
 * @tparam Derived 파생 클래스 타입 (await_resume에서 GetResult 호출용)
 */
template<typename Derived>
class TAsyncAwaiterBase
{
public:
	explicit TAsyncAwaiterBase(UObject* InOwner) : Owner(InOwner)
	{
	}

	//-----------------------------------------------------------------------------
	// Awaiter 인터페이스
	//-----------------------------------------------------------------------------

	/** 파생 클래스에 Ready()가 있으면 호출, 없으면 false */
	bool await_ready() const
	{
		if constexpr (requires { static_cast<const Derived*>(this)->Ready(); })
		{
			return static_cast<const Derived*>(this)->Ready();
		}
		else
		{
			return false;
		}
	}

	/** Context를 저장하고 파생 클래스의 Suspend()를 호출합니다 */
	template<typename Promise>
	void await_suspend(std::coroutine_handle<Promise> Handle)
	{
		Context = Handle.promise().GetContextShared();
		static_cast<Derived*>(this)->Suspend();
	}

	/** 파생 클래스에 GetResult()가 있으면 호출, 없으면 void */
	auto await_resume()
	{
		if constexpr (requires { static_cast<Derived*>(this)->GetResult(); })
		{
			return static_cast<Derived*>(this)->GetResult();
		}
	}

protected:
	/** Owner를 반환합니다 */
	UObject* GetOwner() const { return Owner.Get(); }

	/** Context를 반환합니다 */
	FCoroContextPtr GetContext() const { return Context; }

protected:
	/** Owner (약한 참조) */
	TWeakObjectPtr<UObject> Owner;

	/** 코루틴 Context */
	FCoroContextPtr Context;
};

//-----------------------------------------------------------------------------
// SafeResume
//-----------------------------------------------------------------------------

/**
 * 안전하게 코루틴을 재개합니다
 *
 * Owner가 파괴되었으면 코루틴 프레임을 파괴합니다.
 * 비동기 콜백의 람다에서 사용합니다 (this 캡처 대신 Owner와 Context만 캡처).
 */
inline void SafeResume(const TWeakObjectPtr<UObject>& InOwner, const FCoroContextPtr& InContext)
{
	if (!InContext)
	{
		return;
	}

	// Owner가 파괴되었으면 코루틴 프레임을 파괴합니다
	if (!InOwner.IsValid() || InContext->IsCancelRequested())
	{
		if (!InContext->IsDone())
		{
			InContext->Destroy();
		}
		return;
	}

	InContext->Resume();
}

} // namespace Coro::Private