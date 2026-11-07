#pragma once

#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "Riding/RPGRidingTypes.h"

/** Identifiers only. Never carries a client-selected speed, duration, distance, or permission. */
struct PROJECT_RPG_API FRPGRidingMoveStamp
{
    FRPGRidingStateKey Key;
    int32 ProfileVersion = 0;
    FGuid ExecutionId;
    static FRPGRidingMoveStamp FromState(const FRPGRidingRepState& State);
    bool operator==(const FRPGRidingMoveStamp& Other) const;
    bool Serialize(FArchive& Ar);
};

class PROJECT_RPG_API FSavedMove_RPGRiding : public FSavedMove_Character
{
public:
    FRPGRidingMoveStamp Riding;
    virtual void Clear() override;
    virtual void SetMoveFor(ACharacter* Character, float Delta, const FVector& Accel, FNetworkPredictionData_Client_Character& Data) override;
    virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;
    virtual void PrepMoveFor(ACharacter* Character) override;
};

class PROJECT_RPG_API FPredictionData_Client_RPGRiding : public FNetworkPredictionData_Client_Character
{
public:
    explicit FPredictionData_Client_RPGRiding(const UCharacterMovementComponent& Movement) : Super(Movement) {}
    virtual FSavedMovePtr AllocateNewMove() override;
private:
    using Super = FNetworkPredictionData_Client_Character;
};

struct PROJECT_RPG_API FRPGRidingNetworkMoveData : public FCharacterNetworkMoveData
{
    FRPGRidingMoveStamp Riding;
    virtual void ClientFillNetworkMoveData(const FSavedMove_Character& Move, ENetworkMoveType Type) override;
    virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map, ENetworkMoveType Type) override;
};

struct PROJECT_RPG_API FRPGRidingMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
    FRPGRidingNetworkMoveData Moves[3];
    FRPGRidingMoveDataContainer();
};

struct PROJECT_RPG_API FRPGRidingMoveResponse : public FCharacterMoveResponseDataContainer
{
    FRPGRidingRepState State;
    bool bHasRidingState = false;
    virtual void ServerFillResponseData(const UCharacterMovementComponent& Movement, const FClientAdjustment& Adjustment) override;
    virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map) override;
    /** Bounded state serializer, also exercised directly by native wire-format regression. */
    static bool SerializeState(FArchive& Ar, FRPGRidingRepState& Value);
};
