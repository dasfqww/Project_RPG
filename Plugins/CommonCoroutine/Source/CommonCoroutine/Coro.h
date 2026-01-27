// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * 코루틴 시스템 통합 헤더
 *
 * 코루틴 함수를 작성할 때 이 헤더만 포함하면 됩니다.
 * 포함 순서가 중요한 모든 헤더를 올바른 순서로 포함합니다.
 *
 * @code
 * #include "Coro.h"
 *
 * TCoroTask<void> MyCoroutine()
 * {
 *     co_await Coro::Latent::Seconds(Owner, 1.0);
 *     co_return;
 * }
 * @endcode
 */

// 1. Task 정의 (TCoroTask<T>)
#include "Core/Task.h"

// 2. coroutine_traits 특수화 (promise_type 매핑)
#include "Core/Traits.h"

// 3. Promise 정의 (TPromise<T>)
#include "Core/Promise.h"

// 4. await_transform 구현 (co_await 변환)
#include "Core/AwaitTransform.h"