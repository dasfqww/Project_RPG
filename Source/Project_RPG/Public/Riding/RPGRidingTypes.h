#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "RPGRidingTypes.generated.h"

UENUM(BlueprintType)
enum class ERPGRidingMode : uint8 { OnFoot, Mounted };
UENUM(BlueprintType)
enum class ERPGRidingAction : uint8 { None, Dash, Emote };
UENUM(BlueprintType)
enum class ERPGRidingSelectorKind : uint8 { Specific, Selected, RandomFavorites };
UENUM(BlueprintType)
enum class ERPGRidingResultCode : uint8
{
    Applied, NoChange, NotOwned, NotReady, DefinitionMismatch, InCombat, ActionBlocked,
    WrongMovementMode, RegionDenied, Busy, StaleState, RateLimited, AuthorityRevoked
};
UENUM(BlueprintType)
enum class ERPGRidingEndReason : uint8
{
    None, Manual, Replacement, Hit, Control, Region, MovementMode, Death,
    AuthorityRevoked, ContextLost, ExecutionRejected, Completed
};
enum class ERPGRidingStateOrder : uint8 { Unrelated, Older, Same, Newer };

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGRidingStateKey
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FGuid RiderEpoch;
    UPROPERTY() uint32 StateRevision = 0;
    UPROPERTY() uint32 ActionSequence = 0;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGRidingSelector
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ERPGRidingSelectorKind Kind = ERPGRidingSelectorKind::Selected;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString MountId;
};

/** Command data only. Does not provide an RPC or grant authority to its caller. */
USTRUCT()
struct PROJECT_RPG_API FRPGRidingSetCommand
{
    GENERATED_BODY()
    UPROPERTY() FGuid RiderEpoch;
    UPROPERTY() uint32 RequestSeq = 0;
    UPROPERTY() uint32 ExpectedStateRevision = 0;
    UPROPERTY() bool bDesiredMounted = false;
    UPROPERTY() FRPGRidingSelector Selector;
};

/** Immutable admitted locomotion values, replicated atomically with the Riding state. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGRidingMovementProfile
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 Version = 0;
    UPROPERTY(BlueprintReadOnly) float Speed = 0;
    UPROPERTY(BlueprintReadOnly) float Acceleration = 0;
    UPROPERTY(BlueprintReadOnly) float Braking = 0;
    UPROPERTY(BlueprintReadOnly) float YawRate = 0;
    bool IsValid() const;
    bool IsEmpty() const;
};

/** Canonical authoritative state. No client setter; observer/owner consume the same admitted profile. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGRidingRepState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FRPGRidingStateKey Key;
    UPROPERTY(BlueprintReadOnly) ERPGRidingMode Mode = ERPGRidingMode::OnFoot;
    UPROPERTY(BlueprintReadOnly) FString MountId;
    UPROPERTY(BlueprintReadOnly) int32 DefinitionVersion = 0;
    UPROPERTY(BlueprintReadOnly) ERPGRidingAction Action = ERPGRidingAction::None;
    UPROPERTY(BlueprintReadOnly) FGuid ExecutionId;
    UPROPERTY(BlueprintReadOnly) double ActionStartServerWorldSeconds = 0;
    UPROPERTY(BlueprintReadOnly) float ActionDuration = 0;
    UPROPERTY() FVector_NetQuantizeNormal ActionDirection{FVector::ZeroVector};
    UPROPERTY(BlueprintReadOnly) ERPGRidingEndReason LastEndReason = ERPGRidingEndReason::None;
    UPROPERTY(BlueprintReadOnly) FRPGRidingMovementProfile Movement;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGRidingOwnerState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bReady = false;
    UPROPERTY(BlueprintReadOnly) double DashEndServerSeconds = 0;
    UPROPERTY(BlueprintReadOnly) double EmoteEndServerSeconds = 0;
    UPROPERTY(BlueprintReadOnly) double CombatEndServerSeconds = 0;
};

USTRUCT()
struct PROJECT_RPG_API FRPGRidingCommandResult
{
    GENERATED_BODY()
    UPROPERTY() uint32 RequestSeq = 0;
    UPROPERTY() ERPGRidingResultCode Result = ERPGRidingResultCode::NotReady;
    UPROPERTY() FRPGRidingRepState HistoricalState;
    UPROPERTY() FRPGRidingRepState LatestState;
};

namespace RPGRiding
{
    inline constexpr int32 MaxMountDefinitions = 512;
    inline constexpr int32 MaxFavorites = 32;
    PROJECT_RPG_API bool IsValidMountId(const FString& Id);
    PROJECT_RPG_API bool IsValidCommand(const FRPGRidingSetCommand& Command);
    PROJECT_RPG_API bool IsCanonical(const FRPGRidingRepState& State);
    /** Different or invalid epochs are unrelated, never ordered by GUID. */
    PROJECT_RPG_API ERPGRidingStateOrder Compare(const FRPGRidingStateKey& Incoming, const FRPGRidingStateKey& Current);
}
