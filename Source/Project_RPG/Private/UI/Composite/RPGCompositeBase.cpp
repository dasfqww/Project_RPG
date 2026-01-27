// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Composite/RPGCompositeBase.h"

void URPGCompositeBase::Collapse()
{
	SetVisibility(ESlateVisibility::Collapsed);
}

void URPGCompositeBase::Expand()
{
	SetVisibility(ESlateVisibility::Visible);
}
