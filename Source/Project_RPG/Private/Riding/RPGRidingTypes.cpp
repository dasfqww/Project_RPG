#include "Riding/RPGRidingTypes.h"

bool RPGRiding::IsValidMountId(const FString& Id)
{
    if (Id.IsEmpty() || Id.Len() > 64) return false;
    for (int32 Index = 0; Index < Id.Len(); ++Index)
    {
        const TCHAR C = Id[Index];
        const bool bAlphanumeric = (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('0') && C <= TEXT('9'));
        if (!bAlphanumeric && (Index == 0 || (C != TEXT('.') && C != TEXT('_') && C != TEXT('-')))) return false;
    }
    return true;
}

bool RPGRiding::IsValidCommand(const FRPGRidingSetCommand& Command)
{
    if (!Command.RiderEpoch.IsValid() || Command.RequestSeq == 0 || Command.ExpectedStateRevision == 0) return false;
    if (!Command.bDesiredMounted)
        return Command.Selector.Kind == ERPGRidingSelectorKind::Selected && Command.Selector.MountId.IsEmpty();
    switch (Command.Selector.Kind)
    {
    case ERPGRidingSelectorKind::Specific: return IsValidMountId(Command.Selector.MountId);
    case ERPGRidingSelectorKind::Selected:
    case ERPGRidingSelectorKind::RandomFavorites: return Command.Selector.MountId.IsEmpty();
    default: return false;
    }
}

bool RPGRiding::IsCanonical(const FRPGRidingRepState& State)
{
    if (State.Mode != ERPGRidingMode::OnFoot && State.Mode != ERPGRidingMode::Mounted) return false;
    if (static_cast<uint8>(State.LastEndReason) > static_cast<uint8>(ERPGRidingEndReason::Completed)) return false;
    if (!State.Key.RiderEpoch.IsValid())
    {
        if (State.Key.StateRevision != 0 || State.Key.ActionSequence != 0 || State.Mode != ERPGRidingMode::OnFoot
            || State.LastEndReason != ERPGRidingEndReason::None) return false;
    }
    else if (State.Key.StateRevision == 0) return false;
    if (State.Mode == ERPGRidingMode::OnFoot)
    {
        if (!State.MountId.IsEmpty() || State.DefinitionVersion != 0 || State.Action != ERPGRidingAction::None) return false;
    }
    else if (!IsValidMountId(State.MountId) || State.DefinitionVersion <= 0) return false;
    if (State.Action == ERPGRidingAction::None)
        return !State.ExecutionId.IsValid() && State.ActionStartServerWorldSeconds == 0 && State.ActionDuration == 0
            && State.ActionDirection == FVector::ZeroVector;
    if ((State.Action != ERPGRidingAction::Dash && State.Action != ERPGRidingAction::Emote)
        || !State.ExecutionId.IsValid() || State.Key.ActionSequence == 0 || State.LastEndReason != ERPGRidingEndReason::None
        || !FMath::IsFinite(State.ActionStartServerWorldSeconds) || State.ActionStartServerWorldSeconds < 0
        || !FMath::IsFinite(State.ActionDuration) || State.ActionDuration <= 0 || State.ActionDirection.ContainsNaN()) return false;
    return State.Action == ERPGRidingAction::Emote ? State.ActionDirection == FVector::ZeroVector
        : State.ActionDirection.Z == 0 && State.ActionDirection.IsNormalized();
}

ERPGRidingStateOrder RPGRiding::Compare(const FRPGRidingStateKey& Incoming, const FRPGRidingStateKey& Current)
{
    if (!Incoming.RiderEpoch.IsValid() || Incoming.RiderEpoch != Current.RiderEpoch
        || Incoming.StateRevision == 0 || Current.StateRevision == 0) return ERPGRidingStateOrder::Unrelated;
    if (Incoming.StateRevision != Current.StateRevision)
        return Incoming.StateRevision < Current.StateRevision ? ERPGRidingStateOrder::Older : ERPGRidingStateOrder::Newer;
    if (Incoming.ActionSequence != Current.ActionSequence)
        return Incoming.ActionSequence < Current.ActionSequence ? ERPGRidingStateOrder::Older : ERPGRidingStateOrder::Newer;
    return ERPGRidingStateOrder::Same;
}
