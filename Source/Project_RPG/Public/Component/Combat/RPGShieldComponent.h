#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/RPGApprovedHitContext.h"
#include "RPGShieldComponent.generated.h"

class UAbilitySystemComponent;
struct FOnAttributeChangeData;

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGShieldPresentation
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) float Remaining = 0.f;
	/** Authoritative world time, not a duration to restart on replication. */
	UPROPERTY(BlueprintReadOnly) float ExpiresAt = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGShieldChanged, const FRPGShieldPresentation&, State);

/** Transient, authority-owned damage absorption. No item IDs, persistence, client mutation or Tick. */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGShieldComponent final : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGShieldComponent();
	static URPGShieldComponent* FindOrCreate(AActor* Owner);
	bool GrantOrRefresh(const FGuid& Source, float HealthFraction, float DurationSeconds);
	void RemoveSource(const FGuid& Source);
	void ClearShields();
	/** Returns the part still payable from health. Direct health changes never call this. */
	float AbsorbDamage(float Damage);
	UFUNCTION(BlueprintPure, Category = "RPG|Shield") FRPGShieldPresentation GetShieldState() const { return State; }
	UPROPERTY(BlueprintAssignable, Category = "RPG|Shield") FRPGShieldChanged OnShieldChanged;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
private:
	struct FEntry
	{
		float Remaining = 0.f, Fraction = 0.f, Expires = 0.f;
		FRPGApprovedHitContext Lifetime;
	};
	TMap<FGuid, FEntry> Entries;
	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
	FDelegateHandle HealthChanged, MaximumChanged;
	FTimerHandle ExpiryTimer;
	bool BindAttributes();
	void Refresh();
	void HandleHealthChanged(const FOnAttributeChangeData& Change);
	void HandleMaximumChanged(const FOnAttributeChangeData& Change);
	UFUNCTION() void OnRep_State();
	UPROPERTY(ReplicatedUsing = OnRep_State) FRPGShieldPresentation State;
};
