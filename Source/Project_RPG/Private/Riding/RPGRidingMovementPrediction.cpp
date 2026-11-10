#include "Riding/RPGRidingMovementPrediction.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingDashRootMotion.h"
#include "Riding/RPGRidingDashPrediction.h"
#include "GameFramework/Character.h"

FRPGRidingMoveStamp FRPGRidingMoveStamp::FromState(const FRPGRidingRepState& State)
{
    FRPGRidingMoveStamp Stamp;
    if (!RPGRiding::IsCanonical(State)) return Stamp;
    Stamp.Key = State.Key; Stamp.ProfileVersion = State.Movement.Version; Stamp.ExecutionId = State.ExecutionId;
    return Stamp;
}
bool FRPGRidingMoveStamp::operator==(const FRPGRidingMoveStamp& Other) const
{
    return Key.RiderEpoch == Other.Key.RiderEpoch && Key.StateRevision == Other.Key.StateRevision
        && Key.ActionSequence == Other.Key.ActionSequence && ProfileVersion == Other.ProfileVersion && ExecutionId == Other.ExecutionId;
}
bool FRPGRidingMoveStamp::Serialize(FArchive& Ar)
{
    if (Ar.IsLoading()) *this = {};
    uint8 Bound = Key.RiderEpoch.IsValid(); Ar.SerializeBits(&Bound, 1);
    if (Bound)
    {
        Ar << Key.RiderEpoch << Key.StateRevision << Key.ActionSequence << ProfileVersion;
        uint8 Executing = ExecutionId.IsValid(); Ar.SerializeBits(&Executing, 1);
        if (Executing) Ar << ExecutionId;
        if (!Key.RiderEpoch.IsValid() || Key.StateRevision == 0 || ProfileVersion < 0
            || (Executing && (!ExecutionId.IsValid() || Key.ActionSequence == 0))) Ar.SetError();
    }
    else if (Ar.IsSaving() && (Key.StateRevision || Key.ActionSequence || ProfileVersion || ExecutionId.IsValid())) Ar.SetError();
    return !Ar.IsError();
}

void FSavedMove_RPGRiding::Clear() { FSavedMove_Character::Clear(); Riding = {}; PredictedDashTravelled = 0; }
void FSavedMove_RPGRiding::SetMoveFor(ACharacter* Character, float Delta, const FVector& Accel, FNetworkPredictionData_Client_Character& Data)
{
    FSavedMove_Character::SetMoveFor(Character, Delta, Accel, Data);
    const auto* Component = Character ? Character->FindComponentByClass<URPGRidingComponent>() : nullptr;
    Riding = Component ? FRPGRidingMoveStamp::FromState(Component->GetDashMovementState()) : FRPGRidingMoveStamp{};
    PredictedDashTravelled = Component ? Component->DashPrediction->GetTravelled() : 0;
}
bool FSavedMove_RPGRiding::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const
{
    return !Riding.ExecutionId.IsValid() && NewMove.IsValid() && Riding == static_cast<const FSavedMove_RPGRiding&>(*NewMove).Riding
        && FSavedMove_Character::CanCombineWith(NewMove, Character, MaxDelta);
}
void FSavedMove_RPGRiding::PrepMoveFor(ACharacter* Character)
{
    auto* C = Character ? Character->FindComponentByClass<URPGRidingComponent>() : nullptr;
    if (C)
    {
        // A late correction may contain an older Dash alongside the live prediction.
        // Remove that stale owned type before looking for external preemption.
        const auto Before = C->GetDashMovementState();
        RPGRidingDashMotion::RejectStale(SavedRootMotion, Before);
        RPGRidingDashMotion::Reconcile(*Character, Before);
        C->DashPrediction->Update();
    }
    const auto State = C ? C->GetDashMovementState() : FRPGRidingRepState{};
    RPGRidingDashMotion::RejectStale(SavedRootMotion, State);
    if (Character) RPGRidingDashMotion::Reconcile(*Character, State);
    FSavedMove_Character::PrepMoveFor(Character);
    if (IsValid(Character))
    {
        auto* Latest = Character->FindComponentByClass<URPGRidingComponent>();
        if (Latest)
        {
            const auto Before = Latest->GetDashMovementState();
            RPGRidingDashMotion::RejectStale(SavedRootMotion, Before);
            RPGRidingDashMotion::Reconcile(*Character, Before);
            Latest->DashPrediction->Update(); Latest->DashPrediction->Restore(Riding, PredictedDashTravelled);
        }
        const auto LatestState = Latest ? Latest->GetDashMovementState() : FRPGRidingRepState{};
        RPGRidingDashMotion::RejectStale(SavedRootMotion, LatestState);
        RPGRidingDashMotion::Reconcile(*Character, LatestState);
    }
    // Base resimulation can import a corrected source: filter both sides of that boundary.
}
FSavedMovePtr FPredictionData_Client_RPGRiding::AllocateNewMove() { return FSavedMovePtr(new FSavedMove_RPGRiding()); }

void FRPGRidingNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& Move, ENetworkMoveType Type)
{
    FCharacterNetworkMoveData::ClientFillNetworkMoveData(Move, Type);
    Riding = static_cast<const FSavedMove_RPGRiding&>(Move).Riding;
}
bool FRPGRidingNetworkMoveData::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map, ENetworkMoveType Type)
{
    const bool Base = FCharacterNetworkMoveData::Serialize(Movement, Ar, Map, Type);
    return Base && Riding.Serialize(Ar);
}
FRPGRidingMoveDataContainer::FRPGRidingMoveDataContainer()
{
    NewMoveData = &Moves[0]; PendingMoveData = &Moves[1]; OldMoveData = &Moves[2];
}

void FRPGRidingMoveResponse::ServerFillResponseData(const UCharacterMovementComponent& Movement, const FClientAdjustment& Adjustment)
{
    FCharacterMoveResponseDataContainer::ServerFillResponseData(Movement, Adjustment);
    const auto* Owner = Movement.GetOwner();
    const auto* Component = Owner ? Owner->FindComponentByClass<URPGRidingComponent>() : nullptr;
    State = Component ? Component->GetRidingState() : FRPGRidingRepState{};
    bHasRidingState = IsCorrection() && RPGRiding::IsCanonical(State);
}
bool FRPGRidingMoveResponse::SerializeState(FArchive& Ar, FRPGRidingRepState& Value)
{
    if (Ar.IsLoading()) Value = {};
    if (Ar.IsSaving() && !RPGRiding::IsCanonical(Value)) { Ar.SetError(); return false; }
    Ar << Value.Key.RiderEpoch << Value.Key.StateRevision << Value.Key.ActionSequence;
    uint8 Mode = static_cast<uint8>(Value.Mode); Ar.SerializeBits(&Mode, 1);
    Value.Mode = static_cast<ERPGRidingMode>(Mode);
    if (Value.Mode == ERPGRidingMode::Mounted)
    {
        uint8 Length = static_cast<uint8>(Value.MountId.Len()); Ar << Length;
        if (Length == 0 || Length > 64) { Ar.SetError(); return false; }
        if (Ar.IsLoading()) Value.MountId.Reserve(Length);
        for (int32 Index = 0; Index < Length; ++Index)
        {
            uint8 Char = Ar.IsSaving() ? static_cast<uint8>(Value.MountId[Index]) : 0; Ar << Char;
            if (Ar.IsLoading()) Value.MountId.AppendChar(static_cast<TCHAR>(Char));
        }
        Ar << Value.DefinitionVersion << Value.Movement.Version << Value.Movement.Speed
            << Value.Movement.Acceleration << Value.Movement.Braking << Value.Movement.YawRate;
    }
    uint8 Action = static_cast<uint8>(Value.Action); Ar.SerializeBits(&Action, 2);
    Value.Action = static_cast<ERPGRidingAction>(Action);
    if (Value.Action != ERPGRidingAction::None)
    {
        Ar << Value.ExecutionId << Value.ActionStartServerWorldSeconds << Value.ActionDuration;
        FVector Direction = Value.ActionDirection; Ar << Direction;
        if (Ar.IsLoading()) Value.ActionDirection = Direction;
    }
    uint8 Reason = static_cast<uint8>(Value.LastEndReason); Ar.SerializeBits(&Reason, 4);
    Value.LastEndReason = static_cast<ERPGRidingEndReason>(Reason);
    if (!RPGRiding::IsCanonical(Value)) Ar.SetError();
    return !Ar.IsError();
}
bool FRPGRidingMoveResponse::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map)
{
    if (!FCharacterMoveResponseDataContainer::Serialize(Movement, Ar, Map)) return false;
    uint8 HasState = Ar.IsSaving() && bHasRidingState; Ar.SerializeBits(&HasState, 1);
    if (Ar.IsLoading()) { bHasRidingState = HasState != 0; State = {}; }
    return (!HasState || SerializeState(Ar, State)) && !Ar.IsError();
}
