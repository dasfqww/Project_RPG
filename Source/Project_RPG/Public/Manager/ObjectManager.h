// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameInstance/RPGGameInstanceSubsystem.h"
#include "ObjectManager.generated.h"

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UObjectManager : public URPGGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	// 오브젝트 스폰 (풀링 여부 선택 가능)
	AActor* SpawnObject(FName PoolName, TSubclassOf<AActor> ActorClass, 
		const FVector& Location, const FRotator& Rotation, bool bUsePooling = true);

	// 오브젝트 제거 
	void Despawn(FName PoolName, AActor* Actor, bool bUsePooling = true);

private:
	TSet<AActor*> ManagedObjects;
	TSet<AActor*> Players;
	TSet<AActor*> Monsters;

public:
	// 카운트 조회 
	FORCEINLINE int32 GetPlayerCount() const { return Players.Num(); }
	FORCEINLINE int32 GetMonsterCount() const { return Monsters.Num(); }
	FORCEINLINE int32 GetManagedCount() const { return ManagedObjects.Num(); }
};
