#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "SkillData.generated.h"

USTRUCT(BlueprintType)
struct FRPGSkillDataTable:public FTableRowBase
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skill)
	FName SkillName;  // 스킬 이름

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skill)
	float AttackPower;

	/*UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skill)
	float CoolTime;*/

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skill)
	float ManaCost;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skill)
	float IdentityGainAmount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skill)
	FText ToolTip;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skill)
	TObjectPtr<UTexture2D> SkillIcon;
};