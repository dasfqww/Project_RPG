#pragma once
#include "Tests/RPGRidingActorTests.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Riding/RPGRidingDashMotionHost.h"
#include "RPGRidingDashMotionTests.generated.h"

/** Engine CMC with the production runtime hooks and a deliberately synthetic security-window adapter. */
UCLASS(Transient, NotBlueprintable)
class URPGRidingDashTestMovement : public UCharacterMovementComponent, public IRPGRidingDashMotionHost
{
    GENERATED_BODY()
public:
    virtual bool CanOpenRidingDashWindow() const override { return !Window.IsValid(); }
    virtual FRPGRidingDashWindow OpenRidingDashWindow(float Duration, float Distance) override;
    virtual void PerformMovement(float Delta) override;
    virtual float GetMaxSpeed() const override;
    virtual FVector ConstrainInputAcceleration(const FVector& Input) const override;
    virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
    void RemapTestServerSources(FRootMotionSourceGroup& Group, float Time) { ConvertRootMotionServerIDsToLocalIDs(CurrentRootMotion, Group, Time); }
#if WITH_DEV_AUTOMATION_TESTS
    void StepTestAutonomous(float Time, float Delta) { MoveAutonomous(Time, Delta, 0, FVector::ZeroVector); }
#endif
    FGuid Window;
    int32 Opened = 0, Closed = 0;
    float Duration = 0, Distance = 0;
    TFunction<void()> OnOpen;
protected:
    virtual bool MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& Rotation, bool Sweep,
        FHitResult* Hit = nullptr, ETeleportType Teleport = ETeleportType::None) override;
};

UCLASS(Transient, NotBlueprintable)
class ARPGRidingDashTestPawn : public ACharacter, public IAbilitySystemInterface
{
    GENERATED_BODY()
public:
    ARPGRidingDashTestPawn(const FObjectInitializer& Initializer = FObjectInitializer::Get());
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return ASC; }
    UPROPERTY() TObjectPtr<UAbilitySystemComponent> ASC;
    UPROPERTY() TObjectPtr<URPGRidingTestAttributes> Attributes;
};

#if WITH_DEV_AUTOMATION_TESTS
void RPGUnprotectRidingDashTestSession(class URPGRidingSessionComponent& Session);
#endif
