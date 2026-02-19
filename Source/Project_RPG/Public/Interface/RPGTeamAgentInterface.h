// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "UObject/Interface.h"
#include "UObject/ScriptInterface.h"
#include "Type/RPGEnumTypes.h"
#include "RPGTeamAgentInterface.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnRPGTeamIndexChangedDelegate, UObject*, ObjectChangingTeam, int32, OldTeamID, int32, NewTeamID);

/**
 * 팀 관련 유틸리티 인라인 함수들
 */
inline int32 RPGGenericTeamIdToInteger(FGenericTeamId ID)
{
	return (int32)ID.GetId();
}

inline FGenericTeamId RPGIntegerToGenericTeamId(int32 ID)
{
	return (ID == 255) ? FGenericTeamId::NoTeam : FGenericTeamId((uint8)ID);
}

inline FGenericTeamId RPGEnumToGenericTeamId(ERPGTeamID ID)
{
	return (ID == ERPGTeamID::NoTeam) ? FGenericTeamId::NoTeam : FGenericTeamId((uint8)ID);
}

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class URPGTeamAgentInterface : public UGenericTeamAgentInterface
{
	GENERATED_BODY()
};

/**
 * IRPGTeamAgentInterface: 프로젝트의 팀 관리 인터페이스
 */
class PROJECT_RPG_API IRPGTeamAgentInterface : public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	virtual FOnRPGTeamIndexChangedDelegate* GetOnTeamIndexChangedDelegate() { return nullptr; }

	//~ Begin IGenericTeamAgentInterface Interface
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;
	//~ End IGenericTeamAgentInterface Interface

	/** 팀 변경 시 델리게이트를 안전하게 브로드캐스트하는 헬퍼 함수 */
	static void ConditionalBroadcastTeamChanged(TScriptInterface<IRPGTeamAgentInterface> This, FGenericTeamId OldTeamID, FGenericTeamId NewTeamID);

	FOnRPGTeamIndexChangedDelegate& GetTeamChangedDelegateChecked()
	{
		FOnRPGTeamIndexChangedDelegate* Result = GetOnTeamIndexChangedDelegate();
		check(Result);
		return *Result;
	}
};
