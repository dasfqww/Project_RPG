// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Type/RPGEnumTypes.h"
#include "RPGFunctionLibrary.generated.h"

class URPGAbilitySystemComponent;
class UPawnCombatComponent;
class URPGInventoryComponent;
class UQuickSlotComponent;
class URPGInventoryGrid;
class URPGHoverItem;
struct FScalableFloat;
class URPGGameInstance;
class UNiagaraSystem;
class APlayerController;
class ARPGPickUpBase;
class URPGItemBase;
class UWidget;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	static URPGAbilitySystemComponent* NativeGetWarriorASCFromActor(AActor* InActor);

	UFUNCTION(BlueprintCallable, Category = "RPG|FunctionLibrary")
		static void AddGameplayTagToActorIfNone(AActor* InActor, FGameplayTag TagToAdd);

	UFUNCTION(BlueprintCallable, Category = "RPG|FunctionLibrary")
		static void RemoveGameplayTagFromActorIfFound(AActor* InActor, FGameplayTag TagToRemove);

	static bool NativeDoesActorHaveTag(AActor* InActor, FGameplayTag TagToCheck);

	UFUNCTION(BlueprintCallable, Category = "RPG|FunctionLibrary", meta=(DisplayName="Does Actor Have Tag", ExpandEnumAsExecs="OutConfirmType"))
		static void BP_DoesActorHaveTag(AActor* InActor, FGameplayTag TagToCheck, ERPGConfirmType& OutConfirmType);

	static UPawnCombatComponent* NativeGetPawnCombatComponentFromActor(AActor* InActor);

	UFUNCTION(BlueprintCallable, Category = "RPG|FunctionLibrary", meta = (DisplayName = "Get Pawn Combat Component From Actor", ExpandEnumAsExecs = "OutValidType"))
		static UPawnCombatComponent* BP_GetPawnCombatComponentFromActor(AActor* InActor, ERPGValidType& OutValidType);

	UFUNCTION(BlueprintPure, Category = "RPG|FunctionLibrary")
		static bool IsTargetPawnHostile(APawn* QueryPawn, APawn* TargetPawn);

	UFUNCTION(BlueprintPure, Category = "RPG|FunctionLibrary", meta = (CompactNodeTitle = "Get Value At Level"))
		static float GetScalableFloatValueAtLevel(const FScalableFloat& InScalableFloat, float InLevel = 1.f);

	UFUNCTION(BlueprintPure, Category = "RPG|FunctionLibrary")
		static FGameplayTag ComputeHitReactDirectionTag(AActor* InAttacker, 
			AActor* InVictim, float& OutAngleDifference);

	UFUNCTION(BlueprintPure, Category = "RPG|FunctionLibrary")
		static bool IsValidBlock(AActor* InAttacker, AActor* InDefender);

	UFUNCTION(BlueprintCallable, Category = "Trace")
	static TArray<FHitResult> DoSphereTrace(UObject* WorldContext, FVector Origin, float Radius, float TraceLength,
		TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes, AActor* IgnoredActor);

	UFUNCTION(BlueprintCallable, Category = "Trace")
	static TArray<FHitResult> DoBoxTrace(UObject* WorldContext, FVector Start, FVector HalfSize, FRotator Rotation,
		float TraceLength, TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes);

	UFUNCTION(BlueprintCallable, Category = "RPG|FunctionLibrary")
		static bool ApplyGameplayEffectSpecHandleToTargetActor(AActor* InInstigator, AActor* InTargetActor, 
			const FGameplayEffectSpecHandle& InSpecHandle);

	UFUNCTION(BlueprintCallable, Category = "RPG|FunctionLibrary",
		meta = (Latent, WorldContext = "WorldContextObject", 
			LatentInfo = "LatentInfo", ExpandEnumAsExecs = "CountDownInput|CountDownOutput",
			TotalTime = "1.0", UpdateInterval = "0.1"))
	static void CountDown(const UObject* WorldContextObject, float TotalTime, float UpdateInterval,
		float& OutRemainingTime, ERPGCountDownActionInput CountDownInput,
		UPARAM(DisplayName = "Output") ERPGCountDownActionOutput& CountDownOutput, FLatentActionInfo LatentInfo);

	UFUNCTION(BlueprintPure, Category = "RPG|FunctionLibrary", meta = (WorldContext = "WorldContextObject"))
	static URPGGameInstance* GetRPGGameInstance(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "RPG|FunctionLibrary", meta = (WorldContext = "WorldContextObject"))
	static void ToggleInputMode(const UObject* WorldContextObject, ERPGInputMode InInputMode);

	UFUNCTION(BlueprintCallable, Category = "RPG|FunctionLibrary", meta = (WorldContext = "WorldContextObject"))
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

	UFUNCTION(BlueprintCallable, Category = "RPG|FunctionLibrary", meta = (WorldContext = "WorldContextObject"))
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

