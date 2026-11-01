#pragma once

#include "Blueprint/UserWidget.h"
#include "UI/Debuffs/RPGDebuffPresentation.h"
#include "RPGPlayerDebuffBarWidget.generated.h"

class UAbilitySystemComponent;
class UHorizontalBox;
class UTextBlock;
class USizeBox;

/** Local player's read-only debuff row. Designed as a child of the Vitals HUD module. */
UCLASS(meta=(DisplayName="Player Debuff Bar"))
class PROJECT_RPG_API URPGPlayerDebuffBarWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** Optional explicit preview/host source; switches off automatic owning-Pawn binding. */
	UFUNCTION(BlueprintCallable, Category="UI|Debuffs") void SetDebuffSource(UAbilitySystemComponent* Source);
	void RefreshPresentation();
	const TArray<FRPGHUDDebuff>& GetDisplayedDebuffs() const { return Displayed; }
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void BeginDestroy() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
	UPROPERTY(EditDefaultsOnly, Category="Debuffs") bool bAutoBindOwningPawn = true;
private:
	struct FEntry { ERPGHUDDebuffKind Kind; TWeakObjectPtr<UTextBlock> Time; };
	void FollowOwner();
	void UnbindController();
	void ApplyPresentation();
	UFUNCTION() void OnOwningPawnChanged(APawn* OldPawn, APawn* NewPawn);
	TWeakObjectPtr<UAbilitySystemComponent> Source;
	TWeakObjectPtr<APlayerController> BoundController;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> DebuffList;
	UPROPERTY(Transient) TArray<TObjectPtr<USizeBox>> CellPool;
	TArray<TWeakObjectPtr<UTextBlock>> TimerPool;
	TArray<FRPGHUDDebuff> Displayed;
	TArray<FEntry> Entries;
	float RefreshElapsed = 0.f;
};
