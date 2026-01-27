// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "GameplayTagContainer.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Coro.h"
#include "RPGPrimaryGameLayout.generated.h"

class APlayerController;
class ULocalPlayer;
class UCommonActivatableWidgetContainerBase;
class UCommonActivatableWidget;
class UAsyncAction_CommonPushWidget;
class URPGViewModelBase;

/**
 * 레이어 기반 UI 스택 레이아웃
 *
 * GameplayTag로 식별되는 여러 레이어를 관리하며, 각 레이어는 활성화 가능한 위젯 스택을 가집니다.
 * 블루프린트에서 레이어를 등록하고, C++ 또는 블루프린트에서 위젯을 푸시/팝할 수 있습니다.
 *
 * 기본 레이어:
 * - Widget.Layer.Game: HUD
 * - Widget.Layer.GameMenu: ESC 메뉴, 인벤토리
 * - Widget.Layer.Menu: 메인 메뉴, 설정
 * - Widget.Layer.Modal: 확인 다이얼로그, 팝업
 */
UCLASS(Abstract, meta = (DisableNativeTick))
class PROJECT_RPG_API URPGPrimaryGameLayout : public URPGWidgetBase
{
	GENERATED_BODY()

	friend class UAsyncAction_CommonPushWidget;

public:
	//-----------------------------------------------------------------------------
	// Static 헬퍼
	//-----------------------------------------------------------------------------

	/** Primary Player의 PrimaryGameLayout을 반환합니다 */
	static URPGPrimaryGameLayout* GetPrimaryGameLayoutForPrimaryPlayer(const UObject* WorldContextObject);

	/** PlayerController의 PrimaryGameLayout을 반환합니다 */
	static URPGPrimaryGameLayout* GetPrimaryGameLayout(const APlayerController* PlayerController);

	/** LocalPlayer의 PrimaryGameLayout을 반환합니다 */
	static URPGPrimaryGameLayout* GetPrimaryGameLayout(const ULocalPlayer* LocalPlayer);

	//-----------------------------------------------------------------------------
	// 레이어 관리
	//-----------------------------------------------------------------------------

	/**
	 * 레이어를 등록합니다
	 * @param LayerTag 레이어 식별 태그
	 * @param Container 레이어에 연결할 위젯 컨테이너
	 */
	UFUNCTION(BlueprintCallable, Category = "Common UI|Layout")
	void RegisterLayer(UPARAM(meta = (Categories = "UI.Layer")) FGameplayTag LayerTag, UCommonActivatableWidgetContainerBase* Container);

	/** 지정된 레이어의 위젯 컨테이너를 반환합니다 */
	UFUNCTION(BlueprintPure, Category = "Common UI|Layout")
	UCommonActivatableWidgetContainerBase* GetLayerContainer(UPARAM(meta = (Categories = "UI.Layer")) FGameplayTag LayerTag) const;

	//-----------------------------------------------------------------------------
	// 위젯 푸시/팝
	//-----------------------------------------------------------------------------

	/**
	 * 레이어에 위젯을 동기적으로 푸시합니다
	 * @param LayerTag 대상 레이어 태그
	 * @param WidgetClass 푸시할 위젯 클래스
	 * @return 생성된 위젯 인스턴스
	 */
	UFUNCTION(BlueprintCallable, Category = "Common UI|Layout", meta = (DeterminesOutputType = "WidgetClass"))
	UCommonActivatableWidget* PushWidgetToLayerStack(UPARAM(meta = (Categories = "UI.Layer")) FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> WidgetClass);

	/**
	 * 레이어에 위젯을 비동기적으로 푸시합니다 
	 * @param LayerTag 대상 레이어 태그
	 * @param WidgetClass 푸시할 위젯 클래스 (Soft Reference)
	 */
	TCoroTask<void> PushWidgetToLayerStackCoroutine(FGameplayTag LayerTag, TSoftClassPtr<UCommonActivatableWidget> WidgetClass);

