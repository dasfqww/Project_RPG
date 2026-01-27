// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayAbilitySpecHandle.h"
#include "Core/Awaiter.h"
#include "GameplayPrediction.h"
#include "Abilities/GameplayAbilityTargetTypes.h"

namespace Coro::Private
{

//-----------------------------------------------------------------------------
// FTargetDataAwaiter
//-----------------------------------------------------------------------------

/**
 * GAS TargetData를 대기하는 Awaiter
 *
 * AbilityTargetDataSetDelegate에 바인딩하여 클라이언트의 TargetData를 대기합니다.
 * 델리게이트 등록 후 CallReplicatedTargetDataDelegatesIfSet을 호출하여
 * 이미 도착한 데이터도 즉시 처리합니다.
 */
class COMMONCOROUTINE_API FTargetDataAwaiter : public TAsyncAwaiterBase<FTargetDataAwaiter>
{
	using Super = TAsyncAwaiterBase<FTargetDataAwaiter>;

public:
	using FResultType = TTuple<FGameplayAbilityTargetDataHandle, FGameplayTag>;

	FTargetDataAwaiter(UObject* InOwner, UAbilitySystemComponent* InASC, FGameplayAbilitySpecHandle InSpecHandle, FPredictionKey InPredictionKey);
	~FTargetDataAwaiter();

	//-----------------------------------------------------------------------------
	// TAsyncAwaiterBase 메서드
	//-----------------------------------------------------------------------------

	/** 델리게이트에 바인딩하고 대기합니다 */
	void Suspend();

	/** TargetData와 ApplicationTag를 반환합니다 */
	FResultType GetResult() { return MoveTemp(Result); }

private:
	/** AbilitySystemComponent (약한 참조) */
	TWeakObjectPtr<UAbilitySystemComponent> ASC;

	/** 어빌리티 스펙 핸들 */
	FGameplayAbilitySpecHandle SpecHandle;

	/** 예측 키 */
	FPredictionKey PredictionKey;

	/** 델리게이트 핸들 */
	FDelegateHandle Handle;

	/** 수신한 결과 */
	FResultType Result;
};

} // namespace Coro::Private

namespace Coro::GAS
{

//-----------------------------------------------------------------------------
// 사용자 함수
//-----------------------------------------------------------------------------

/**
 * 클라이언트의 GAS TargetData가 도착할 때까지 대기합니다
 *
 * 델리게이트 등록과 CallReplicatedTargetDataDelegatesIfSet 호출을 자동 처리합니다.
 * 코루틴 종료 시 델리게이트 바인딩이 자동으로 해제됩니다.
 *
 * @param Owner 코루틴을 소유한 UObject (파괴 시 안전하게 정리됨)
 * @param ASC AbilitySystemComponent
 * @param SpecHandle 어빌리티 스펙 핸들
 * @param PredictionKey 예측 키
 * @return TTuple<FGameplayAbilityTargetDataHandle, FGameplayTag>
 *
 * @code
 * TCoroTask<void> WaitForTargetDataCoroutine()
 * {
 *     auto [TargetDataHandle, ApplicationTag] = co_await Coro::GAS::WaitForTargetData(
 *         this, ASC, CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
 *
 *     // TargetDataHandle 처리
 * }
 * @endcode
 */
inline Private::FTargetDataAwaiter WaitForTargetData(UObject* Owner, UAbilitySystemComponent* ASC, FGameplayAbilitySpecHandle SpecHandle, FPredictionKey PredictionKey)
{
	return Private::FTargetDataAwaiter(Owner, ASC, SpecHandle, PredictionKey);
}
	
} // namespace Coro::GAS
