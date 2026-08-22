#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "RPGGladiatorData.generated.h"

class AAIController;
class UAnimMontage;
class UGameplayEffect;
class UMaterialInterface;
class USkeletalMesh;
class UTexture2D;
class UUserWidget;
class URPGAbilitySet;
class URPGAbilitySystemComponent;
struct FRPGAbilitySet_GrantedHandles;

/** D1-compatible class identities. Kept separate from the project's current equipment enums. */
UENUM(BlueprintType)
enum class ERPGGladiatorCharacterClass : uint8
{
	Fighter,
	Swordmaster,
	Barbarian,
	Wizard,
	Archer,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ERPGGladiatorEquipmentSlotType : uint8
{
	Unarmed_LeftHand,
	Unarmed_RightHand,
	Primary_LeftHand,
	Primary_RightHand,
	Primary_TwoHand,
	Secondary_LeftHand,
	Secondary_RightHand,
	Secondary_TwoHand,
	Utility_Primary,
	Utility_Secondary,
	Utility_Tertiary,
	Utility_Quaternary,
	Helmet,
	Chest,
	Legs,
	Hands,
	Foot,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ERPGGladiatorItemRarity : uint8
{
	Poor,
	Common,
	Uncommon,
	Rare,
	Legendary,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ERPGGladiatorArmorType : uint8
{
	Helmet,
	Chest,
	Legs,
	Hands,
	Foot,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ERPGGladiatorSkinType : uint8
{
	Asian,
	Black,
	Count UMETA(Hidden)
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGAssetEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FName AssetName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FSoftObjectPath AssetPath;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TArray<FName> AssetLabels;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGAssetSet
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TArray<FRPGAssetEntry> AssetEntries;
};

UCLASS(BlueprintType, Const, CollapseCategories, meta = (DisplayName = "RPG Asset Data"))
class PROJECT_RPG_API URPGAssetData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	FSoftObjectPath GetAssetPathByName(FName AssetName) const;
	const FRPGAssetSet* FindAssetSetByLabel(FName Label) const;

private:
	UPROPERTY(EditDefaultsOnly)
	TMap<FName, FRPGAssetSet> AssetGroupNameToSet;

	UPROPERTY()
	TMap<FName, FSoftObjectPath> AssetNameToPath;

	UPROPERTY()
	TMap<FName, FRPGAssetSet> AssetLabelToSet;
};

UCLASS(BlueprintType, Const, meta = (DisplayName = "RPG Cheat Data"))
class PROJECT_RPG_API URPGCheatData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cheat")
	TArray<TSoftObjectPtr<UAnimMontage>> AnimMontagePaths;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cheat")
	TSubclassOf<UGameplayEffect> ResetCooldownGameplayEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cheat")
	TSubclassOf<UGameplayEffect> ResetVitalGameplayEffectClass;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDefaultArmorMeshSet
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSoftObjectPtr<UMaterialInterface> UpperBodySkinMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSoftObjectPtr<UMaterialInterface> LowerBodySkinMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSoftObjectPtr<USkeletalMesh> HeadDefaultMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSoftObjectPtr<USkeletalMesh> HeadSecondaryMesh;

	UPROPERTY(EditDefaultsOnly, meta = (ArraySizeEnum = "ERPGGladiatorArmorType"))
	TSoftObjectPtr<USkeletalMesh> DefaultMeshEntries[static_cast<int32>(ERPGGladiatorArmorType::Count)];

	UPROPERTY(EditDefaultsOnly, meta = (ArraySizeEnum = "ERPGGladiatorArmorType"))
	TSoftObjectPtr<USkeletalMesh> SecondaryMeshEntries[static_cast<int32>(ERPGGladiatorArmorType::Count)];
};

UCLASS(BlueprintType, Const, meta = (DisplayName = "RPG Character Data"))
class PROJECT_RPG_API URPGCharacterData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	const FRPGDefaultArmorMeshSet* FindDefaultArmorMeshSet(ERPGGladiatorSkinType SkinType) const;

private:
	UPROPERTY(EditDefaultsOnly)
	TMap<ERPGGladiatorSkinType, FRPGDefaultArmorMeshSet> DefaultArmorMeshMap;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDefaultItemEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	ERPGGladiatorEquipmentSlotType EquipmentSlotType = ERPGGladiatorEquipmentSlotType::Count;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UObject> ItemTemplateClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	ERPGGladiatorItemRarity ItemRarity = ERPGGladiatorItemRarity::Poor;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "1"))
	int32 ItemCount = 1;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGClassInfoEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FText ClassName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TArray<FRPGDefaultItemEntry> DefaultItemEntries;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<URPGAbilitySet> ClassAbilitySet;
};

