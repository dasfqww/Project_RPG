// Fill out your copyright notice in the Description page of Project Settings.


#include "Manager/ObjectManager.h"
#include "Manager/PoolManager.h"
#include "RPGDebugHelper.h"

AActor* UObjectManager::SpawnObject(FName PoolName, TSubclassOf<AActor> ActorClass, 
    const FVector& Location, const FRotator& Rotation, bool bUsePooling)
{
	AActor* SpawnedActor = nullptr;

	if (bUsePooling)
	{
		// 풀이 존재하지 않으면 자동으로 등록 (초기 크기: 5개)
		SpawnedActor = UPoolManager::Get<UPoolManager>(this)->AcquireFromPool(PoolName);
		if (!IsValid(SpawnedActor))
		{
			// 풀에 없으면 새로 등록
			Debug::Print(FString::Printf(TEXT("Pool '%s' not found. Creating new pool..."), *PoolName.ToString()), FColor::Yellow);
			UPoolManager::Get<UPoolManager>(this)->RegisterPool(PoolName, ActorClass, 15);
			SpawnedActor = UPoolManager::Get<UPoolManager>(this)->AcquireFromPool(PoolName);
			
			if (IsValid(SpawnedActor))
			{
				Debug::Print(FString::Printf(TEXT("Pool '%s' created successfully!"), *PoolName.ToString()), FColor::Green);
			}
		}
		else
		{
			Debug::Print(FString::Printf(TEXT("Actor acquired from pool '%s'"), *PoolName.ToString()), FColor::Cyan);
		}

		if (IsValid(SpawnedActor))
		{
			SpawnedActor->SetActorLocation(Location);
			SpawnedActor->SetActorRotation(Rotation);
			Debug::Print(FString::Printf(TEXT("Actor positioned at Location: (%.1f, %.1f, %.1f)"), 
				Location.X, Location.Y, Location.Z), FColor::Green);
		}
	}
	else
	{
		SpawnedActor = GetWorld()->SpawnActor<AActor>(ActorClass, Location, Rotation);
		Debug::Print(TEXT("Actor spawned without pooling (Directly spawned)"), FColor::Orange);
	}

	if (IsValid(SpawnedActor))
	{
		ManagedObjects.Add(SpawnedActor);
		Debug::Print(FString::Printf(TEXT("Total Managed Objects: %d"), ManagedObjects.Num()), FColor::Blue);
	}
	else
	{
		Debug::Print(TEXT("Failed to spawn actor!"), FColor::Red);
	}

    return SpawnedActor;
}

void UObjectManager::Despawn(FName PoolName, AActor* Actor, bool bUsePooling)
{
	if(!IsValid(Actor)) 
	{
		Debug::Print(TEXT("Attempting to despawn invalid actor!"), FColor::Red);
		return;
	}

	Debug::Print(FString::Printf(TEXT("Despawning actor: %s from pool: %s"), *Actor->GetName(), *PoolName.ToString()), FColor::Yellow);

	if (bUsePooling)
	{
		UPoolManager* PoolManager = UPoolManager::Get<UPoolManager>(this);
		if (IsValid(PoolManager))
		{
			PoolManager->ReleaseToPool(PoolName, Actor);
			Debug::Print(FString::Printf(TEXT("Actor returned to pool '%s'"), *PoolName.ToString()), FColor::Green);
		}
		else
		{
			Debug::Print(TEXT("PoolManager is not valid! Destroying actor instead"), FColor::Red);
			Actor->Destroy();
		}
	}
	else
	{
		Actor->Destroy();
		Debug::Print(TEXT("Actor destroyed directly"), FColor::Green);
	}

	ManagedObjects.Remove(Actor);
	Debug::Print(FString::Printf(TEXT("Total Managed Objects: %d"), ManagedObjects.Num()), FColor::Blue);
}
