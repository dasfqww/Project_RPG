// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGSkillTooltip.generated.h"

class UImage;
class UTextBlock;
class UTexture2D;
class URPGSkillDefinition;
struct FRPGSkillRuntimeSpec;
struct FRPGSkillRefinementOption;

/** Shared content for skill and refinement hover cards. */
struct PROJECT_RPG_API FRPGSkillTooltipPresentation
{
	FText Title;
	FText Description;
	FText Details;
	FText Status;
	UTexture2D* Icon = nullptr;

	static FRPGSkillTooltipPresentation ForSkill(const URPGSkillDefinition& Definition,
		const FRPGSkillRuntimeSpec& Spec, bool bAcquired, AActor* Source);
	static FRPGSkillTooltipPresentation ForRefinement(const FRPGSkillRefinementOption& Option,
		int32 RequiredLevel, bool bSelected, bool bUnlocked);
};

UCLASS()
class PROJECT_RPG_API URPGSkillTooltip : public URPGWidgetBase
{
	GENERATED_BODY()

public:
	static void UpdateFor(UUserWidget& Host, const FRPGSkillTooltipPresentation& Presentation);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void SetPresentation(const FRPGSkillTooltipPresentation& Presentation);

	UPROPERTY(Transient)
	TObjectPtr<UImage> Icon;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Title;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Description;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Details;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Status;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> IconTexture;

	FText TitleText;
	FText DescriptionText;
	FText DetailsText;
	FText StatusText;
};
