// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InputCoreTypes.h"
#include "DataAsset_KeyIconConfig.generated.h"

USTRUCT(BlueprintType)
struct FRPGKeyIconPair
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FKey Key;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UTexture2D> Icon;
};

/**
 * 키(Key)와 텍스처(Icon)를 매핑하는 데이터 에셋
 */
UCLASS()
class PROJECT_RPG_API UDataAsset_KeyIconConfig : public UDataAsset
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config")
	TArray<FRPGKeyIconPair> KeyIcons;

	// 키에 해당하는 텍스처를 찾는 헬퍼 함수
	UTexture2D* FindIconForKey(const FKey& InKey) const
	{
		for (const FRPGKeyIconPair& Pair : KeyIcons)
		{
			if (Pair.Key == InKey)
			{
				return Pair.Icon;
			}
		}
		return nullptr;
	}

#if WITH_EDITOR
	// 에디터에서 버튼으로 호출하여 자동으로 아이콘을 채우는 함수
	UFUNCTION(CallInEditor, Category = "Config")
	void AutoPopulateKeyIcons();
#endif
};
