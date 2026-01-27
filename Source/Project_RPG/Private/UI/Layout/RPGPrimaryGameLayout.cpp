// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Layout/RPGPrimaryGameLayout.h"
#include "Manager/UIManager.h"
#include "CommonActivatableWidget.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "UI/MVVM/RPGViewModelBase.h"
#include "View/MVVMView.h"
#include "View/MVVMViewClass.h"
#include "Coro.h"
#include "Awaiters/Asset.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGPrimaryGameLayout)

#include "RPGDebugHelper.h"

URPGPrimaryGameLayout* URPGPrimaryGameLayout::GetPrimaryGameLayoutForPrimaryPlayer(const UObject* WorldContextObject)
{
    return nullptr;
}

URPGPrimaryGameLayout* URPGPrimaryGameLayout::GetPrimaryGameLayout(const APlayerController* PlayerController)
{
    return nullptr;
}

URPGPrimaryGameLayout* URPGPrimaryGameLayout::GetPrimaryGameLayout(const ULocalPlayer* LocalPlayer)
{
    return nullptr;
}

void URPGPrimaryGameLayout::RegisterLayer(FGameplayTag LayerTag, UCommonActivatableWidgetContainerBase* Container)
{
	if (!LayerTag.IsValid())
	{
		Debug::Print("레이어 등록 실패: 유효하지 않은 레이어 태그");
		return;
	}

	if (!Container)
	{
		Debug::Print("레이어 등록 실패: Container가 nullptr입니다");
		return;
	}

	if (Layers.Contains(LayerTag))
	{
		Debug::Print(FString::Printf(TEXT("레이어 '%s'는 이미 등록되어 있습니다"), *LayerTag.ToString()));
		return;
	}

	Layers.Add(LayerTag, Container);
}

UCommonActivatableWidgetContainerBase* URPGPrimaryGameLayout::GetLayerContainer(FGameplayTag LayerTag) const
{
	return Layers.FindRef(LayerTag);
}

UCommonActivatableWidget* URPGPrimaryGameLayout::PushWidgetToLayerStack(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> WidgetClass)
{
	if (UCommonActivatableWidgetContainerBase* Container = GetLayerContainer(LayerTag))
	{
		UCommonActivatableWidget* Widget = Container->AddWidget<UCommonActivatableWidget>(WidgetClass);

		// Widget�� MVVM ���ε��� �ʿ��� ViewModel �ڵ� ����
		SetupViewModelsForWidget(Widget);

		return Widget;
	}
	
	Debug::Print(FString::Printf(TEXT("���� Ǫ�� ����: ���̾� �±� '%s'�� �ش��ϴ� �����̳ʸ� ã�� �� �����ϴ�"), *LayerTag.ToString()));
	return nullptr;
}

TCoroTask<void> URPGPrimaryGameLayout::PushWidgetToLayerStackCoroutine(FGameplayTag LayerTag, TSoftClassPtr<UCommonActivatableWidget> WidgetClass)
{
	if (WidgetClass.IsNull())
	{
		co_return;
	}

	// �񵿱� �ε� (�̹� �ε�Ǿ� ������ �ٷ� ��ȯ)
	if (TSubclassOf<UCommonActivatableWidget> LoadedClass = co_await Coro::Async::LoadClass(this, WidgetClass))
	{
		PushWidgetToLayerStack(LayerTag, LoadedClass);
	}
	else
	{
		Debug::Print(FString::Printf(TEXT("���� Ǫ�� ����: ���� Ŭ���� �ε� ���� '%s'"), *WidgetClass.ToString()));
	}
}

void URPGPrimaryGameLayout::K2_PushWidgetToLayerStackAsync(FGameplayTag LayerTag,
	TSoftClassPtr<UCommonActivatableWidget> WidgetClass)
{
	PushWidgetToLayerStackCoroutine(LayerTag, WidgetClass);
}

void URPGPrimaryGameLayout::PopWidgetFromLayerStack(FGameplayTag LayerTag)
{
	if (UCommonActivatableWidgetContainerBase* Container = GetLayerContainer(LayerTag))
	{
		if (UCommonActivatableWidget* ActiveWidget = Container->GetActiveWidget())
		{
			ActiveWidget->DeactivateWidget();
		}
	}
}

void URPGPrimaryGameLayout::ClearAllLayers()
{
	for (const auto& Pair : Layers)
	{
		if (UCommonActivatableWidgetContainerBase* Container = Pair.Value)
		{
			Container->ClearWidgets();
		}
	}
}

void URPGPrimaryGameLayout::FindAndRemoveWidgetFromLayer(UCommonActivatableWidget* ActivatableWidget)
{
	if (!ActivatableWidget)
	{
		return;
	}
	for (const auto& Pair : Layers)
	{
		if (UCommonActivatableWidgetContainerBase* Container = Pair.Value)
		{
			Container->RemoveWidget(*ActivatableWidget);
		}
	}
}

URPGViewModelBase* URPGPrimaryGameLayout::GetViewModelByName(FName ViewModelName) const
{
	if (const TObjectPtr<URPGViewModelBase>* Found = ViewModels.Find(ViewModelName))
	{
		return *Found;
	}

	return nullptr;
}

void URPGPrimaryGameLayout::SetupViewModelsForWidget(const UCommonActivatableWidget* Widget)
{
	if (!Widget)
	{
		return;
	}

	UMVVMView* View = Widget->GetExtension<UMVVMView>();
	if (!View)
	{
		return;
	}

	const UMVVMViewClass* ViewClass = View->GetViewClass();
	if (!ViewClass)
	{
		return;
	}

	// Widget�� �ʿ�� �ϴ� ViewModel ��� ��ȸ
	for (const FMVVMViewClass_Source& Source : ViewClass->GetSources())
	{
		// ViewModel Ÿ���� ��츸 ó��
		if (!Source.IsViewModel())
		{
			continue;
		}

		FName ViewModelName = Source.GetName();
		UClass* ViewModelClass = Source.GetSourceClass();

		// UCommonViewModelBase ����Ŭ�������� Ȯ��
		if (!ViewModelClass || !ViewModelClass->IsChildOf(URPGViewModelBase::StaticClass()))
		{
			continue;
		}

		// GetOrCreate�� ViewModel ȹ��
		if (URPGViewModelBase* ViewModel = 
			GetOrCreateViewModelByClass(ViewModelName, TSubclassOf<URPGViewModelBase>(ViewModelClass)))
		{
			// Widget�� MVVMView�� ViewModel ����
			View->SetViewModel(ViewModelName, ViewModel);
		}
	}
}

URPGViewModelBase* URPGPrimaryGameLayout::GetOrCreateViewModelByClass(FName ViewModelName,
	TSubclassOf<URPGViewModelBase> ViewModelClass)
{
	if (ViewModelName.IsNone() || !ViewModelClass)
	{
		return nullptr;
	}

	// ĳ�ÿ��� ã��
	if (TObjectPtr<URPGViewModelBase>* Found = ViewModels.Find(ViewModelName))
	{
		return *Found;
	}

	// ���� ���� (Outer = this�� Layout �����ֱ⿡ ����)
	URPGViewModelBase* NewViewModel = NewObject<URPGViewModelBase>(this, ViewModelClass);
	ViewModels.Add(ViewModelName, NewViewModel);

	return NewViewModel;
}


