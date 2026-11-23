#include "UI/Tooltip/RPGSkillTooltip.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "DataTable/SkillData.h"
#include "Engine/Texture2D.h"
#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGSkillExecutionPolicy.h"
#include "Skill/RPGSkillRuntimeTypes.h"

#define LOCTEXT_NAMESPACE "RPGSkillTooltip"

namespace
{
void AddHitDamageLines(TArray<FText>& Lines, const FRPGSkillRuntimeSpec& Spec, AActor* Source)
{
	// Native policy damage events use the runtime spec's authored value for each hit.
	if (!Spec.ExecutionPolicyClass || !FMath::IsFinite(Spec.BaseDamage) || Spec.BaseDamage <= 0.f) return;

	const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Source);
	const FGameplayAttribute AttackAttribute = URPGAttributeSet::GetAttackAttribute();
	if (!ASC || !ASC->HasAttributeSetForAttribute(AttackAttribute)) return;
	const float Attack = ASC->GetNumericAttribute(AttackAttribute);
	if (!FMath::IsFinite(Attack) || Attack < 0.f) return;

	const float ClassMultiplier = Spec.bGreatSwordRageSkill ? Spec.GreatSwordRageDamageMultiplier : 1.f;
	const float ScaledDamage = Spec.ScaleHitDamage(Spec.BaseDamage, ClassMultiplier);
	if (!FMath::IsFinite(ScaledDamage) || ScaledDamage <= 0.f
		|| ScaledDamage > Spec.SecurityProfile.MaximumDamagePerHit) return;

	const double NormalDamage = static_cast<double>(ScaledDamage) * Attack;
	FNumberFormattingOptions Format;
	Format.SetMaximumFractionalDigits(2);
	Lines.Add(Spec.bGreatSwordRageSkill
		? LOCTEXT("RageHitPreview", "타격당 피해 · 대상 방어 적용 전 · 분노 소모 기준")
		: LOCTEXT("HitPreview", "타격당 피해 · 대상 방어 적용 전"));
	Lines.Add(FText::Format(LOCTEXT("NormalHitDamage", "일반 {0}"), FText::AsNumber(NormalDamage, &Format)));
	const FGameplayAttribute CritAttribute = URPGAttributeSet::GetCriticalDamageAttribute();
	if (!ASC->HasAttributeSetForAttribute(CritAttribute)) return;
	const float CritMultiplier = ASC->GetNumericAttribute(CritAttribute);
	if (FMath::IsFinite(CritMultiplier) && CritMultiplier >= 0.f)
	{
		Lines.Add(FText::Format(LOCTEXT("CriticalHitDamage", "치명타 {0} ({1})"),
			FText::AsNumber(NormalDamage * CritMultiplier, &Format), FText::AsPercent(CritMultiplier, &Format)));
	}
}
}

FRPGSkillTooltipPresentation FRPGSkillTooltipPresentation::ForSkill(const URPGSkillDefinition& Definition,
	const FRPGSkillRuntimeSpec& Spec, const bool bAcquired, AActor* Source)
{
	FRPGSkillTooltipPresentation Result;
	Result.Title = Spec.DisplayName.IsEmpty() ? FText::FromName(Definition.SkillTag.GetTagName()) : Spec.DisplayName;
	Result.Icon = Spec.Icon;
	if (const FRPGSkillDataTable* Row = Definition.SkillDataHandle.IsNull() ? nullptr
		: Definition.SkillDataHandle.GetRow<FRPGSkillDataTable>(TEXT("Skill tooltip")))
	{
		Result.Description = Row->ToolTip;
	}

	TArray<FText> Lines;
	if (bAcquired)
	{
		Lines.Add(FText::Format(LOCTEXT("SkillLevel", "스킬 레벨 {0}"), FText::AsNumber(Spec.SkillLevel)));
		AddHitDamageLines(Lines, Spec, Source);
	}
	Lines.Add(FText::Format(LOCTEXT("Cooldown", "재사용 대기시간 {0}초"), FText::AsNumber(Spec.GetCooldownDuration())));
	if (Spec.bGreatSwordRageSkill)
	{
		Lines.Add(FText::Format(LOCTEXT("RageCost", "분노 소모 {0}"), FText::AsNumber(Spec.GreatSwordRageCost)));
	}
	Result.Details = FText::Join(FText::FromString(TEXT("\n")), Lines);
	if (!bAcquired) Result.Status = LOCTEXT("Unlearned", "미습득");
	return Result;
}

