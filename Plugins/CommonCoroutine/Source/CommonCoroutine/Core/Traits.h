// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Core/Private.h"

/**
 * TCoroTask<T>에 대한 coroutine_traits 특수화
 *
 * 컴파일러가 TCoroTask<T> 반환형의 코루틴을 만나면 이 traits를 통해 promise_type을 결정합니다.
 * 모든 TCoroTask<T> 코루틴은 TPromise<T>를 promise_type으로 사용합니다.
 */
template<typename T, typename... Args>
struct std::coroutine_traits<TCoroTask<T>, Args...>
{
	using promise_type = Coro::Private::TPromise<T>;
};