// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "GameplayTagContainer.h"
#include "ScalableFloat.h"
#include "StructUtils/InstancedStruct.h"
#include "Type/RPGEnumTypes.h"
#include "RPGStructTypes.generated.h"

class URPGPlayerLinkedAnimLayer;
class URPGGameplayAbility;
class UInputMappingContext;
class URPGItemBase;
class UNiagaraSystem;
class UAnimMontage;
class URPGSkillAction;
class URPGSkillExecutionPolicy;
class URPGSkillTargetingPolicy;

/**
 * 유저의 개별 스킬 성장 데이터
 */
USTRUCT(BlueprintType)
struct FRPGSkillSaveData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 SkillLevel = 1;

	// 선택된 트라이포드 인덱스 배열 (인덱스 0: 1티어, 1: 2티어, 2: 3티어 / -1은 선택 안됨)
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<int32> SelectedTripodIndices = { -1, -1, -1 };
};

USTRUCT(BlueprintType)
struct FRPGPlayerAbilitySet
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (Categories = "InputTag"))
		FGameplayTag InputTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
		TSubclassOf<URPGGameplayAbility> AbilityToGrant;

	bool IsValid() const;
};

USTRUCT(BlueprintType)
struct FRPGPlayerSkillSet:public FRPGPlayerAbilitySet
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSoftObjectPtr<UMaterialInterface> SoftAbilityIconMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (Categories = "Player.Cooldown"))
	FGameplayTag AbilityCooldownTag;
};

USTRUCT(BlueprintType)
struct FSingleSectionData
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Skill")
	TObjectPtr<UAnimMontage> MontageToPlay;

	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	FName SectionNameToPlay;//for instant

	/*UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	ERPGAttackType AttackType;*/
};



/** 
 * 트라이포드가 주는 수치적 변화 (기존 RPGSkillConfig 설계 계승)
 */
USTRUCT(BlueprintType)
struct FRPGSkillModifier
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (Categories = "Shared.Stat"))
	FGameplayTag StatTag; 

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float ScalarValue = 1.0f;
};

/** 
 * 로스트아크식 트라이포드 선택지 (통합본)
 */
USTRUCT(BlueprintType)
struct FRPGSkillTripodOption
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "General")
	FText OptionName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "General")
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "General")
	TSoftObjectPtr<UTexture2D> OptionIcon;

	/** 1. 수치 변조: 데미지, 쿨감 등 (기존 기능) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Logic")
	TArray<FRPGSkillModifier> StatModifiers;

	/** 2. 로직 분기 태그 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Logic")
	FGameplayTag TripodTag;

	/** 3. 로직 자체 교체: 차징->즉발 등 (새 기능) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Logic")
	TSubclassOf<URPGSkillAction> OverrideActionClass;

	/** Preferred replacement path for runtime input/execution behavior. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Logic")
	TSubclassOf<URPGSkillExecutionPolicy> OverrideExecutionPolicyClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Logic",
		meta = (BaseStruct = "/Script/Project_RPG.RPGSkillExecutionConfig"))
	FInstancedStruct OverrideExecutionConfig;

	/** Optional replacement of crosshair, soft-target, or ground-point behavior. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Logic")
	TSubclassOf<URPGSkillTargetingPolicy> OverrideTargetingPolicyClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Logic",
		meta = (BaseStruct = "/Script/Project_RPG.RPGSkillTargetingConfig"))
	FInstancedStruct OverrideTargetingConfig;

	/** 4. 비주얼 오버라이드 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UAnimMontage> OverrideMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UNiagaraSystem> OverrideVFX;
};

USTRUCT(BlueprintType)
struct FMultipleSectionData
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Skill")
	TObjectPtr<UAnimMontage> MontageToPlay;

	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	TMap<int,FName> SectionNamesToPlay;//for Combo&Casting&Charge

	/*UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	ERPGAttackType AttackType;*/
};

USTRUCT(BlueprintType)
struct FChargeLevelNiagaraOptionData
{
	GENERATED_BODY()

	//  ܰ迡 شϴ 
	UPROPERTY(EditDefaultsOnly, Category = "Charge Effect")
	bool bAddDetail = false;

	UPROPERTY(EditDefaultsOnly, Category = "Charge Effect")
	bool bSimple = false;
};

USTRUCT(BlueprintType)
struct FRPGPlayerWeaponData
{
	GENERATED_BODY()

		UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
		TSubclassOf<URPGPlayerLinkedAnimLayer> WeaponAnimLayerToLink;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
		TObjectPtr<UInputMappingContext> WeaponInputMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (TitleProperty = "InputTag"))
		TArray<FRPGPlayerAbilitySet> DefaultWeaponAbilities;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (TitleProperty = "InputTag"))
		TArray<FRPGPlayerSkillSet> SpecialWeaponAbilities;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
		FScalableFloat WeaponBaseDamage;
};

USTRUCT()
struct FContentData
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FString Description;

	UPROPERTY()
	TArray<FString> RewardItems;  //  ̸ Ʈ ( ó)

	
	
};

USTRUCT(BlueprintType)
struct FDropItem
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Drop Item")
	TSubclassOf<class ARPGPickUpBase> ItemClass;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Drop Item")
	int32 DropQuantity = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Drop Item")
	float DropChance = 0.0f;
};

USTRUCT(BlueprintType)
struct FRewardItem
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Drop Item")
	FName ItemRowName;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Drop Item")
	int32 DropQuantity = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Drop Item")
	float DropChance = 0.0f;
};

USTRUCT(BlueprintType)
struct FSlotAvailability
{
	GENERATED_BODY()

	FSlotAvailability() {}
	FSlotAvailability(int32 ItemIndex, int32 Space, bool bHasItem) :
		Index(ItemIndex), AmountToFill(Space), bItemAtIndex(bHasItem) {}

	int32 Index = INDEX_NONE;
	int32 AmountToFill = 0;
	bool bItemAtIndex = false;

};

USTRUCT(BlueprintType)
struct FSlotAvailabilityResult
{
	GENERATED_BODY()

	FSlotAvailabilityResult() {}

	TWeakObjectPtr<URPGItemBase> Item;

	int32 TotalSpaceToFill = 0;
	int32 Remainder = 0;

	bool bStackable = false;
	TArray<FSlotAvailability> SlotAvailabilities;
};

USTRUCT(BlueprintType)
struct FTileParameters
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Inventory")
	FIntPoint TileCoords{};

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Inventory")
	int32 TileIndex{ INDEX_NONE };

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Inventory")
	ETileQuadrant TileQuadrant = ETileQuadrant::None;
};

inline bool operator==(const FTileParameters& A, const FTileParameters& B)
{
	return A.TileCoords == B.TileCoords &&
		A.TileIndex == B.TileIndex && A.TileQuadrant == B.TileQuadrant;
}

USTRUCT()
struct FSpaceQueryResult
{
	GENERATED_BODY()

	// True if the space queried has no items in it
	bool bHasSpace{ false };

	// Valid if there's a single item we can swap with
	TWeakObjectPtr<URPGItemBase> ValidItem = nullptr;

	// Upper left index of the valid item, if there is one
	int32 UpperLeftIndex = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct FItemSaveData
{
	GENERATED_BODY()

	UPROPERTY()
	FName ItemID;

	UPROPERTY()
	int32 Quantity = 0;

	UPROPERTY()
	int32 SlotIndex = -1;

	UPROPERTY()
	FString Category;

	UPROPERTY()
	FString InstanceId;
};


USTRUCT()
struct FGraphicSaveData
{
	GENERATED_BODY()

	UPROPERTY()
	FString Resolution;

	UPROPERTY()
	FString WindowMode;

	UPROPERTY()
	bool bVSync = false;
};


USTRUCT(BlueprintType)
struct FSoundSaveData
{
	GENERATED_BODY()

	UPROPERTY()
	float MasterVolume = 1.0f;

	UPROPERTY()
	bool bMasterMuted = false;

	UPROPERTY()
	TMap<FString, float> Volumes;

	UPROPERTY()
	TMap<FString, bool> Mutes;
};

/** 직업별 아이덴티티 설정 데이터 */
USTRUCT(BlueprintType)
struct FRPGPlayerIdentityData
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	ERPGIdentityType IdentityType = ERPGIdentityType::Cost;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<URPGGameplayAbility> IdentityAbility;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FLinearColor GaugeColor = FLinearColor::White;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSoftObjectPtr<UTexture2D> IdentityIcon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSoftObjectPtr<UMaterialInterface> IdentityGaugeMaterial;
};

/**
 * 퀵슬롯에 담길 수 있는 내용물 (아이템 또는 스킬)
 */
USTRUCT(BlueprintType)
struct FRPGQuickSlotContent
{
	GENERATED_BODY()

	FRPGQuickSlotContent() : AbilityTag(FGameplayTag::EmptyTag), Item(nullptr) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QuickSlot")
	FGameplayTag AbilityTag; // 스킬 태그 (GAS)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QuickSlot")
	TObjectPtr<URPGItemBase> Item; // 아이템

	bool IsEmpty() const { return !AbilityTag.IsValid() && !Item; }
};
