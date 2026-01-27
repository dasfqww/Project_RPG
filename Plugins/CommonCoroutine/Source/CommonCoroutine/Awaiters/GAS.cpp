// Copyright Epic Games, Inc. All Rights Reserved.

#include "Awaiters/GAS.h"
#include "AbilitySystemComponent.h"

namespace Coro::Private
{

//-----------------------------------------------------------------------------
// FTargetDataAwaiter
//-----------------------------------------------------------------------------

FTargetDataAwaiter::FTargetDataAwaiter(UObject* InOwner, UAbilitySystemComponent* InASC, FGameplayAbilitySpecHandle InSpecHandle, FPredictionKey InPredictionKey)
	: Super(InOwner)
	, ASC(InASC)
	, SpecHandle(InSpecHandle)
	, PredictionKey(InPredictionKey)
{
}

FTargetDataAwaiter::~FTargetDataAwaiter()
{
	// 델리게이트 바인딩 정리
	if (Handle.IsValid() && ASC.IsValid())
	{
		ASC->AbilityTargetDataSetDelegate(SpecHandle, PredictionKey).Remove(Handle);
	}
}

void FTargetDataAwaiter::Suspend()
{
	if (!ASC.IsValid())
	{
		SafeResume(Owner, Context);
		return;
	}

	// 1. 델리게이트 등록
	Handle = ASC->AbilityTargetDataSetDelegate(SpecHandle, PredictionKey).AddLambda(
		[CapturedOwner = Owner, CapturedContext = Context, WeakASC = ASC, CapturedSpecHandle = SpecHandle, CapturedPredictionKey = PredictionKey, HandlePtr = &Handle, ResultPtr = &Result]
		(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag Tag)
		{
			if (CapturedOwner.IsValid())
			{
				*ResultPtr = FResultType(Data, Tag);

				// 델리게이트 해제
				if (WeakASC.IsValid())
				{
					WeakASC->AbilityTargetDataSetDelegate(CapturedSpecHandle, CapturedPredictionKey).Remove(*HandlePtr);
					HandlePtr->Reset();
				}
			}
			SafeResume(CapturedOwner, CapturedContext);
		}
	);

	// 2. 이미 도착한 데이터 즉시 처리 (GAS 표준 패턴)
	ASC->CallReplicatedTargetDataDelegatesIfSet(SpecHandle, PredictionKey);
}
	
} // namespace Coro::Private
