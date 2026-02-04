// Fill out your copyright notice in the Description page of Project Settings.

#include "DataAsset/Input/DataAsset_KeyIconConfig.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"

void UDataAsset_KeyIconConfig::AutoPopulateKeyIcons()
{
	KeyIcons.Empty();

	// 아이콘이 있는 폴더 경로 (프로젝트 상황에 맞게 수정 가능)
	const FString IconFolderPath = TEXT("/Game/Assets/Textures/UI/ControllerIcons/MouseKeyboard");

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> AssetDataList;
	
	// 폴더 내의 모든 Texture2D 에셋을 찾음
	FARFilter Filter;
	Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(*IconFolderPath);
	Filter.bRecursivePaths = true; // 하위 폴더까지 검색

	AssetRegistryModule.Get().GetAssets(Filter, AssetDataList);

	for (const FAssetData& AssetData : AssetDataList)
	{
		UTexture2D* IconTexture = Cast<UTexture2D>(AssetData.GetAsset());
		if (!IconTexture) continue;

		FString FileName = AssetData.AssetName.ToString();
		FString KeyNameString;

		// 파일명 파싱 규칙: "KeyName_Key_Dark" 형태라고 가정
		// 예: "A_Key_Dark" -> "A", "Space_Key_Dark" -> "Space"
		// 첫 번째 '_' 문자가 나오기 전까지를 키 이름으로 취급
		if (FileName.Split(TEXT("_"), &KeyNameString, nullptr))
		{
			// 예외 처리: 파일명과 언리얼 FKey 이름이 다른 경우 매핑
			// 필요하면 여기에 if 문을 추가해서 직접 매핑해줘야 함
			if (KeyNameString.Equals(TEXT("Ctrl"), ESearchCase::IgnoreCase)) KeyNameString = TEXT("LeftControl");
			else if (KeyNameString.Equals(TEXT("Alt"), ESearchCase::IgnoreCase)) KeyNameString = TEXT("LeftAlt");
			else if (KeyNameString.Equals(TEXT("Shift"), ESearchCase::IgnoreCase)) KeyNameString = TEXT("LeftShift");
			else if (KeyNameString.Equals(TEXT("Space"), ESearchCase::IgnoreCase)) KeyNameString = TEXT("SpaceBar");
			else if (KeyNameString.Equals(TEXT("Esc"), ESearchCase::IgnoreCase)) KeyNameString = TEXT("Escape");
			else if (KeyNameString.Equals(TEXT("Enter"), ESearchCase::IgnoreCase)) KeyNameString = TEXT("Enter");
			else if (KeyNameString.Equals(TEXT("BackSpace"), ESearchCase::IgnoreCase)) KeyNameString = TEXT("BackSpace");
            else if (KeyNameString.Equals(TEXT("Arrow"), ESearchCase::IgnoreCase)) // Arrow_Up 등 처리
            {
                // Arrow_Up_Key_Dark -> KeyNameString="Arrow", Remainder="Up_Key_Dark"
                // 다시 한번 Split 필요
                FString Temp, Direction;
                if (FileName.Split(TEXT("_"), &Temp, &Direction)) // "Arrow" | "Up_Key_Dark"
                {
                    if (Direction.StartsWith(TEXT("Up"))) KeyNameString = TEXT("Up");
                    else if (Direction.StartsWith(TEXT("Down"))) KeyNameString = TEXT("Down");
                    else if (Direction.StartsWith(TEXT("Left"))) KeyNameString = TEXT("Left");
                    else if (Direction.StartsWith(TEXT("Right"))) KeyNameString = TEXT("Right");
                }
            }
			
			// 숫자 키 처리 (0~9) -> "Zero", "One"... 은 아님. 언리얼은 그냥 "0", "1"
			// 파일명이 "0_Key_Dark" 라면 "0"으로 잘 들어옴.

			FKey Key(*KeyNameString);
			
			// 키가 유효한지 확인 (존재하는 키인지)
			if (Key.IsValid())
			{
				FRPGKeyIconPair NewPair;
				NewPair.Key = Key;
				NewPair.Icon = IconTexture;
				KeyIcons.Add(NewPair);
			}
		}
	}

	// 저장
	MarkPackageDirty();
}
#endif
