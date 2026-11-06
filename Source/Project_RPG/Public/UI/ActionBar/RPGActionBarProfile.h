#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "RPGActionBarProfile.generated.h"

class URPGActionBarWidget;
class UTexture2D;

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGActionBarEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName ActionId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FText Label;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UTexture2D> Icon;
    /** Optional existing combat-slot input. INDEX_NONE uses provider-owned input only. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="-1", ClampMax="7")) int32 SkillSlot = INDEX_NONE;
    /** Optional existing ability-input tag. Additional contexts may call PressAction directly. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FGameplayTag InputTag;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGActionBarActionView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bAvailable = false;
    UPROPERTY(BlueprintReadOnly) FText KeyLabel;
    UPROPERTY(BlueprintReadOnly) float CooldownRemaining = 0.f;
    UPROPERTY(BlueprintReadOnly) float CooldownDuration = 0.f;
};

/** Immutable authoring data; never owns grants, cooldowns or saved class loadouts. */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGActionBarProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName ProfileId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FRPGActionBarEntry> Actions;
    /** Null selects the native action bar. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<URPGActionBarWidget> LeftWidgetClass;
    /** Null hides the class identity region while this profile is active. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<URPGActionBarWidget> CenterWidgetClass;
    bool IsValidProfile() const;
};