#pragma region UI_Utils

	UFUNCTION(BlueprintPure, Category = "RPG|FunctionLibrary", meta = (DisplayName = "Format Time To MM:SS"))
	static FString FormatTimeToMMSS(float InSeconds);

	static FVector2D GetClampedWidgetPosition(const FVector2D& Boundary,
		const FVector2D& WidgetSize, const FVector2D& MousePos);

	UFUNCTION(BlueprintCallable, Category = "RPG|FunctionLibrary", meta = (WorldContext = "WorldContextObject"))
	static int32 GetIndexFromWidgetPosition(const FIntPoint& Position, const int32 Columns);

	UFUNCTION(BlueprintCallable, Category = "RPG|FunctionLibrary", meta = (WorldContext = "WorldContextObject"))
	static FIntPoint GetPositionFromWidgetIndex(const int32 Index, const int32 Columns);

	template<typename T>
	static T* GetComponentFromPlayerController(const APlayerController* PlayerController);

	UFUNCTION(BlueprintCallable, Category = "RPG|FunctionLibrary")
	static URPGHoverItem* GetHoverItem(APlayerController* PC);

	UFUNCTION(BlueprintCallable, Category = "RPG|FunctionLibrary")
	static EItemCategory GetItemCategoryFromItemPickup(ARPGPickUpBase* ItemPickup);

	UFUNCTION(BlueprintCallable, Category = "RPG|FunctionLibrary")
	static FVector2D GetWidgetPosition(UWidget* Widget);

	UFUNCTION(BlueprintCallable, Category = "RPG|FunctionLibrary")
	static FVector2D GetWidgetSize(UWidget* Widget);

	UFUNCTION(BlueprintCallable, Category = "RPG|FunctionLibrary")
	static bool IsWithinBounds(const FVector2D& BoundaryPos, 
		const FVector2D& WidgetSize, const FVector2D& MousePos);

	template<typename T, typename FuncT>
	static void ForeachGridSlot2D(TArray<T>& Array, int32 Index,
		const FIntPoint& Range2D, int32 GridColumns, const FuncT& Function);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	static void ItemHovered(APlayerController* PC, URPGItemBase* Item);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	static void ItemUnhovered(APlayerController* PC);

	template<typename TEnum>
	static bool TryConvertStringToEnum(const FString& StringKey, TEnum& OutEnum);

	template<typename TEnum>
	static FString GetEnumNameString(TEnum EnumValue);

#pragma endregion
	
};

template<typename T>
inline T* URPGFunctionLibrary::GetComponentFromPlayerController(const APlayerController* PlayerController)
{
	if (!IsValid(PlayerController)) return nullptr;

	return PlayerController->FindComponentByClass<T>();
}

template<typename T, typename FuncT>
inline void URPGFunctionLibrary::ForeachGridSlot2D(TArray<T>& Array, int32 Index,
	const FIntPoint& Range2D, int32 GridColumns, const FuncT& Function)
{
	for (int32 j = 0; j < Range2D.Y; ++j)
	{
		for (int32 i = 0; i < Range2D.X; ++i)
		{
			const FIntPoint Coordinates = GetPositionFromWidgetIndex(Index, GridColumns) + FIntPoint(i, j);
			const int32 TileIndex = GetIndexFromWidgetPosition(Coordinates, GridColumns);
			if (Array.IsValidIndex(TileIndex))
			{
				Function(Array[TileIndex]);
			}
		}
	}
}

template<typename TEnum>
inline bool URPGFunctionLibrary::TryConvertStringToEnum(const FString& StringKey, TEnum& OutEnum)
{
	if (const UEnum* EnumPtr = StaticEnum<TEnum>())
	{
		FName NameKey = FName(StringKey);
		int64 EnumValue = EnumPtr->GetValueByName(NameKey);
		if (EnumValue != INDEX_NONE)
		{
			OutEnum = static_cast<TEnum>(EnumValue);
			return true;
		}
	}
	return false;
}

template<typename TEnum>
inline FString URPGFunctionLibrary::GetEnumNameString(TEnum EnumValue)
{
	const UEnum* EnumPtr = StaticEnum<TEnum>();
	return EnumPtr ? EnumPtr->GetNameStringByValue(static_cast<int64>(EnumValue)) : TEXT("");
}