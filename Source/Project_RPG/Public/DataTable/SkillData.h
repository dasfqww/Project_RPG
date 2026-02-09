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
	FName SkillName;  // ��ų �̸�

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skill)
	float AttackPower = 0.0f;

	/*UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skill)
	float CoolTime;*/

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skill)
	float ManaCost = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skill)
	float IdentityGainAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skill)
	FText ToolTip;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skill)
	TObjectPtr<UTexture2D> SkillIcon;
};