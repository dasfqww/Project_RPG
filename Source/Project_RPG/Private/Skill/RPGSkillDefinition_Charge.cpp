// Fill out your copyright notice in the Description page of Project Settings.


#include "Skill/RPGSkillDefinition_Charge.h"
#include "Skill/RPGSkillAction_Charge.h"
#include "Skill/RPGSkillExecutionPolicy.h"
#include "Skill/RPGSkillExecutionTypes.h"

URPGSkillDefinition_Charge::URPGSkillDefinition_Charge()
{
	// 기본 액션 클래스를 차징 액션으로 설정
	DefaultActionClass = URPGSkillAction_Charge::StaticClass();
}

void URPGSkillDefinition_Charge::ApplyDefinitionExecutionDefaults(
	FRPGSkillRuntimeSpec& OutSpec) const
{
	OutSpec.ExecutionPolicyClass = URPGSkillExecutionPolicy_Charge::StaticClass();

	FRPGSkillChargeExecutionConfig Config;
	Config.MaxChargeHoldTime = MaxChargeHoldTime;
	Config.MaxChargeLevel = MaxChargeLevel;
	Config.ChargeTimePerLevel = ChargeTimePerLevel;
	if (const FName* ChargeSection = MontageSections.SectionNamesToPlay.Find(0))
	{
		Config.ChargeSection = *ChargeSection;
	}
	if (const FName* ReleaseSection = MontageSections.SectionNamesToPlay.Find(1))
	{
		Config.ReleaseSection = *ReleaseSection;
	}
	OutSpec.ExecutionConfig.InitializeAs<FRPGSkillChargeExecutionConfig>(Config);
}
