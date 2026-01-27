// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Type/RPGEnumTypes.h"
#include "StructUtils/InstancedStruct.h"
#include "GameplayTagContainer.h"
#include "RPGItemManifest.generated.h"

class URPGItemBase;
struct FItemFragment;
class URPGCompositeBase;

USTRUCT()
struct PROJECT_RPG_API FItemManifest
{
	GENERATED_BODY()

	TArray<TInstancedStruct<FItemFragment>>& GetFragmentsMutable() { return Fragments; }
	URPGItemBase* Manifest(UObject* NewOuter);
	EItemCategory GetItemCategory() const { return ItemCategory; }
	FGameplayTag GetItemTag() const { return ItemTag; }
	void AssimilateInventoryFragments(URPGCompositeBase* Composite) const;

	template<typename T> requires std::derived_from<T, FItemFragment>
	const T* GetFragmentOfTypeByTag(const FGameplayTag& FragmentTag) const;

	template<typename T> requires std::derived_from<T, FItemFragment>
	const T* GetFragmentOfType() const;

	template<typename T> requires std::derived_from<T, FItemFragment>
	T* GetFragmentOfTypeMutable();

	template<typename T> requires std::derived_from<T, FItemFragment>
	TArray<const T*> GetAllFragmentsOfType() const;

	void SpawnPickupActor(const UObject* WorldContextObject,
		const FVector& SpawnLocation, const FRotator& SpawnRotation);

private:
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (ExcludeBaseStruct))
	TArray<TInstancedStruct<FItemFragment>> Fragments;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	EItemCategory ItemCategory = EItemCategory::None;

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (Categories = "GameItems"))
	FGameplayTag ItemTag;

	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (Categories = "GameItems"))
	FName TagName;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<AActor> PickupActorClass;

	void ClearFragments();
};

template<typename T> requires std::derived_from<T, FItemFragment>
inline const T* FItemManifest::GetFragmentOfTypeByTag(const FGameplayTag& FragmentTag) const
{
	for (const TInstancedStruct<FItemFragment>& Fragment :Fragments)
	{
		if (const T* FragmentPtr = Fragment.GetPtr<T>())
		{
			if (!FragmentPtr->GetFragmentTag().MatchesTagExact(FragmentTag)) continue;
			return FragmentPtr;
		}
	}

	return nullptr;
}

template<typename T> requires std::derived_from<T, FItemFragment>
inline const T* FItemManifest::GetFragmentOfType() const
{
	for (const TInstancedStruct<FItemFragment>& Fragment : Fragments)
	{
		if (const T* FragmentPtr = Fragment.GetPtr<T>())
		{
			return FragmentPtr;
		}
	}

	return nullptr;
}

template<typename T> requires std::derived_from<T, FItemFragment>
inline T* FItemManifest::GetFragmentOfTypeMutable()
{
	for (TInstancedStruct<FItemFragment>& Fragment : Fragments)
	{
		if (T* FragmentPtr = Fragment.GetMutablePtr<T>())
		{
			return FragmentPtr;
		}
	}

	return nullptr;
}

template<typename T> requires std::derived_from<T, FItemFragment>
inline TArray<const T*> FItemManifest::GetAllFragmentsOfType() const
{
	TArray<const T*> Result;
	for (const TInstancedStruct<FItemFragment>& Fragment : Fragments)
	{
		if (const T* FragmentPtr = Fragment.GetPtr<T>())
		{
			Result.Add(FragmentPtr);
		}
	}

	return Result;
}
