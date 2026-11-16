#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGLootPresentationComponent.generated.h"

struct FStreamableHandle;
class UNiagaraComponent;
class UNiagaraSystem;
class UStaticMeshComponent;
class USoundBase;
class USoundAttenuation;
class USoundConcurrency;

/** Presentation only; rarity never changes drop probability, ownership or granted stats. */
UENUM(BlueprintType)
enum class ERPGLootRarity : uint8
{
	Common, Uncommon, Rare, Epic, Legendary
};

/** One logical drop. Server clock prevents relevance/asset loading from replaying the toss. */
USTRUCT()
struct FRPGLootFlight
{
	GENERATED_BODY()
	UPROPERTY() FGuid DropId;
	UPROPERTY() FVector Origin = FVector::ZeroVector;
	UPROPERTY() double StartedAt = 0;
	UPROPERTY() float Duration = 0;
	UPROPERTY() float Height = 0;
	UPROPERTY() int32 Seed = 0;
};

/** Local mesh toss + grounded Niagara. The replicated pickup stays at its real landing point. */
UCLASS(ClassGroup=(Items), meta=(BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGLootPresentationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGLootPresentationComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitializeRestingDrop(const FGuid& DropId);
	bool BeginDropFlight(const FGuid& DropId, const FVector& Origin, int32 Seed);
	bool IsInFlight() const;
	void SetPickupAvailable(bool bAvailable);
	void SetFocused(bool bFocused);
	void NotifyCollected();

	static FVector EvaluateArc(const FVector& Start, const FVector& End, float Alpha, float Height);
	static float EvaluateSettle(float SecondsAfterLanding);
	static FName DefaultEffectName(ERPGLootRarity Rarity);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Loot|Presentation")
	bool bEnabled = true;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Loot|Presentation")
	ERPGLootRarity Rarity = ERPGLootRarity::Common;
	/** Empty uses the authored DrapEffet variant for this rarity. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Loot|Presentation")
	TSoftObjectPtr<UNiagaraSystem> GroundEffect;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Loot|Presentation", meta=(ClampMin="0.1", ClampMax="3.0"))
	float EffectScale = 0.6f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Loot|Presentation", meta=(ClampMin="0.2", ClampMax="2.0"))
	float FlightDuration = 0.7f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Loot|Presentation", meta=(ClampMin="0", ClampMax="300"))
	float ArcHeight = 105.f;
	/** Empty uses a rarity-specific cue from Interface_And_Item_Sounds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Loot|Audio")
	TSoftObjectPtr<USoundBase> LandingSound;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Loot|Audio")
	TSoftObjectPtr<USoundBase> CollectedSound;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Loot|Audio")
	TSoftObjectPtr<USoundConcurrency> SoundConcurrency;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Loot|Audio", meta=(ClampMin="0", ClampMax="1"))
	float SoundVolume = 0.55f;

private:
	UPROPERTY(ReplicatedUsing=OnRep_Flight) FRPGLootFlight Flight;
	UFUNCTION() void OnRep_Flight();
	UFUNCTION(NetMulticast, Reliable) void MulticastCollected(FGuid DropId);
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> SourceMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> VisualMesh;
	UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Beam;
	UPROPERTY(Transient) TObjectPtr<USoundAttenuation> SpatialAttenuation;
	TSharedPtr<FStreamableHandle> AssetLoad;
	TSoftObjectPtr<UNiagaraSystem> ResolvedEffect;
	TSoftObjectPtr<USoundBase> ResolvedLandingSound;
	FGuid PresentedDropId;
	FGuid CollectedDropId;
	bool bLanded = false;
	bool bAvailable = true;
	bool bSourceWasVisible = false;

	double ServerTime() const;
	void RefreshDrop();
	void LoadPresentationAssets();
	void UpdatePresentation();
	void Land(bool bPlaySound);
	void ResetPresentation();
	void PlayCue(USoundBase* Sound, float Volume);
};
