#pragma once

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "GameplayEffect.h"
#include "Interface/PawnUIInterface.h"
#include "RPGTargetDataWireProbe.generated.h"

class URPGAttributeSet;
class UPawnUIComponent;

/** Native HP fixture only. Not a production enemy, player, or authored BP. */
UCLASS(Transient, NotBlueprintable, NotPlaceable)
class ARPGTargetDataDamageTestTarget final : public AActor,
	public IAbilitySystemInterface, public IPawnUIInterface
{
	GENERATED_BODY()
public:
	ARPGTargetDataDamageTestTarget();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual UPawnUIComponent* GetPawnUIComponent() const override;
	virtual UPlayerUIComponent* GetPlayerUIComponent() const override { return nullptr; }
	virtual UNPCUIComponent* GetNPCUIComponent() const override { return nullptr; }
	float Health() const;
	UPROPERTY(Replicated) int32 Peer = -1;
	UPROPERTY(Replicated) bool bFinished = false;
private:
	UPROPERTY() TObjectPtr<URPGAbilitySystemComponent> ASC;
	UPROPERTY() TObjectPtr<URPGAttributeSet> Attributes;
	UPROPERTY() TObjectPtr<UPawnUIComponent> UI;
};

UCLASS(Transient, NotBlueprintable)
class URPGTargetDataDamageTestEffect final : public UGameplayEffect
{
	GENERATED_BODY()
public:
	URPGTargetDataDamageTestEffect();
};

/** Observes the real RPC receiver; never substitutes direct implementation calls. */
UCLASS(Transient, NotBlueprintable)
class URPGTargetDataWireTestASC final : public URPGAbilitySystemComponent
{
	GENERATED_BODY()
public:
	URPGTargetDataWireTestASC();
	int32 ConfirmRequests = 0;
	int32 CancelRequests = 0;
	bool HasCache(FGameplayAbilitySpecHandle Handle, FPredictionKey Key) const;
	int32 GetRequestLimit() const { return MaximumTargetDataRequestsPerSecond; }
	UFUNCTION(Server, Reliable)
	void ServerWireCheckpoint(int32 Case);
	UFUNCTION(Server, Reliable)
	void ServerRateCheckpoint(int32 Stage);
	UFUNCTION(Server, Reliable)
	void ServerDamageReady();
protected:
	virtual void ServerSetReplicatedTargetData_Implementation(FGameplayAbilitySpecHandle Handle,
		FPredictionKey Original, const FGameplayAbilityTargetDataHandle& Data,
		FGameplayTag Tag, FPredictionKey Message) override;
	virtual void ServerSetReplicatedTargetDataCancelled_Implementation(FGameplayAbilitySpecHandle Handle,
		FPredictionKey Original, FPredictionKey Message) override;
};

/** Opt-in, non-placeable wire fixture with the production admission guard. No authored assets change. */
UCLASS(Transient, NotBlueprintable, NotPlaceable)
class ARPGTargetDataWireTestActor final : public AActor, public IAbilitySystemInterface
{
	GENERATED_BODY()
public:
	ARPGTargetDataWireTestActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
	friend class URPGTargetDataWireTestASC;
	UFUNCTION(Client, Reliable)
	void ClientRunCase(int32 Case, FPredictionKey Original, FPredictionKey Previous,
		ARPGTargetDataWireTestActor* Foreign, FGameplayAbilitySpecHandle ForeignHandle, int32 ForeignOriginalValue);
	void Checkpoint(int32 Case);
	UFUNCTION(Client, Reliable)
	void ClientComplete();
	UFUNCTION(Client, Reliable)
	void ClientRunRateStage(int32 Stage, bool bAttacker, FPredictionKey Original);
	UFUNCTION(Client, Reliable)
	void ClientRateComplete();
	void TickRate();
	void BeginRateStage(int32 Stage, bool bAttacker);
	void SubmitRateRequest();
	void RateCheckpoint(int32 Stage);
	void ObserveRateRequest(FGameplayAbilitySpecHandle Handle, FPredictionKey Original,
		FPredictionKey Message, bool bCancellation, const FGameplayAbilityTargetDataHandle* Data);
	void ObserveRateResult();
	void DamageReady();
	void TickDamage();
	bool PrepareClientDamage();
	void ApplyFixtureDamage(const FGameplayAbilityTargetDataHandle& Data);
	void ObserveDamageEffect(UAbilitySystemComponent* Source, const FGameplayEffectSpec& Spec,
		FActiveGameplayEffectHandle Handle);

	void BindActivation();
	void IssueCase();
	void Fail(const TCHAR* Reason);
	void OnTargetData(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag Tag);
	void OnCancellation();
	UPROPERTY()
	TObjectPtr<URPGTargetDataWireTestASC> ASC;
	UPROPERTY() TObjectPtr<URPGAttributeSet> DamageAttributes;
	UPROPERTY(Replicated) TObjectPtr<ARPGTargetDataDamageTestTarget> DamageTarget;
	UPROPERTY(Replicated) bool bDamageReady = false;
	TWeakObjectPtr<ARPGTargetDataDamageTestTarget> ClientDamageTargets[2];
	FDelegateHandle DamageHealthDelegates[2];
	int32 ClientDamageNotifications[2] = { 0, 0 };
	FDelegateHandle DamageEffectDelegate;
	int32 DamageEffects = 0;
	int32 BeforeDamageEffects = 0;
	float BeforeDamageHealth = 0;
	bool bClientDamagePrepared = false;
	bool bServerDamageReadyReceived = false;
	bool bWireCompleteReceived = false;
	bool bDamageFinished = false;
	double DamageQuietUntil = 0;
	UPROPERTY(Replicated)
	FGameplayAbilitySpecHandle GrantedHandle;
	UPROPERTY(Replicated)
	int32 Peer = -1;
	FPredictionKey BoundOriginal;
	FPredictionKey PreviousOriginal;
	FDelegateHandle DataDelegate;
	FDelegateHandle CancelDelegate;
	int32 NextCase = 0;
	int32 Confirmations = 0;
	int32 Cancellations = 0;
	int32 BeforeConfirmRequests = 0;
	int32 BeforeCancelRequests = 0;
	int32 BeforeConfirmations = 0;
	int32 BeforeCancellations = 0;
	double Deadline = 0.0;
	bool bEnabled = false;
	bool bStartedActivation = false;
	bool bAwaitingCheckpoint = false;
	struct FRateWindow
	{
		double Started = 0.0;
		double Ended = 0.0;
		int32 First = 0;
		int32 Requests = 0;
		int32 Confirms = 0;
		int32 Cancels = 0;
		int32 Accepted = 0;
		int32 Cancelled = 0;
	};
	TArray<FRateWindow> RateWindows;
	int32 RateStage = -1;
	int32 RateReceived = 0;
	int32 RateDropped = 0;
	int32 RateBeforeConfirmations = 0;
	int32 RateBeforeCancellations = 0;
	int32 ClientRateSubmitted = 0;
	FPredictionKey ClientRateOriginal;
	double NextRateStageAt = -1.0;
	double ClientRateNextAt = 0.0;
	bool bRateAttacker = false;
	bool bRateStageComplete = false;
	bool bRateChallengeIssued = false;
	bool bExpectedRateCallback = false;
	bool bRateCancellation = false;
	bool bClientRateSending = false;
};

UCLASS(Transient, NotBlueprintable)
class URPGTargetDataWireProbeComponent final : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGTargetDataWireProbeComponent();
protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* TickFunction) override;
};