	/**
	 * 레이어에 위젯을 비동기적으로 푸시합니다 (블루프린트용)
	 * @param LayerTag 대상 레이어 태그
	 * @param WidgetClass 푸시할 위젯 클래스 (Soft Reference)
	 */
	UFUNCTION(BlueprintCallable, Category = "Common UI|Layout", meta = (DisplayName = "Push Widget To Layer Stack Async"))
	void K2_PushWidgetToLayerStackAsync(UPARAM(meta = (Categories = "UI.Layer")) FGameplayTag LayerTag, TSoftClassPtr<UCommonActivatableWidget> WidgetClass);

	/** 레이어 스택 최상단 위젯을 팝합니다 */
	UFUNCTION(BlueprintCallable, Category = "Common UI|Layout")
	void PopWidgetFromLayerStack(UPARAM(meta = (Categories = "UI.Layer")) FGameplayTag LayerTag);

	/** 모든 레이어의 모든 위젯을 제거합니다 */
	UFUNCTION(BlueprintCallable, Category = "Common UI|Layout")
	void ClearAllLayers();

	/** 모든 레이어에서 지정된 위젯을 찾아 제거합니다 */
	UFUNCTION(BlueprintCallable, Category = "Common UI|Layout")
	void FindAndRemoveWidgetFromLayer(UCommonActivatableWidget* ActivatableWidget);

	//-----------------------------------------------------------------------------
	// ViewModel 관리
	//-----------------------------------------------------------------------------
	
	/** ViewModel을 반환합니다 (없으면 nullptr) */
	template<typename T>
	T* GetViewModel(FName ViewModelName) const
	{
		static_assert(TIsDerivedFrom<T, URPGViewModelBase>::Value, "T must derive from UCommonViewModelBase");
		return Cast<T>(GetViewModelByName(ViewModelName));
	}
	
	/** ViewModel을 반환합니다 */
	UFUNCTION(BlueprintPure, Category = "Common UI|ViewModel")
	URPGViewModelBase* GetViewModelByName(FName ViewModelName) const;

	//-----------------------------------------------------------------------------
	// 위젯 푸시 (템플릿)
	//-----------------------------------------------------------------------------

	/** 레이어에 위젯을 푸시합니다 (템플릿 버전) */
	template<typename WidgetT>
	WidgetT* PushWidgetToLayerStack(FGameplayTag LayerTag)
	{
		return PushWidgetToLayerStack<WidgetT>(LayerTag, WidgetT::StaticClass());
	}

	/** 레이어에 위젯을 푸시하고 초기화 함수를 호출합니다 */
	template<typename WidgetT>	
	WidgetT* PushWidgetToLayerStack(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> WidgetClass, TFunctionRef<void(WidgetT&)> InitFunc)
	{
		if (UCommonActivatableWidgetContainerBase* Container = GetLayerContainer(LayerTag))
		{
			return Container->AddWidget<WidgetT>(WidgetClass, InitFunc);
		}
		return nullptr;
	}

protected:
	/** Widget의 MVVMView에 필요한 ViewModel들을 자동으로 설정합니다 */
	void SetupViewModelsForWidget(const UCommonActivatableWidget* Widget);
	
	/** ViewModel을 생성하여 반환합니다 */
	UFUNCTION(BlueprintCallable, Category = "Common UI|ViewModel", meta = (DeterminesOutputType = "ViewModelClass"))
	URPGViewModelBase* GetOrCreateViewModelByClass(FName ViewModelName, TSubclassOf<URPGViewModelBase> ViewModelClass);
	
protected:
	/** 등록된 레이어 맵 */
	UPROPERTY(Transient, meta = (Categories = "UI.Layer"))
	TMap<FGameplayTag, TObjectPtr<UCommonActivatableWidgetContainerBase>> Layers;

	/** ViewModel 캐시 (이름 → 인스턴스) */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<URPGViewModelBase>> ViewModels;
};
