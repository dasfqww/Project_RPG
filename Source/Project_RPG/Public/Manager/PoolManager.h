// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameInstance/RPGGameInstanceSubsystem.h"
#include "PoolManager.generated.h"

class Pool
{
public:
	Pool(TSubclassOf<AActor> InActorClass, int32 InitialSize = 10, UWorld* InWorld = nullptr)
		: ActorClass(InActorClass), World(InWorld)
	{
		for (int32 i = 0; i < InitialSize; ++i)
		{
			AActor* NewActor = World->SpawnActor<AActor>(ActorClass);
			NewActor->SetActorHiddenInGame(true);
			NewActor->SetActorEnableCollision(false);
			ActorPool.Add(NewActor);
		}
	}

	AActor* Acquire()
	{
		for (AActor* Actor : ActorPool)
		{
			if (Actor->IsHidden())
			{
				Actor->SetActorHiddenInGame(false);
				Actor->SetActorEnableCollision(true);
				return Actor;
			}
		}
		AActor* NewActor = World->SpawnActor<AActor>(ActorClass);
		ActorPool.Add(NewActor);
		return NewActor;
	}

	void Release(AActor* Actor)
	{
		if (!IsValid(Actor)) return;

		Actor->SetActorHiddenInGame(true);
		Actor->SetActorLocation(FVector::ZeroVector);
		Actor->SetActorEnableCollision(false);
	}

private:
	TSubclassOf<AActor> ActorClass;
	TArray<AActor*> ActorPool;
	TObjectPtr<UWorld> World;
};

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UPoolManager : public URPGGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	void RegisterPool(FName PoolName, TSubclassOf<AActor> ActorClass, int32 InitialSize = 10);

	AActor* AcquireFromPool(FName PoolName);

	void ReleaseToPool(FName PoolName, AActor* Actor);

private:
	TMap<FName, TUniquePtr<Pool>> PoolMap;
};