UCLASS(BlueprintType, Const, meta = (DisplayName = "RPG Class Data"))
class PROJECT_RPG_API URPGClassData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const URPGClassData* GetDefaultClassData();
	const FRPGClassInfoEntry* FindClassInfo(ERPGGladiatorCharacterClass CharacterClass) const;
	bool GiveClassAbilitiesToAbilitySystem(
		ERPGGladiatorCharacterClass CharacterClass,
		URPGAbilitySystemComponent* AbilitySystemComponent,
		FRPGAbilitySet_GrantedHandles* OutGrantedHandles = nullptr,
		UObject* SourceObject = nullptr) const;

private:
	UPROPERTY(EditDefaultsOnly, meta = (ArraySizeEnum = "ERPGGladiatorCharacterClass"))
	FRPGClassInfoEntry ClassInfoEntries[static_cast<int32>(ERPGGladiatorCharacterClass::Count)];
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGElectricFieldPhaseEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float TargetRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float BreakTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float NoticeTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float ShrinkTime = 0.0f;
};

UCLASS(BlueprintType, Const, meta = (DisplayName = "RPG Electric Field Phase Data"))
class PROJECT_RPG_API URPGElectricFieldPhaseData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	bool IsValidPhaseIndex(int32 PhaseIndex) const;
	const FRPGElectricFieldPhaseEntry* FindPhaseEntry(int32 PhaseIndex) const;

private:
	UPROPERTY(EditDefaultsOnly)
	TArray<FRPGElectricFieldPhaseEntry> PhaseEntries;
};

UCLASS(BlueprintType, Const, meta = (DisplayName = "RPG Item Data"))
class PROJECT_RPG_API URPGGladiatorItemData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	TMap<int32, TSubclassOf<UObject>> ItemTemplateIDToClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	TMap<TSubclassOf<UObject>, int32> ItemTemplateClassToID;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	TArray<TSubclassOf<UObject>> WeaponItemTemplateClasses;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	TArray<TSubclassOf<UObject>> ArmorItemTemplateClasses;
};

UCLASS(BlueprintType, Const, meta = (DisplayName = "RPG Monster Data"))
class PROJECT_RPG_API URPGMonsterData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Monster")
	TMap<TSubclassOf<AAIController>, TObjectPtr<UPrimaryDataAsset>> PawnDataMap;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGItemRarityInfoEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (HideAlphaChannel))
	FColor Color = FColor::Black;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UTexture2D> EntryTexture;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UTexture2D> HoverTexture;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGUIInfo
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FText Title;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FText Content;
};

UCLASS(BlueprintType, Const, meta = (DisplayName = "RPG UI Data"))
class PROJECT_RPG_API URPGUIData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	FIntPoint UnitInventorySlotSize = FIntPoint::ZeroValue;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	TSubclassOf<UUserWidget> DragWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	TSubclassOf<UUserWidget> ItemHoverWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	TSubclassOf<UUserWidget> SkillStatHoverWidget;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	TSubclassOf<UUserWidget> EquipmentEntryWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	TSubclassOf<UUserWidget> InventorySlotWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	TSubclassOf<UUserWidget> InventoryEntryWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	TSubclassOf<UUserWidget> InventoryValidWidgetClass;

	UPROPERTY(EditDefaultsOnly, meta = (ArraySizeEnum = "ERPGGladiatorItemRarity"))
	FRPGItemRarityInfoEntry RarityInfoEntries[static_cast<int32>(ERPGGladiatorItemRarity::Count)];

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (DisplayName = "Tag UI Infos"))
	TMap<FGameplayTag, FRPGUIInfo> TagUIInfos;
};
