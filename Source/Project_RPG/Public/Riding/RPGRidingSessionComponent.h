#pragma once

#include "Components/ActorComponent.h"
#include "Riding/RPGRidingTypes.h"
#include "RPGRidingSessionComponent.generated.h"

class ARPGPlayerState;
class AController;
class URPGRidingComponent;
class URPGRidingPolicy;
class FRPGRidingSession;
struct FRPGRidingSessionDeleter
{
    void operator()(FRPGRidingSession* Session) const;
};

/** PlayerState-owned connection lifetime. Mutation ports are native and private. */
UCLASS(ClassGroup=(RPG))
class PROJECT_RPG_API URPGRidingSessionComponent final : public UActorComponent
{
    GENERATED_BODY()
public:
    URPGRidingSessionComponent();
    virtual ~URPGRidingSessionComponent() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(BlueprintPure, Category="Riding") FRPGRidingOwnerState GetOwnerState() const { return OwnerState; }

private:
    friend class ARPGPlayerState;
    friend class URPGRidingComponent;
    friend class FRPGRidingDamageScope;
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FRPGRidingActorTestAccess;
    friend struct FRPGRidingDashTestAccess;
    friend struct FRPGRidingEmoteTestAccess;
#endif
    void UpdateAuthenticatedContext(const FString& CharacterId, const FString& SteamId,
        const FString& SessionId, int64 Generation, bool bRevoked, bool bAdmissionConfirmed);
    bool IsContextCurrent() const;
    FGuid Bind(URPGRidingComponent& PawnComponent);
    void Unbind(URPGRidingComponent& PawnComponent, const FGuid& Epoch);
    void PublishOwnerState();
    void Invalidate(bool bRevoke);
    TUniquePtr<FRPGRidingSession, FRPGRidingSessionDeleter> Runtime;
    TWeakObjectPtr<AController> BoundController;
    TWeakObjectPtr<URPGRidingComponent> BoundPawn;
    FString BoundCharacter, BoundSteam, BoundSession;
    int64 BoundGeneration = 0;
    FGuid BoundEpoch;
    FGuid AuthorityContext;
    TSet<FGuid> PendingDamageScopes;
    TFunction<bool()> ValidateAdmission;
    UPROPERTY(EditDefaultsOnly, Category="Riding") TObjectPtr<URPGRidingPolicy> Policy;
    UPROPERTY(Replicated) FRPGRidingOwnerState OwnerState;
};
