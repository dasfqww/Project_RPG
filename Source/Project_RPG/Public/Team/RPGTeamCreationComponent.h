// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/GameStateComponent.h"
#include "RPGTeamCreationComponent.generated.h"

class URPGTeamDisplayAsset;
class ARPGTeamPublicInfo;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGTeamCreationComponent : public UGameStateComponent
{
	GENERATED_BODY()
public:
	URPGTeamCreationComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected: // 파티/진영 정의
	UPROPERTY(EditDefaultsOnly, Category = Teams) 
	TMap<uint8, TObjectPtr<URPGTeamDisplayAsset>> TeamsToCreate; 
	
	UPROPERTY(EditDefaultsOnly, Category=Teams) 
	TSubclassOf<ARPGTeamPublicInfo> PublicTeamInfoClass;

protected: 
	virtual void BeginPlay() override; 
	
	// 던전/인스턴스 로드 시 팀 초기화 
	//void OnDungeonLoaded(const URPGDungeonDefinition* Dungeon); 

	// 파티 단위 팀 생성
	virtual void ServerCreatePartyTeam(int32 PartyId, URPGTeamDisplayAsset* DisplayAsset);

	// NPC 진영 생성 
	virtual void ServerCreateFactionTeam(int32 FactionId, URPGTeamDisplayAsset* DisplayAsset);

#if WITH_SERVER_CODE
protected: 
	// 서버에서 아군/적 NPC 팀 생성
	virtual void ServerCreateTeams(); 

private: 
	// 플레이어가 던전에 들어올 때 파티 팀에 배정 
	void OnPlayerInitialized(AGameModeBase* GameMode, AController* NewPlayer); 
	// 특정 팀 생성 (아군/적 NPC 구분) 
	void ServerCreateTeam(int32 TeamId, URPGTeamDisplayAsset* DisplayAsset);

#endif
};
