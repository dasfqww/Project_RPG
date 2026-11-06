#include "Riding/RPGRidingStatePolicy.h"

FRPGRidingDecision RPGRidingStatePolicy::Evaluate(const FRPGRidingEvaluationContext& Context, const FRPGRidingSetCommand& Command)
{
    const auto Reject = [](ERPGRidingResultCode Result) { FRPGRidingDecision Decision; Decision.Result = Result; return Decision; };
    if (!Context.bHasAuthority || Context.bAuthorityRevoked) return Reject(ERPGRidingResultCode::AuthorityRevoked);
    if (!RPGRiding::IsValidCommand(Command) || Command.RiderEpoch != Context.Current.Key.RiderEpoch
        || Command.ExpectedStateRevision != Context.Current.Key.StateRevision) return Reject(ERPGRidingResultCode::StaleState);
    if (!RPGRiding::IsCanonical(Context.Current)) return Reject(ERPGRidingResultCode::NotReady);
    if (!Command.bDesiredMounted)
    {
        auto Decision = Reject(Context.Current.Mode == ERPGRidingMode::OnFoot ? ERPGRidingResultCode::NoChange : ERPGRidingResultCode::Applied);
        if (Decision.Result == ERPGRidingResultCode::Applied) Decision.Transition = ERPGRidingTransition::Dismount;
        return Decision;
    }
    if (!Context.bReady) return Reject(ERPGRidingResultCode::NotReady);
    if (!Context.bAlive || Context.bActionBlocked || Context.bSlowActive) return Reject(ERPGRidingResultCode::ActionBlocked);
    if (!Context.bOnGround) return Reject(ERPGRidingResultCode::WrongMovementMode);
    if (!Context.bCandidateOwned) return Reject(ERPGRidingResultCode::NotOwned);
    if (!Context.bDefinitionCompatible || !RPGRiding::IsValidMountId(Context.CandidateMountId) || Context.CandidateDefinitionVersion <= 0
        || (Command.Selector.Kind == ERPGRidingSelectorKind::Specific && Command.Selector.MountId != Context.CandidateMountId))
        return Reject(ERPGRidingResultCode::DefinitionMismatch);
    if (!Context.bRegionAllowed) return Reject(ERPGRidingResultCode::RegionDenied);
    if (Context.Current.Mode == ERPGRidingMode::Mounted && Context.Current.MountId == Context.CandidateMountId
        && Context.Current.DefinitionVersion == Context.CandidateDefinitionVersion) return Reject(ERPGRidingResultCode::NoChange);
    if (Context.Current.Action != ERPGRidingAction::None) return Reject(ERPGRidingResultCode::Busy);
    if (Context.bInCombat) return Reject(ERPGRidingResultCode::InCombat);
    FRPGRidingDecision Decision;
    Decision.Result = ERPGRidingResultCode::Applied;
    Decision.Transition = Context.Current.Mode == ERPGRidingMode::OnFoot ? ERPGRidingTransition::Mount : ERPGRidingTransition::Replace;
    Decision.ResolvedMountId = Context.CandidateMountId;
    Decision.DefinitionVersion = Context.CandidateDefinitionVersion;
    return Decision;
}
