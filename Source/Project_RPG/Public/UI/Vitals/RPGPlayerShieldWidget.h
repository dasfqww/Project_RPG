#pragma once

#include "Blueprint/UserWidget.h"
#include "Component/Combat/RPGShieldComponent.h"
#include "RPGPlayerShieldWidget.generated.h"

class URPGShieldComponent;
class UAbilitySystemComponent;
class UTextBlock;
struct FOnAttributeChangeData;

USTRUCT(BlueprintType)
struct FRPGShieldHUDSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bVisible = false;
	UPROPERTY(BlueprintReadOnly) float Remaining = 0.f;
	UPROPERTY(BlueprintReadOnly) float SecondsRemaining = 0.f;
};

/** Read-only owner shield display. Only the visible countdown needs a cosmetic tick. */
UCLASS(meta=(DisplayName="Player Shield"))
class PROJECT_RPG_API URPGPlayerShieldWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** Explicit previews are allowed without an owner; a player view only accepts its own Pawn. */
	UFUNCTION(BlueprintCallable, Category="UI|Shield") void SetShieldSource(URPGShieldComponent* Source);
	UFUNCTION(BlueprintPure, Category="UI|Shield") FRPGShieldHUDSnapshot GetDisplayedShield() const { return Displayed; }
	void RefreshPresentation();
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void BeginDestroy() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ShieldText;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ShieldTimeText;
private:
	friend struct FRPGPlayerShieldWidgetTestAccess;
	void UnbindSource();
	void BindSource();
	void UnbindController();
	UFUNCTION() void OnOwningPawnChanged(APawn* OldPawn, APawn* NewPawn);
	UFUNCTION() void OnSourceEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
	UFUNCTION() void OnShieldChanged(const FRPGShieldPresentation& State);
	void OnHealthChanged(const FOnAttributeChangeData& Change);
	TWeakObjectPtr<URPGShieldComponent> Source;
	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
	TWeakObjectPtr<AActor> BoundActor;
	TWeakObjectPtr<APlayerController> Controller;
	FDelegateHandle HealthHandle;
	UPROPERTY(Transient) FRPGShieldHUDSnapshot Displayed;
	bool bConstructed = false;
	float RefreshElapsed = 0.f;
};
