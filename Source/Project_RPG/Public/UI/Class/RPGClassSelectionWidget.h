#pragma once

#include "DataAsset/Definition/RPGGladiatorData.h"
#include "UI/RPGWidgetBase.h"
#include "RPGClassSelectionWidget.generated.h"

class UButton;
class UImage;
class URichTextBlock;
class UTextBlock;
class UVerticalBox;
class URPGClassEntryWidget;
class URPGClassSkillEntryWidget;
class URPGGameplayAbility;

/** D1-compatible class selection panel. */
UCLASS()
class PROJECT_RPG_API URPGClassSelectionWidget : public URPGWidgetBase
{
	GENERATED_BODY()

public:
	URPGClassSelectionWidget(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UFUNCTION()
	void OnExitButtonClicked();

	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<URPGClassEntryWidget> ClassEntryWidgetClass;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UVerticalBox> VerticalBox_ClassElements;

	UPROPERTY(EditDefaultsOnly)
	FKey DeactivateKey;
};

/** One selectable class and its two skill previews. */
UCLASS()
class PROJECT_RPG_API URPGClassEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	URPGClassEntryWidget(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION()
	void InitializeUI(URPGClassSelectionWidget* OwnerWidget, ERPGGladiatorCharacterClass ClassType);

protected:
	UFUNCTION()
	void OnButtonClicked();

	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<URPGClassSkillEntryWidget> SkillEntryWidgetClass;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Class;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_ClassName;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UVerticalBox> VerticalBox_SkillElements;

private:
	UPROPERTY()
	ERPGGladiatorCharacterClass CachedClassType = ERPGGladiatorCharacterClass::Count;

	UPROPERTY()
	TWeakObjectPtr<URPGClassSelectionWidget> CachedOwnerWidget;
};

/** Displays the icon, name, and description serialized on a class skill. */
UCLASS()
class PROJECT_RPG_API URPGClassSkillEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	URPGClassSkillEntryWidget(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	void InitializeUI(const TSubclassOf<URPGGameplayAbility>& AbilityClass);

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Skill;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_SkillName;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<URichTextBlock> Text_SkillDescription;
};
