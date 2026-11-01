// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Component/PawnExtensionComponentBase.h"
#include "GameplayTagContainer.h"
#include "Type/RPGStructTypes.h"
#include "RPGPlayerSkillComponent.generated.h"

class URPGSkillDefinition;
class URPGPlayerClassDefinition;
class FLifetimeProperty;

USTRUCT()
struct FRPGReplicatedSkillSaveEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FGameplayTag SkillTag;

	UPROPERTY()
	FRPGSkillSaveData SaveData;

	/** Links this authoritative acquisition record to the current restore request. */
	UPROPERTY()
	uint32 RestoreGeneration = 0;
};

UENUM(BlueprintType)
enum class ERPGPlayerSkillLoadState : uint8
{
	Uninitialized,
	Loading,
	Ready,
	Failed
};

/** Growth context only; applying/removing GAS grants belongs to the class application owner. */
USTRUCT()
struct FRPGPlayerSkillAccessState
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<URPGPlayerClassDefinition> ClassDefinition;

	UPROPERTY()
	FPrimaryAssetId ExpectedClassId;

	UPROPERTY()
	ERPGPlayerSkillLoadState LoadState = ERPGPlayerSkillLoadState::Uninitialized;

	UPROPERTY()
	uint32 RestoreGeneration = 0;
	UPROPERTY()
	bool bIdentityUnlocked = false;
	UPROPERTY()
	bool bConfigurationMutable = true;
};

/** Server-validated input, not a backend persistence implementation or a client command. */
struct FRPGPlayerSkillRestoreData
{
	static constexpr int32 CurrentVersion = 1;
	int32 Version = CurrentVersion;
	FPrimaryAssetId ClassId;
	int32 TotalSP = 0;
	bool bIdentityUnlocked = true; // Explicit development suppliers retain their existing recipe.
	bool bConfigurationMutable = true;
	TArray<FRPGReplicatedSkillSaveEntry> AcquiredSkills;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGSkillDataChangedSignature,
	FGameplayTag,
	SkillTag);

