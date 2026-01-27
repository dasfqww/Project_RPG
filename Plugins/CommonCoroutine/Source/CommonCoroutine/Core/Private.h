// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include <coroutine>

/**
 * 코루틴 시스템 전방 선언 허브
 *
 * 모든 내부 타입의 전방 선언을 제공하여 순환 의존성을 방지합니다.
 * 각 헤더 파일은 이 파일을 포함하여 필요한 타입을 참조합니다.
 */

//-----------------------------------------------------------------------------
// TCoroTask 전방 선언 (Public API)
//-----------------------------------------------------------------------------

template<typename T = void>
class TCoroTask;

//-----------------------------------------------------------------------------
// Coro::Private 네임스페이스 (내부 구현)
//-----------------------------------------------------------------------------

namespace Coro::Private
{

//-----------------------------------------------------------------------------
// Context (공유 상태)
//-----------------------------------------------------------------------------

struct FCoroContext;

template<typename T>
struct TCoroContext;

} // namespace Coro::Private

//-----------------------------------------------------------------------------
// Context 포인터 타입 별칭
//-----------------------------------------------------------------------------

/** 스레드 안전한 기본 Context 포인터 타입 */
using FCoroContextPtr = TSharedPtr<Coro::Private::FCoroContext, ESPMode::ThreadSafe>;

/** 스레드 안전한 템플릿 Context 포인터 타입 */
template<typename T>
using TCoroContextPtr = TSharedPtr<Coro::Private::TCoroContext<T>, ESPMode::ThreadSafe>;

/** 스레드 안전한 템플릿 Context Weak 타입 */
template<typename T>
using TCoroContextWeakPtr = TWeakPtr<Coro::Private::TCoroContext<T>, ESPMode::ThreadSafe>;

namespace Coro::Private
{

//-----------------------------------------------------------------------------
// Promise
//-----------------------------------------------------------------------------

class FPromise;

template<typename T>
class TPromise;

//-----------------------------------------------------------------------------
// LatentAction
//-----------------------------------------------------------------------------

class FCoroLatentAction;

//-----------------------------------------------------------------------------
// Awaiter 기본 클래스
//-----------------------------------------------------------------------------

/** 폴링 인터페이스 (Latent Awaiter용) */
class ILatentPollable;

/** Latent Awaiter 기본 클래스 (폴링 방식) */
template<typename Derived>
class TLatentAwaiterBase;

/** Async Awaiter 기본 클래스 (콜백 방식) */
template<typename Derived>
class TAsyncAwaiterBase;

//-----------------------------------------------------------------------------
// Time Awaiters (Latent)
//-----------------------------------------------------------------------------

class FSecondsAwaiter;
class FFramesAwaiter;
class FUntilAwaiter;

//-----------------------------------------------------------------------------
// Thread Awaiters (Async)
//-----------------------------------------------------------------------------

class FThreadAwaiter;
class FTaskAwaiter;

//-----------------------------------------------------------------------------
// Combinator Awaiters (Async)
//-----------------------------------------------------------------------------

class FWhenAllAwaiter;
class FWhenAnyAwaiter;

//-----------------------------------------------------------------------------
// Delegate Awaiters (Async)
//-----------------------------------------------------------------------------

class FSimpleDelegateAwaiter;

template<typename... Args>
class TDelegateAwaiter;

//-----------------------------------------------------------------------------
// Asset Awaiters (Async)
//-----------------------------------------------------------------------------

template<typename T>
class TLoadObjectAwaiter;

template<typename T>
class TLoadClassAwaiter;

template<typename T>
class TLoadPrimaryAssetAwaiter;

class FLoadPrimaryAssetsAwaiter;

class FLoadGameFeatureAwaiter;

//-----------------------------------------------------------------------------
// Future Awaiters (Async)
//-----------------------------------------------------------------------------

template<typename T>
class TFutureAwaiter;

} // namespace Coro::Private