FRPGSkillTooltipPresentation FRPGSkillTooltipPresentation::ForRefinement(const FRPGSkillRefinementOption& Option,
	const int32 RequiredLevel, const bool bSelected, const bool bUnlocked)
{
	FRPGSkillTooltipPresentation Result;
	Result.Title = Option.OptionName;
	Result.Description = Option.Description;
	// The option button already owns icon presentation and can resolve the soft reference.
	Result.Icon = Option.OptionIcon.Get();
	if (RequiredLevel > 0)
	{
		Result.Details = FText::Format(LOCTEXT("RequiredLevel", "필요 스킬 레벨 {0}"), FText::AsNumber(RequiredLevel));
	}
	Result.Status = !bUnlocked ? LOCTEXT("Locked", "잠김")
		: bSelected ? LOCTEXT("Selected", "선택됨") : LOCTEXT("NotSelected", "선택 가능");
	return Result;
}

void URPGSkillTooltip::UpdateFor(UUserWidget& Host, const FRPGSkillTooltipPresentation& Presentation)
{
	URPGSkillTooltip* Tooltip = Cast<URPGSkillTooltip>(Host.GetToolTip());
	if (Presentation.Title.IsEmpty() && Presentation.Description.IsEmpty())
	{
		if (Tooltip) Host.SetToolTip(nullptr);
		return;
	}
	if (Tooltip)
	{
		Tooltip->SetPresentation(Presentation);
		if (!Host.GetToolTipText().IsEmpty())
		{
			Host.SetToolTipText(FText::GetEmpty());
			Host.SetToolTip(Tooltip);
		}
		return;
	}
	Tooltip = CreateWidget<URPGSkillTooltip>(&Host, StaticClass());
	if (Tooltip)
	{
		Tooltip->SetPresentation(Presentation);
		Host.SetToolTipText(FText::GetEmpty());
		Host.SetToolTip(Tooltip);
	}
}

namespace
{
void UpdateText(UTextBlock& Widget, const FText& Text)
{
	if (!Widget.GetText().EqualTo(Text)) Widget.SetText(Text);
	Widget.SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}
}

void URPGSkillTooltip::SetPresentation(const FRPGSkillTooltipPresentation& Presentation)
{
	TitleText = Presentation.Title;
	DescriptionText = Presentation.Description;
	DetailsText = Presentation.Details;
	StatusText = Presentation.Status;
	IconTexture = Presentation.Icon;
	if (Title) UpdateText(*Title, TitleText);
	if (Description) UpdateText(*Description, DescriptionText);
	if (Details) UpdateText(*Details, DetailsText);
	if (Status) UpdateText(*Status, StatusText);
	if (Icon)
	{
		if (Icon->GetBrush().GetResourceObject() != IconTexture.Get())
			Icon->SetBrushFromTexture(IconTexture);
		Icon->SetVisibility(IconTexture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

TSharedRef<SWidget> URPGSkillTooltip::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
		Width->SetWidthOverride(320.f);
		Width->SetVisibility(ESlateVisibility::HitTestInvisible);
		WidgetTree->RootWidget = Width;

		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetPadding(FMargin(12.f));
		Panel->SetBrushColor(FLinearColor(0.035f, 0.027f, 0.023f, 0.98f));
		Width->AddChild(Panel);

		UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>();
		Panel->AddChild(Body);
		UHorizontalBox* Heading = WidgetTree->ConstructWidget<UHorizontalBox>();
		Body->AddChildToVerticalBox(Heading);
		Icon = WidgetTree->ConstructWidget<UImage>();
		Icon->SetDesiredSizeOverride(FVector2D(32.f, 32.f));
		UHorizontalBoxSlot* IconSlot = Heading->AddChildToHorizontalBox(Icon);
		IconSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		IconSlot->SetVerticalAlignment(VAlign_Center);

		const auto MakeText = [this](const float Size, const FLinearColor& Color)
		{
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
			FSlateFontInfo Font = Text->GetFont();
			Font.Size = Size;
			Text->SetFont(Font);
			Text->SetColorAndOpacity(FSlateColor(Color));
			Text->SetAutoWrapText(true);
			return Text;
		};
		Title = MakeText(16.f, FLinearColor(1.f, 0.82f, 0.47f));
		UHorizontalBoxSlot* TitleSlot = Heading->AddChildToHorizontalBox(Title);
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleSlot->SetVerticalAlignment(VAlign_Center);
		Description = MakeText(13.f, FLinearColor::White);
		Details = MakeText(12.f, FLinearColor(0.84f, 0.8f, 0.73f));
		Status = MakeText(12.f, FLinearColor(1.f, 0.82f, 0.47f));
		for (UTextBlock* Text : {Description.Get(), Details.Get(), Status.Get()})
		{
			Body->AddChildToVerticalBox(Text)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
		}
		SetPresentation({TitleText, DescriptionText, DetailsText, StatusText, IconTexture.Get()});
	}
	return Super::RebuildWidget();
}

#undef LOCTEXT_NAMESPACE
