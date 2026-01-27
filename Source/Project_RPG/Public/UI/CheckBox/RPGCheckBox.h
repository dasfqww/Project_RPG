// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/CheckBox.h"
#include "Type/RPGEnumTypes.h"
#include "RPGCheckBox.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRPGMuteChangedSignature, bool, bMute, URPGCheckBox*, CheckBox);

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGCheckBox : public UCheckBox
{
	GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable)
    FRPGMuteChangedSignature OnRPGMuteChanged;

protected:
    virtual void SynchronizeProperties() override;

    UFUNCTION()
    void HandleCheckChanged(bool bIsChecked);
              
private:
    UPROPERTY(EditDefaultsOnly, Category = "Type")
    ESoundType SoundType;

public:
    FORCEINLINE ESoundType GetSoundType() const { return SoundType; }
};
