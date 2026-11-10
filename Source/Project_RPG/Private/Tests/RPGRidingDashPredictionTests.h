#pragma once
#include "Tests/RPGRidingDashMotionTests.h"
#include "GameFramework/PlayerController.h"
#include "RPGRidingDashPredictionTests.generated.h"

/** Native RPC interception, deliberately not a socket/packet-loss test. Real GAS performs activation. */
UCLASS(Transient, NotBlueprintable)
class URPGRidingDashPredictionTestASC : public UAbilitySystemComponent
{
    GENERATED_BODY()
public:
    FPredictionKey SentKey, SentMessageKey;
    FGameplayAbilityTargetDataHandle SentData;
    int32 ActivationRequests = 0, IntentRequests = 0;
    virtual void ServerTryActivateAbility_Implementation(FGameplayAbilitySpecHandle Handle, bool Input, FPredictionKey Key) override;
    virtual void ServerSetReplicatedTargetData_Implementation(FGameplayAbilitySpecHandle Handle, FPredictionKey Original,
        const FGameplayAbilityTargetDataHandle& Data, FGameplayTag Tag, FPredictionKey Message) override;
    virtual void ServerSetReplicatedTargetDataCancelled_Implementation(FGameplayAbilitySpecHandle Handle, FPredictionKey Original, FPredictionKey Message) override;
    virtual void ClientActivateAbilitySucceed_Implementation(FGameplayAbilitySpecHandle Handle, FPredictionKey Key) override;
    virtual void ClientActivateAbilityFailed_Implementation(FGameplayAbilitySpecHandle Handle, int16 Key) override;
    virtual void ClientEndAbility_Implementation(FGameplayAbilitySpecHandle Handle, FGameplayAbilityActivationInfo Info) override;
    virtual void ClientCancelAbility_Implementation(FGameplayAbilitySpecHandle Handle, FGameplayAbilityActivationInfo Info) override;
    void ActivateRemote(FGameplayAbilitySpecHandle Handle, FPredictionKey Key) { Super::ServerTryActivateAbility_Implementation(Handle, false, Key); }
};
UCLASS(Transient, NotBlueprintable)
class ARPGRidingDashPredictionTestController : public APlayerController
{
    GENERATED_BODY()
public:
    bool bTestLocal = true;
    virtual bool IsLocalController() const override
    {
#if WITH_DEV_AUTOMATION_TESTS
        return bTestLocal;
#else
        return Super::IsLocalController();
#endif
    }
};
UCLASS(Transient, NotBlueprintable)
class ARPGRidingDashPredictionTestPawn : public ARPGRidingDashTestPawn
{
    GENERATED_BODY()
public:
    ARPGRidingDashPredictionTestPawn(const FObjectInitializer& I = FObjectInitializer::Get());
    virtual int32 GetFunctionCallspace(UFunction* Function, FFrame* Stack) override;
};