/**
 * URPGPlayerSkillComponent
 * 
 * 유저의 스킬 포인트(SP), 스킬 레벨, 트라이포드 선택 정보를 관리합니다.
 * 로스트아크의 스킬창 시스템을 뒷받침하는 핵심 데이터 컴포넌트입니다.
 */
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGPlayerSkillComponent : public UPawnExtensionComponentBase
{
	GENERATED_BODY()

public:
	URPGPlayerSkillComponent();
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Trusted server caller must retain a weak reference to this Pawn's component plus this token.
	 * A ready Pawn cannot be reinitialized in place; respawn/save restoration policy is separate. */
	uint32 BeginClassSkillRestore(FPrimaryAssetId ExpectedClassId);
	bool CompleteClassSkillRestore(uint32 RequestId, URPGPlayerClassDefinition* Definition,
		const FRPGPlayerSkillRestoreData& Data, FText& OutError);
	void FailClassSkillRestore(uint32 RequestId);
	/** Trusted supplier rollback, including a ready record whose grant/slot transaction failed. */
	void InvalidateClassSkillRestore(uint32 RequestId);
	bool IsClassIdentityUnlocked() const;
	bool IsConfigurationMutable() const { return SkillAccessState.bConfigurationMutable; }

	UFUNCTION(BlueprintPure, Category = "RPG|Skill")
	ERPGPlayerSkillLoadState GetSkillLoadState() const { return SkillAccessState.LoadState; }
	uint32 GetSkillRestoreGeneration() const { return SkillAccessState.RestoreGeneration; }
	URPGPlayerClassDefinition* GetPlayerClassDefinition() const;
	bool IsSkillInCurrentClass(FGameplayTag SkillTag) const;
	bool IsSkillAcquired(FGameplayTag SkillTag) const;
	bool TryGetSkillSaveData(FGameplayTag SkillTag, FRPGSkillSaveData& OutData) const;

	// ---------------------------------------------------
	// 1. 스킬 관리 API
	// ---------------------------------------------------

	/** 스킬 레벨업 시도 (SP 체크 및 레벨업 로직) */
	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	bool TryLevelUpSkill(FGameplayTag SkillTag);

	/** 스킬 레벨다운 시도 (SP 환급) */
	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	bool TryLevelDownSkill(FGameplayTag SkillTag);

	/** [편의기능] 가능한 최대치(또는 10레벨)까지 한 번에 레벨업 (Shift + 클릭) */
	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	void LevelUpToMax(FGameplayTag SkillTag, int32 TargetGoalLevel = 10);

	/** [편의기능] 스킬 레벨을 1로 초기화하고 모든 SP 환급 (Shift + 우클릭 등) */
	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	void ResetSkillLevel(FGameplayTag SkillTag);

	/** 트라이포드 선택 (레벨 제한 체크 포함) */
	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	bool SelectTripod(FGameplayTag SkillTag, int32 TierIndex, int32 OptionIndex);

	/** 특정 스킬의 저장된 데이터 가져오기 */
	UFUNCTION(BlueprintPure, Category = "RPG|Skill")
	FRPGSkillSaveData GetSkillSaveData(FGameplayTag SkillTag) const;

	// ---------------------------------------------------
	// 2. 스킬 포인트(SP) 관리
	// ---------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "RPG|Skill")
	int32 GetRemainingSP() const;

	UFUNCTION(BlueprintPure, Category = "RPG|Skill")
	int32 GetTotalSP() const { return TotalSP; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "RPG|Skill")
	void AddTotalSP(int32 Amount);

	// 스킬 레벨에 따른 소모 SP 계산 (로아 방식: 고레벨일수록 많이 필요)
	UFUNCTION(BlueprintPure, Category = "RPG|Skill")
	int32 GetRequiredSPForLevel(int32 TargetLevel) const;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Skill")
	FRPGSkillDataChangedSignature OnSkillDataChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(Server, Reliable)
	void ServerTryLevelUpSkill(FGameplayTag SkillTag);

	UFUNCTION(Server, Reliable)
	void ServerTryLevelDownSkill(FGameplayTag SkillTag);

	UFUNCTION(Server, Reliable)
	void ServerLevelUpToMax(FGameplayTag SkillTag, int32 TargetGoalLevel);

	UFUNCTION(Server, Reliable)
	void ServerResetSkillLevel(FGameplayTag SkillTag);

	UFUNCTION(Server, Reliable)
	void ServerSelectTripod(
		FGameplayTag SkillTag,
		int32 TierIndex,
		int32 OptionIndex);

	UFUNCTION()
	void OnRep_ReplicatedSkillData();
	
private:
	bool bEndingPlay = false;
	bool CanModifySkill(FGameplayTag SkillTag) const;
	bool ApplyLevelUpSkill(FGameplayTag SkillTag);
	bool ApplyLevelDownSkill(FGameplayTag SkillTag);
	void ApplyLevelUpToMax(FGameplayTag SkillTag, int32 TargetGoalLevel);
	void ApplyResetSkillLevel(FGameplayTag SkillTag);
	bool ApplySelectTripod(
		FGameplayTag SkillTag,
		int32 TierIndex,
		int32 OptionIndex);
	void PublishAuthoritativeSkillData(FGameplayTag SkillTag);
	void TouchAuthoritativeRevision();
	int32 GetEffectiveUsedSP() const;
	void SetEffectiveUsedSP(int32 Amount);
	bool IsAuthorityOwner() const;
	const URPGSkillDefinition* FindSkillDefinition(
		FGameplayTag SkillTag) const;

	// 유저의 스킬 정보 맵 (태그 -> 데이터)
	UPROPERTY()
	TMap<FGameplayTag, FRPGSkillSaveData> SkillDataMap;

	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedSkillData)
	TArray<FRPGReplicatedSkillSaveEntry> ReplicatedSkillData;

	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedSkillData)
	uint32 ReplicatedSkillDataRevision = 0;

	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedSkillData)
	FRPGPlayerSkillAccessState SkillAccessState;

	UPROPERTY(EditDefaultsOnly, Replicated, Category = "RPG|Skill")
	int32 TotalSP = 50; // 기본 지급 SP

	UPROPERTY(Replicated)
	int32 UsedSP = 0;

	// Never modify the replicated authority baseline during client prediction.
	// A revision-only rejection can then restore SP without resending UsedSP.
	int32 PredictedUsedSP = INDEX_NONE;
};
