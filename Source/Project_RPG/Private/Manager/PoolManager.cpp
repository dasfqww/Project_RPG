// Fill out your copyright notice in the Description page of Project Settings.

#include "Manager/PoolManager.h"

void UPoolManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	//RegisterPool(FName("Item"), AActor::StaticClass(), 20);
}

void UPoolManager::RegisterPool(FName PoolName, TSubclassOf<AActor> ActorClass, int32 InitialSize)
{
	if (!PoolMap.Contains(PoolName))
	{
		PoolMap.Add(PoolName, MakeUnique<Pool>(ActorClass, InitialSize, GetWorld()));
	}
}

AActor* UPoolManager::AcquireFromPool(FName PoolName)
{
	if (TUniquePtr<Pool>* PoolPtr = PoolMap.Find(PoolName))
	{
		return (*PoolPtr)->Acquire();
	}
	return nullptr;
}

void UPoolManager::ReleaseToPool(FName PoolName, AActor* Actor)
{
	if (TUniquePtr<Pool>* PoolPtr = PoolMap.Find(PoolName))
	{
		(*PoolPtr)->Release(Actor);
	}
}
