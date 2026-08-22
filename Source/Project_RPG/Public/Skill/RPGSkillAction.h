// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Skill/RPGSkillRuntimeTypes.h"
#include "UObject/NoExportTypes.h"
#include "RPGSkillAction.generated.h"

class URPGGameplayAbility;
class URPGSkillDefinition;
class ACharacter;
class URPGAbilitySystemComponent;
struct FRPGHitQueryResult;

/**
 * URPGSkillAction
 * 
 * 스킬의 실제 "동작 로직"을 담당하는 모듈입니다.
 * Ability 내에서 인스턴싱되어 실행됩니다.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew)
class PROJECT_RPG_API URPGSkillAction : public UObject
{
	GENERATED_BODY()

public:
	URPGSkillAction();

	// 초기화 (Ability에서 실행 전 호출)
	virtual void Initialize(URPGGameplayAbility* InAbility, URPGSkillDefinition* InDefinition,
		const FRPGSkillRuntimeSpec& InRuntimeSpec);

	// 액션 시작 (진입점)
	virtual void StartAction();

	// 액션 종료 (정상 종료)
	virtual void EndAction();

	// 액션 취소 (피격, 다른 스킬 등으로 인한 중단)
	virtual void CancelAction();

	// 매 프레임 업데이트 (필요한 경우)
	virtual void TickAction(float DeltaTime);

	virtual void OnInputPressed();
	virtual void OnInputReleased();

protected:
	// 헬퍼 함수들
	ACharacter* GetCharacter() const;
	/**
	 * Execute the activation-frozen targeting profile.
	 * Damage and Gameplay Effect application remain the caller's responsibility.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Skill|Targeting")
	bool ExecuteHitQuery(
		const FTransform& QueryTransform,
		const TArray<AActor*>& AlreadyHitActors,
		TArray<FRPGHitQueryResult>& OutResults,
		bool bForceDebug = false) const;

	URPGAbilitySystemComponent* GetAbilitySystemComponent() const;
	UWorld* GetWorld() const override;

	// 몽타주 재생 헬퍼
	void PlayMontage();

protected:
	UPROPERTY(Transient)
	TObjectPtr<URPGGameplayAbility> OwnerAbility;

	UPROPERTY(Transient)
	TObjectPtr<URPGSkillDefinition> SkillDefinition;

	/** Activation-local copy; actions never read mutable UI selection state. */
	UPROPERTY(Transient)
	FRPGSkillRuntimeSpec RuntimeSpec;

	bool bIsActive;
};
