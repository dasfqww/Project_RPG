#pragma once

#include "CoreMinimal.h"
#include "RPGEnumTypes.generated.h"

UENUM(BlueprintType)
enum class ERPGTeamID : uint8
{
	Player,
	Enemy,
	Neutral,

	NoTeam = 255
};
UENUM()
enum class ERPGConfirmType : uint8
{
	Yes,
	No
};

UENUM()
enum class ERPGValidType : uint8
{
	Valid,
	Invalid
};

UENUM()
enum class ERPGSuccessType : uint8
{
	Successful,
	Failed
};

UENUM()
enum class ERPGCountDownActionInput :uint8
{
	Start,
	Cancel
};

UENUM()
enum class ERPGCountDownActionOutput :uint8
{
	Updated,
	Completed,
	Canceled
};

//UENUM(BlueprintType)
//enum class ERPGSkillCastType : uint8
//{
//	Instant UMETA(DisplayName = "Instant Skill"), // ��� ���� ��ų),
//	Combo UMETA(DisplayName = "Combo Skill"),
//	Holding UMETA(DisplayName = "Holding Skill"),
//	Casting UMETA(DisplayName = "Casting Skill"),
//	Charge UMETA(DisplayName = "Charge Skill")	
//};

UENUM(BlueprintType)
enum class ERPGAttackType : uint8
{
	Melee UMETA(DisplayName = "Melee Attack"),
	Projectile UMETA(DisplayName = "Ranged Attack"),
	AOE UMETA(DisplayName = "AOE Attack")
};

UENUM(BlueprintType)
enum class ERPGGameDifficulty : uint8
{
	Easy,
	Normal,
	Hard,
	Hell
};

UENUM(BlueprintType)
enum class ERPGInputMode : uint8
{
	GameOnly,
	UIOnly
};

UENUM(BlueprintType)
enum class ESoundType : uint8
{
	BGM     UMETA(DisplayName = "BGM"),
	Effect  UMETA(DisplayName = "Effect"),
	Max
};

UENUM(BlueprintType)
enum class EAOETraceType : uint8
{
	Box     UMETA(DisplayName = "Box"),
	Sphere  UMETA(DisplayName = "Sphere"),
	Cone  UMETA(DisplayName = "Cone"),
	Capsule  UMETA(DisplayName = "Capsule"),
	Max
};

UENUM(BlueprintType)
enum class EItemCategory :uint8
{
	Equip  UMETA(DisplayName = "Equip"),
	Consume UMETA(DisplayName = "Consume"),
	Craft UMETA(DisplayName = "Craft"),
	None
};

UENUM(BlueprintType)
enum class EWeaponHandType : uint8
{
	LeftHand,
	RightHand,
	TwoHand,

	Count	UMETA(Hidden)
};

/** D1 equipment categories retained for imported ability and item metadata. */
UENUM(BlueprintType)
enum class ERPGGladiatorEquipmentType : uint8
{
	Armor,
	Weapon,
	Utility,

	Count UMETA(Hidden)
};

/** D1 weapon identities. Count means the current item/actor has no strict metadata yet. */
UENUM(BlueprintType)
enum class ERPGGladiatorWeaponType : uint8
{
	Unarmed,
	OneHandSword,
	TwoHandSword,
	GreatSword,
	Shield,
	Staff,
	Bow,

	Count UMETA(Hidden)
};

/** D1 utility identities retained for serialized equipment requirements. */
UENUM(BlueprintType)
enum class ERPGGladiatorUtilityType : uint8
{
	Drink,
	LightSource,

	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EOverlayTargetType : uint8
{
	None,
	Weapon,
	Character,
	All,
};

UENUM(BlueprintType)
enum class ETileQuadrant : uint8
{
	TopLeft,
	TopRight,
	BottomLeft,
	BottomRight,
	None
};

UENUM(BlueprintType)
enum class EConditionType : uint8
{
	TimeElapsed, 
	MoneyGreaterThan,
	CoolTime,
	Max
};

/** 로스트아크식 직업 아이덴티티 타입 */
UENUM(BlueprintType)
enum class ERPGIdentityType : uint8
{
	Cost,       /** 게이지를 코스트로 소모하는 형태 (예: 블링크) */
	Transform,  /** 게이지가 차면 변신하는 형태 (예: 데모닉) */
    BuffMode    /** 게이지가 차면 강화 모드로 진입하는 형태 (예: 버서커) */,
};

UENUM(BlueprintType)
enum class EEquipState : uint8
{
    None,
    WeaponSet_Primary,
    WeaponSet_Secondary,
    Utility,

    Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EEquipmentSlotType : uint8
{
    Head,
    Chest,
    Legs,
    Feet,
    Hands,
    Necklace,
    Ring_L,
    Ring_R,
    Weapon_Primary_L,
    Weapon_Primary_R,
    Weapon_Secondary_L,
    Weapon_Secondary_R,
    Utility_1,
    Utility_2,

    Count UMETA(Hidden),
    None UMETA(Hidden)
};
