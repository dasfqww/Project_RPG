// Fill out your copyright notice in the Description page of Project Settings.


#include "Skill/RPGSkillDefinition_Charge.h"
#include "Skill/RPGSkillAction_Charge.h"

URPGSkillDefinition_Charge::URPGSkillDefinition_Charge()
{
	// 기본 액션 클래스를 차징 액션으로 설정
	DefaultActionClass = URPGSkillAction_Charge::StaticClass();
}
