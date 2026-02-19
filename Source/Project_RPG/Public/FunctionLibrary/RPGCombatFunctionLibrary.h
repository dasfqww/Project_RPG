// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Type/RPGEnumTypes.h"
#include "GameplayTagContainer.h"
#include "RPGCombatFunctionLibrary.generated.h"

class UPawnCombatComponent;
class UNiagaraSystem;
class UDecalComponent;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGCombatFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	static UPawnCombatComponent* NativeGetPawnCombatComponentFromActor(AActor* InActor);

	UFUNCTION(BlueprintCallable, Category = "RPG|CombatFunctionLibrary", meta = (DisplayName = "Get Pawn Combat Component From Actor", ExpandEnumAsExecs = "OutValidType"))
		static UPawnCombatComponent* BP_GetPawnCombatComponentFromActor(AActor* InActor, ERPGValidType& OutValidType);

	UFUNCTION(BlueprintPure, Category = "RPG|CombatFunctionLibrary")
		static bool IsTargetPawnHostile(APawn* QueryPawn, APawn* TargetPawn);

	UFUNCTION(BlueprintPure, Category = "RPG|CombatFunctionLibrary")
		static FGameplayTag ComputeHitReactDirectionTag(AActor* InAttacker, 
			AActor* InVictim, float& OutAngleDifference);

	UFUNCTION(BlueprintPure, Category = "RPG|CombatFunctionLibrary")
		static bool IsValidBlock(AActor* InAttacker, AActor* InDefender);

	UFUNCTION(BlueprintCallable, Category = "Trace")
	static TArray<FHitResult> DoSphereTrace(UObject* WorldContext, FVector Origin, float Radius, float TraceLength,
		TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes, AActor* IgnoredActor);

	UFUNCTION(BlueprintCallable, Category = "Trace")
	static TArray<FHitResult> DoBoxTrace(UObject* WorldContext, FVector Start, FVector HalfSize, FRotator Rotation,
		float TraceLength, TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes);

	UFUNCTION(BlueprintCallable, Category = "RPG|CombatFunctionLibrary", meta = (WorldContext = "WorldContextObject"))
	static bool FanShapeCollisionCheck(
		const UObject* WorldContextObject,
		const FVector& Center,
		const FVector& ForwardDirection,
		float Radius,
		float AngleDegrees,
		const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes,
		AActor* IgnoredActor,
		TArray<AActor*>& OutActors,
		bool bDrawDebug = false,
		FColor DebugColor = FColor::Red,
		float DebugDuration = 2.0f
	);

	UFUNCTION(BlueprintCallable, Category = "RPG|CombatFunctionLibrary", meta = (WorldContext = "WorldContextObject"))
	static UDecalComponent* SpawnFanShapeDecal(
		const UObject* WorldContextObject,
		UMaterialInterface* DecalMaterial,
		const FVector& Location,
		const FRotator& Rotation,
		float Radius,
		float AngleDegrees,
		const FLinearColor& Color,
		float Duration = 2.0f
	);
};
