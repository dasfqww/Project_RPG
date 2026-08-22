#include "UI/Class/RPGClassSelectionWidget.h"

#include "Ability/RPGAbilitySet.h"
#include "Ability/RPGGameplayAbility.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Player/RPGPlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGClassSelectionWidget)

URPGClassSelectionWidget::URPGClassSelectionWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void URPGClassSelectionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (!VerticalBox_ClassElements || !ClassEntryWidgetClass)
	{
		return;
	}

	VerticalBox_ClassElements->ClearChildren();
	for (int32 ClassIndex = 0;
		ClassIndex < static_cast<int32>(ERPGGladiatorCharacterClass::Count);
		++ClassIndex)
	{
		URPGClassEntryWidget* Entry = CreateWidget<URPGClassEntryWidget>(this, ClassEntryWidgetClass);
		if (Entry)
		{
			Entry->InitializeUI(this, static_cast<ERPGGladiatorCharacterClass>(ClassIndex));
			VerticalBox_ClassElements->AddChild(Entry);
		}
	}
}

FReply URPGClassSelectionWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (DeactivateKey.IsValid() && InKeyEvent.GetKey() == DeactivateKey && !InKeyEvent.IsRepeat())
	{
		DeactivateWidget();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void URPGClassSelectionWidget::OnExitButtonClicked()
{
	DeactivateWidget();
}

URPGClassEntryWidget::URPGClassEntryWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void URPGClassEntryWidget::InitializeUI(
	URPGClassSelectionWidget* OwnerWidget,
	const ERPGGladiatorCharacterClass ClassType)
{
	CachedClassType = ClassType;
	CachedOwnerWidget = OwnerWidget;

	const URPGClassData* ClassData = URPGClassData::GetDefaultClassData();
	const FRPGClassInfoEntry* ClassInfo = ClassData ? ClassData->FindClassInfo(ClassType) : nullptr;
	if (!ClassInfo)
	{
		return;
	}

	if (Text_ClassName)
	{
		Text_ClassName->SetText(ClassInfo->ClassName);
	}

	if (VerticalBox_SkillElements)
	{
		VerticalBox_SkillElements->ClearChildren();
		if (ClassInfo->ClassAbilitySet && SkillEntryWidgetClass)
		{
			const TArray<FRPGAbilitySet_GameplayAbility>& Abilities =
				ClassInfo->ClassAbilitySet->GetGrantedGameplayAbilities();
			for (int32 SkillIndex = 0; SkillIndex < 2 && Abilities.IsValidIndex(SkillIndex); ++SkillIndex)
			{
				URPGClassSkillEntryWidget* SkillEntry =
					CreateWidget<URPGClassSkillEntryWidget>(this, SkillEntryWidgetClass);
				if (SkillEntry)
				{
					SkillEntry->InitializeUI(Abilities[SkillIndex].Ability);
					VerticalBox_SkillElements->AddChild(SkillEntry);
				}
			}
		}
	}

	if (Button_Class)
	{
		Button_Class->OnClicked.AddUniqueDynamic(this, &ThisClass::OnButtonClicked);
	}
}

void URPGClassEntryWidget::OnButtonClicked()
{
	if (ARPGPlayerState* PlayerState = Cast<ARPGPlayerState>(GetOwningPlayerState()))
	{
		PlayerState->ServerSelectClass(CachedClassType);
	}

	if (URPGClassSelectionWidget* OwnerWidget = CachedOwnerWidget.Get())
	{
		OwnerWidget->DeactivateWidget();
	}
}

URPGClassSkillEntryWidget::URPGClassSkillEntryWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void URPGClassSkillEntryWidget::InitializeUI(const TSubclassOf<URPGGameplayAbility>& AbilityClass)
{
	const URPGGameplayAbility* Ability = AbilityClass.GetDefaultObject();
	if (!Ability)
	{
		return;
	}

	if (Image_Skill)
	{
		Image_Skill->SetBrushFromTexture(Ability->GetAbilityIcon());
	}
	if (Text_SkillName)
	{
		Text_SkillName->SetText(Ability->GetAbilityDisplayName());
	}
	if (Text_SkillDescription)
	{
		Text_SkillDescription->SetText(Ability->GetAbilityDescription());
	}
}
