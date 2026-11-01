#pragma once

#include "Components/ActorComponent.h"
#include "AI/RPGBossAIComponent.h"
#include "RPGBossTelegraphPresentationComponent.generated.h"

class UDecalComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
struct FStreamableHandle;

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGBossTelegraphStyle
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	FGameplayTag ActionTag;
	/** Presentation only. Must match the server ability's authored radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss", meta = (ClampMin = "1", ClampMax = "2500", Units = "cm"))
	float Radius = 200.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	FLinearColor Color = FLinearColor(1.f, .15f, .02f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	TSoftObjectPtr<UMaterialInterface> Material;
};

/** Local rendering of the Iris snapshot. Never activates abilities or applies damage. */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGBossTelegraphPresentationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGBossTelegraphPresentationComponent();
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Presentation")
	TArray<FRPGBossTelegraphStyle> Styles;

	/** Reads the latest snapshot, including when the observer binds after replication. */
	UFUNCTION(BlueprintCallable, Category = "Boss|Presentation")
	void RefreshPresentation();
	UFUNCTION(BlueprintPure, Category = "Boss|Presentation")
	bool IsIndicatorVisible() const;
	UFUNCTION(BlueprintPure, Category = "Boss|Presentation")
	float GetProgress() const { return Progress; }
	UFUNCTION(BlueprintPure, Category = "Boss|Presentation")
	UDecalComponent* GetIndicator() const { return Indicator; }

	static bool EvaluateProgress(const FRPGBossState& BossState,
		const FRPGBossTelegraphState& Telegraph, double ServerNow, float& OutProgress);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	UPROPERTY(Transient)
	TObjectPtr<URPGBossAIComponent> Boss;
	UPROPERTY(Transient)
	TObjectPtr<UDecalComponent> Indicator;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;
	TSharedPtr<FStreamableHandle> MaterialLoad;
	int32 DisplayedEncounter = 0;
	int32 DisplayedSequence = 0;
	float Progress = 0.f;
	bool bEndingPlay = false;
	void HideIndicator();
	UFUNCTION()
	void OnTelegraphChanged(const FRPGBossTelegraphState& Previous, const FRPGBossTelegraphState& Current);
	UFUNCTION()
	void OnBossStateChanged(const FRPGBossState& Previous, const FRPGBossState& Current);
};
