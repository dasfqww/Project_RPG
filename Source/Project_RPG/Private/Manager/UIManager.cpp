// Fill out your copyright notice in the Description page of Project Settings.


#include "Manager/UIManager.h"
#include "UI/RPGWidgetBase.h"

UUIManager* UUIManager::Instance = nullptr;

UUIManager* UUIManager::Get()
{
	if (!Instance)
	{
		Instance = NewObject<UUIManager>();
		Instance->AddToRoot(); // GC 방지
	}

	return Instance;
}

void UUIManager::PushUI(URPGWidgetBase* NewUI)
{
	if (!NewUI) return;

	// 현재 UI 비활성화
	if (PopUpUIStack.Num() > 0)
	{
		PopUpUIStack.Last()->SetIsEnabled(false);
	}

	PopUpUIStack.Add(NewUI);
	NewUI->AddToViewport();
	NewUI->SetIsEnabled(true);
	NewUI->SetVisibility(ESlateVisibility::Visible);
}

void UUIManager::PopUI()
{
	if (PopUpUIStack.Num() == 0) return;

	URPGWidgetBase* TopUI = PopUpUIStack.Pop();
	TopUI->RemoveFromParent();

	// 스택에 남은 UI가 있다면 다시 활성화
	if (PopUpUIStack.Num() > 0)
	{
		PopUpUIStack.Last()->SetIsEnabled(true);
		PopUpUIStack.Last()->SetVisibility(ESlateVisibility::Visible);
	}
}

void UUIManager::RemoveUI(URPGWidgetBase* TargetUI)
{
	if (!TargetUI) return;

	// 스택에서 해당 UI를 찾아서 제거
	int32 Index = PopUpUIStack.Find(TargetUI);
	if (Index != INDEX_NONE)
	{
		PopUpUIStack.RemoveAt(Index);
		TargetUI->RemoveFromParent();

		// 만약 방금 제거한 게 맨 위였다면, 새로운 탑 UI 활성화
		if (Index == PopUpUIStack.Num())
		{
			if (PopUpUIStack.Num() > 0)
			{
				PopUpUIStack.Last()->SetIsEnabled(true);
				PopUpUIStack.Last()->SetVisibility(ESlateVisibility::Visible);
			}
		}
	}
}

URPGWidgetBase* UUIManager::GetTopUI() const
{
	if (PopUpUIStack.Num() > 0)
	{
		return PopUpUIStack.Last(); // TArray는 스택처럼 사용할 수 있음
	}
	return nullptr;
}
