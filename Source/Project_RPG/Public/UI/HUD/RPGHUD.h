// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include <Interface/InteractionInterface.h>
#include "RPGHUD.generated.h"

class URPGInteractionWidget;
class URPGMainMenuWidget;
class URPGOptionMenu;
class URPGCombatContributionWidget;
class URPGTrainingGuideWidget;
class URPGActionProgressWidget;
class UUserWidget;

/**
 * �ڵ� �ĺ��� �ʿ�� �����丵 �����Ұ�.
 */
UCLASS()
class PROJECT_RPG_API ARPGHUD : public AHUD
{
	GENERATED_BODY()
public:
	ARPGHUD();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Local player's visual HUD. Created for each possessed Pawn; null disables it. */
	UPROPERTY(EditDefaultsOnly, Category = "Widgets|Player HUD")
	TSubclassOf<UUserWidget> PlayerHUDWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Widgets|Player HUD")
	int32 PlayerHUDZOrder = 0;

	UFUNCTION(BlueprintPure, Category = "UI|Player HUD")
	UUserWidget* GetPlayerHUDWidget() const { return PlayerHUDWidget; }

	UPROPERTY(EditDefaultsOnly, Category = "Widgets")
	TSubclassOf<URPGMainMenuWidget> MainMenuWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Widgets")
	TSubclassOf<class URPGOptionMenu> OptionMenuWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Widgets")
	TSubclassOf<URPGInteractionWidget> InteractionWidgetClass;

	/** Replace with an authored subclass; null disables this display module. */
	UPROPERTY(EditDefaultsOnly, Category = "Widgets|Action Progress")
	TSubclassOf<URPGActionProgressWidget> ActionProgressWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Widgets|Action Progress", meta=(ClampMin="1.0"))
	FVector2D ActionProgressSize = FVector2D(420.f, 160.f);

	UPROPERTY(EditDefaultsOnly, Category = "Widgets|Action Progress", meta=(ClampMin="0.0", ClampMax="1.0"))
	FVector2D ActionProgressAnchor = FVector2D(.5f, .75f);

	UPROPERTY(EditDefaultsOnly, Category = "Widgets|Action Progress", meta=(ClampMin="0.0", ClampMax="1.0"))
	FVector2D ActionProgressAlignment = FVector2D(.5f, 0.f);

	/** Layout units relative to the anchor, without applying a second DPI conversion. */
	UPROPERTY(EditDefaultsOnly, Category = "Widgets|Action Progress")
	FVector2D ActionProgressOffset = FVector2D::ZeroVector;

	UPROPERTY(EditDefaultsOnly, Category = "Widgets|Action Progress")
	int32 ActionProgressZOrder = 5;

	/*UPROPERTY(EditDefaultsOnly, Category = "Widgets")
	TSubclassOf<URPGInteractionWidget> InteractionWidgetClass;*/

	bool bIsMenuVisible;
	bool bIsOptionMenuVisible;

	void DisplayMenu();
	void HideMenu();
	void ToggleMenu();


	/** Widget supplied by the optional options GameFeature, or the default HUD widget. */
	UFUNCTION(BlueprintPure, Category = "UI|Options")
	URPGOptionMenu* GetOptionMenuWidget() const { return OptionMenuWidget; }

	bool SetOptionMenuProvider(UObject* Provider, URPGOptionMenu* Widget);
	void ClearOptionMenuProvider(UObject* Provider, bool bRestoreDefault);
	bool IsOptionMenuProvider(const UObject* Provider, const URPGOptionMenu* Widget) const;

	void DisplayOptionMenu();
	void HideOptionMenu();
	void ToggleOptionMenu();

	void ShowInteractionWidget();
	void HideInteractionWidget();

	//void UpdateInteractionWidget(const FInteractableData* InteractableData);

protected:
	UPROPERTY()
	URPGMainMenuWidget* MainMenuWidget;

	UPROPERTY()
	class URPGOptionMenu* OptionMenuWidget;

	UPROPERTY()
	URPGInteractionWidget* InteractionWidget;

private:
	UPROPERTY(Transient) TObjectPtr<UUserWidget> PlayerHUDWidget;
	TWeakObjectPtr<APlayerController> PlayerHUDController;
	TWeakObjectPtr<APawn> PlayerHUDPawn;
	uint64 PlayerHUDRevision = 0;
	bool bEndingPlayerHUD = false;
	void InitializePlayerHUD();
	void RefreshPlayerHUD();
	void RemovePlayerHUD();
	UFUNCTION() void OnPlayerHUDPawnChanged(APawn* OldPawn, APawn* NewPawn);

	UPROPERTY(Transient) TObjectPtr<URPGCombatContributionWidget> CombatContributionWidget;
	UPROPERTY(Transient) TObjectPtr<URPGTrainingGuideWidget> TrainingGuideWidget;
	void CreateDefaultOptionMenuWidget();

	TWeakObjectPtr<UObject> OptionMenuProvider;
	uint64 OptionMenuProviderRevision = 0;
	bool bEndingOptionsUI = false;

	UPROPERTY(Transient) TObjectPtr<URPGActionProgressWidget> ActionProgressWidget;
	TWeakObjectPtr<APlayerController> ProgressController;
	void CreateActionProgressWidget();
	void RemoveActionProgressWidget();
	UFUNCTION() void OnProgressPawnChanged(APawn* OldPawn, APawn* NewPawn);
};
