// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/CheckBox/RPGCheckBox.h"

void URPGCheckBox::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	OnCheckStateChanged.AddDynamic(this, &URPGCheckBox::HandleCheckChanged);
}

void URPGCheckBox::HandleCheckChanged(bool bIsChecked)
{
	OnRPGMuteChanged.Broadcast(bIsChecked, this);
}
