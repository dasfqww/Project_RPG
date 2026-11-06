#include "Riding/RPGRidingSession.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    FRPGRidingSessionIdentity Identity() { return {FGuid::NewGuid(), FGuid::NewGuid(), FGuid::NewGuid(), 1}; }
    FRPGRidingPolicyValues Policy()
    {
        FRPGRidingPolicyValues Value;
        Value.GameplayHash = FString::ChrN(64, TEXT('a'));
        return Value;
    }
    FRPGRidingRepState State(const FGuid& Epoch, uint32 Revision = 1)
    {
        FRPGRidingRepState Value;
        Value.Key = {Epoch, Revision, 0};
        return Value;
    }
    FRPGRidingSetCommand Command(const FGuid& Epoch, uint32 Sequence, uint32 Revision = 1)
    {
        FRPGRidingSetCommand Value;
        Value.RiderEpoch = Epoch;
        Value.RequestSeq = Sequence;
        Value.ExpectedStateRevision = Revision;
        return Value;
    }
    FRPGRidingAdmissionSnapshot Snapshot(const FRPGRidingSessionIdentity& Who)
    {
        FRPGRidingAdmissionSnapshot Value;
        Value.Identity = Who;
        Value.RosterId = FGuid::NewGuid();
        Value.OwnershipRevision = 1;
        Value.ProtocolVersion = 1;
        Value.CatalogVersion = 1;
        Value.GameplayHash = Policy().GameplayHash;
        Value.OwnedMountIds = {TEXT("horse"), TEXT("wolf")};
        Value.SelectedMountId = TEXT("horse");
        Value.Favorites = {TEXT("wolf")};
        return Value;
    }
    bool Ready(FRPGRidingSession& Session, const FRPGRidingSessionIdentity& Who, double Now = 100)
    {
        return Session.SetIdentity(Who, Now) && Session.ConfirmAdmission(Who, Policy())
            && Session.ApplySnapshot(Session.BeginSnapshot(Now), Snapshot(Who), Now);
    }
    FRPGRidingRepState Action(const FGuid& Epoch, uint32 Sequence, ERPGRidingAction Kind = ERPGRidingAction::Dash)
    {
        auto Value = State(Epoch, 2);
        Value.Mode = ERPGRidingMode::Mounted;
        Value.MountId = TEXT("horse");
        Value.DefinitionVersion = 1;
        Value.Key.ActionSequence = Sequence;
        Value.Action = Kind;
        Value.ExecutionId = FGuid::NewGuid();
        Value.ActionStartServerWorldSeconds = 1;
        Value.ActionDuration = 0.6f;
        if (Kind == ERPGRidingAction::Dash) Value.ActionDirection = FVector::ForwardVector;
        return Value;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingLedgerReplayTest, "ProjectRPG.Riding.Session.LedgerReplay", Flags)
bool FRidingLedgerReplayTest::RunTest(const FString&)
{
    FRPGRidingRequestLedger Ledger;
    const FGuid Epoch = FGuid::NewGuid();
    Ledger.Reset(Epoch);
    auto Request = Command(Epoch, 1);
    const auto Started = Ledger.Begin(Request);
    TestTrue(TEXT("First request reserves"), Started.Disposition == ERPGRidingRequestDisposition::Reserved);
    const auto Duplicate = Ledger.Begin(Request);
    TestTrue(TEXT("In-flight duplicate waits"), Duplicate.Disposition == ERPGRidingRequestDisposition::Pending);
    TestFalse(TEXT("Duplicate cannot complete original"), Duplicate.Token.Reservation.IsValid());
    auto Changed = Request;
    Changed.ExpectedStateRevision++;
    TestTrue(TEXT("Changed fingerprint rejected"), Ledger.Begin(Changed).Disposition == ERPGRidingRequestDisposition::Rejected);
    const FRPGRidingReceipt First{ERPGRidingResultCode::Applied, State(Epoch, 2)};
    TestTrue(TEXT("Original completes once"), Ledger.Complete(Started.Token, First));
    TestFalse(TEXT("Cannot rewrite receipt"), Ledger.Complete(Started.Token, {ERPGRidingResultCode::Busy, State(Epoch, 3)}));
    const auto Replay = Ledger.Begin(Request);
    TestTrue(TEXT("Replay retains success"), Replay.Disposition == ERPGRidingRequestDisposition::Replayed && Replay.Receipt.Result == ERPGRidingResultCode::Applied);
    TestEqual(TEXT("Replay retains original revision"), Replay.Receipt.State.Key.StateRevision, 2u);
    Request.RequestSeq = 2;
    const auto Pending = Ledger.Begin(Request);
    Ledger.Reset(FGuid::NewGuid());
    TestFalse(TEXT("Late completion cannot cross epoch"), Ledger.Complete(Pending.Token, First));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingLedgerBoundsTest, "ProjectRPG.Riding.Session.LedgerBounds", Flags)
bool FRidingLedgerBoundsTest::RunTest(const FString&)
{
    FRPGRidingRequestLedger Ledger;
    const FGuid Epoch = FGuid::NewGuid();
    Ledger.Reset(Epoch);
    for (uint32 Invalid : {0u, 1026u, MAX_uint32})
        TestTrue(TEXT("Invalid/far sequence rejected"), Ledger.Begin(Command(Epoch, Invalid)).Disposition == ERPGRidingRequestDisposition::Rejected);
    TestEqual(TEXT("Invalid input never raises high water"), Ledger.GetHighWater(), 0u);
    auto Malformed = Command(Epoch, 1);
    Malformed.Selector.MountId = FString::ChrN(10000, TEXT('a'));
    TestTrue(TEXT("Noncanonical input rejected before storage"), Ledger.Begin(Malformed).Disposition == ERPGRidingRequestDisposition::Rejected);
    const auto Active = Ledger.Begin(Command(Epoch, 1));
    for (uint32 Sequence = 2; Sequence <= 70; ++Sequence)
    {
        const auto Start = Ledger.Begin(Command(Epoch, Sequence));
        TestTrue(TEXT("New request reserved with pinned entry"), Start.Disposition == ERPGRidingRequestDisposition::Reserved);
        TestTrue(TEXT("Denial recorded"), Ledger.Complete(Start.Token, {ERPGRidingResultCode::Busy, State(Epoch)}));
    }
    TestTrue(TEXT("Pinned execution survives ring wrap"), Ledger.Complete(Active.Token, {ERPGRidingResultCode::NoChange, State(Epoch)}));
    TestTrue(TEXT("Old evicted sequence never reexecutes"), Ledger.Begin(Command(Epoch, 2)).Disposition == ERPGRidingRequestDisposition::Rejected);
    TestTrue(TEXT("Newest denial replays"), Ledger.Begin(Command(Epoch, 70)).Disposition == ERPGRidingRequestDisposition::Replayed);
    Ledger.Reset(Epoch);
    TestTrue(TEXT("Maximum permitted initial gap"), Ledger.Begin(Command(Epoch, 1025)).Disposition == ERPGRidingRequestDisposition::Reserved);
    for (uint32 Sequence = 1026; Sequence < 1057; ++Sequence) Ledger.Begin(Command(Epoch, Sequence));
    TestTrue(TEXT("All pending slots cannot be evicted"), Ledger.Begin(Command(Epoch, 1057)).Receipt.Result == ERPGRidingResultCode::Busy);
    TestEqual(TEXT("Full ledger does not advance sequence"), Ledger.GetHighWater(), 1056u);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingSessionAdmissionTest, "ProjectRPG.Riding.Session.Admission", Flags)
bool FRidingSessionAdmissionTest::RunTest(const FString&)
{
    FRPGRidingSession Session;
    const auto Who = Identity();
    TestFalse(TEXT("No unauthenticated snapshot request"), Session.BeginSnapshot(100).RequestId.IsValid());
    TestTrue(TEXT("Identity accepted"), Session.SetIdentity(Who, 100));
    TestFalse(TEXT("Identity alone is not admission"), Session.BeginSnapshot(100).RequestId.IsValid());
    TestFalse(TEXT("Wrong admission identity rejected"), Session.ConfirmAdmission(Identity(), Policy()));
    TestTrue(TEXT("Matching admission installs policy"), Session.ConfirmAdmission(Who, Policy()));
    TestFalse(TEXT("Policy hot reload blocked"), Session.ConfirmAdmission(Who, Policy()));
    auto Ticket = Session.BeginSnapshot(100);
    TestTrue(TEXT("First fetch starts"), Ticket.RequestId.IsValid());
    TestFalse(TEXT("No concurrent fetch"), Session.BeginSnapshot(100, true).RequestId.IsValid());
    TestTrue(TEXT("Failure remains local to Riding"), Session.FailSnapshot(Ticket, 100));
    TestFalse(TEXT("Retry waits one second"), Session.BeginSnapshot(100.9).RequestId.IsValid());
    const auto Second = Session.BeginSnapshot(101);
    TestTrue(TEXT("Second fetch starts"), Second.RequestId.IsValid());
    TestFalse(TEXT("Previous callback cannot overwrite retry"), Session.ApplySnapshot(Ticket, Snapshot(Who), 101));
    TestTrue(TEXT("Second failure"), Session.FailSnapshot(Second, 101));
    TestFalse(TEXT("Retry waits two seconds"), Session.BeginSnapshot(102.9).RequestId.IsValid());
    Ticket = Session.BeginSnapshot(103);
    TestTrue(TEXT("Third fetch starts"), Ticket.RequestId.IsValid());
    TestTrue(TEXT("Third failure"), Session.FailSnapshot(Ticket, 103));
    TestFalse(TEXT("Retry waits four seconds"), Session.BeginSnapshot(106.9).RequestId.IsValid());
    Ticket = Session.BeginSnapshot(107);
    TestTrue(TEXT("Final automatic attempt"), Ticket.RequestId.IsValid());
    TestTrue(TEXT("Fourth failure"), Session.FailSnapshot(Ticket, 107));
    TestFalse(TEXT("User retry waits ten seconds"), Session.BeginSnapshot(116, true).RequestId.IsValid());
    TestFalse(TEXT("No periodic fetch after retries"), Session.BeginSnapshot(117).RequestId.IsValid());
    Ticket = Session.BeginSnapshot(117, true);
    TestTrue(TEXT("User retry permitted"), Ticket.RequestId.IsValid());
    TestTrue(TEXT("Snapshot accepted"), Session.ApplySnapshot(Ticket, Snapshot(Who), 117));
    TestTrue(TEXT("Ready only after snapshot"), Session.GetStatus() == ERPGRidingSessionStatus::Ready);
    TestFalse(TEXT("Ready snapshot is immutable during session"), Session.BeginSnapshot(118, true).RequestId.IsValid());
    TestEqual(TEXT("Retries did not renew entry protection"), Session.GetTiming(118).DashRemaining, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingSnapshotValidationTest, "ProjectRPG.Riding.Session.SnapshotValidation", Flags)
bool FRidingSnapshotValidationTest::RunTest(const FString&)
{
    const auto Who = Identity();
    const auto Reject = [this, &Who](FRPGRidingAdmissionSnapshot Bad)
    {
        FRPGRidingSession Session;
        Session.SetIdentity(Who, 100);
        Session.ConfirmAdmission(Who, Policy());
        TestFalse(TEXT("Invalid backend snapshot rejected"), Session.ApplySnapshot(Session.BeginSnapshot(100), Bad, 100));
        FRPGRidingAdmissionSnapshot Out = Snapshot(Who);
        TestFalse(TEXT("No partial snapshot exposed"), Session.CopySnapshot(Out));
        TestEqual(TEXT("Failed read clears output"), Out.OwnedMountIds.Num(), 0);
        TestTrue(TEXT("Invalid response remains retryable"), Session.GetStatus() == ERPGRidingSessionStatus::RetryableFailure);
    };
    auto Bad = Snapshot(Who); Bad.Identity.ConnectionGeneration++; Reject(Bad);
    Bad = Snapshot(Who); Bad.RosterId.Invalidate(); Reject(Bad);
    Bad = Snapshot(Who); Bad.GameplayHash[0] = TEXT('b'); Reject(Bad);
    Bad = Snapshot(Who); Bad.CatalogVersion++; Reject(Bad);
    Bad = Snapshot(Who); Bad.ProtocolVersion++; Reject(Bad);
    Bad = Snapshot(Who); Bad.OwnershipRevision = -1; Reject(Bad);
    Bad = Snapshot(Who); Bad.OwnedMountIds.Add(TEXT("horse")); Reject(Bad);
    Bad = Snapshot(Who); Bad.OwnedMountIds.Add(TEXT("UPPER")); Reject(Bad);
    Bad = Snapshot(Who); Bad.SelectedMountId = TEXT("unowned"); Reject(Bad);
    Bad = Snapshot(Who); Bad.Favorites.Add(TEXT("unowned")); Reject(Bad);
    Bad = Snapshot(Who); Bad.Favorites.Add(TEXT("wolf")); Reject(Bad);
    Bad = Snapshot(Who); Bad.OwnedMountIds.SetNum(513); Reject(Bad);
    Bad = Snapshot(Who); Bad.Favorites.SetNum(33); Reject(Bad);
    FRPGRidingSession Session;
    Session.SetIdentity(Who, 100);
    Session.ConfirmAdmission(Who, Policy());
    auto Ticket = Session.BeginSnapshot(100);
    auto Empty = Snapshot(Who); Empty.OwnedMountIds.Reset(); Empty.Favorites.Reset(); Empty.SelectedMountId.Reset(); Empty.OwnershipRevision = 0;
    TestTrue(TEXT("Empty owned roster grants nothing"), Session.ApplySnapshot(Ticket, Empty, 100));
    Session.SetIdentity(Identity(), 101);
    TestFalse(TEXT("Old identity callback rejected"), Session.ApplySnapshot(Ticket, Snapshot(Who), 101));
    TestTrue(TEXT("New identity requires admission again"), Session.GetStatus() == ERPGRidingSessionStatus::WaitingAdmission);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingSessionRequestsTest, "ProjectRPG.Riding.Session.Requests", Flags)
bool FRidingSessionRequestsTest::RunTest(const FString&)
{
    FRPGRidingSession Session;
    TestTrue(TEXT("Ready fixture"), Ready(Session, Identity()));
    const auto Epoch = Session.BindPawn();
    auto Current = State(Epoch);
    const auto Request = Command(Epoch, 1);
    const auto Start = Session.BeginCommand(Request, Current, 100);
    TestTrue(TEXT("First command executes"), Start.Disposition == ERPGRidingRequestDisposition::Reserved);
    TestTrue(TEXT("Same command cannot reenter"), Session.BeginCommand(Request, Current, 100).Disposition == ERPGRidingRequestDisposition::Pending);
    TestTrue(TEXT("New command during transition caches Busy"), Session.BeginCommand(Command(Epoch, 2), Current, 100).Receipt.Result == ERPGRidingResultCode::Busy);
    Current.Key.StateRevision++;
    TestTrue(TEXT("Commit original result"), Session.CompleteCommand(Start.Token, {ERPGRidingResultCode::Applied, Current}));
    Current.Key.StateRevision++;
    auto Replay = Session.BeginCommand(Request, Current, 100);
    TestTrue(TEXT("Replay before expected revision check"), Replay.Disposition == ERPGRidingRequestDisposition::Replayed && Replay.Receipt.Result == ERPGRidingResultCode::Applied);
    TestEqual(TEXT("Historical state separate from current"), Replay.Receipt.State.Key.StateRevision, 2u);
    TestTrue(TEXT("Cached denial also replays"), Session.BeginCommand(Command(Epoch, 2), Current, 100).Receipt.Result == ERPGRidingResultCode::Busy);
    TestTrue(TEXT("New stale request rejected"), Session.BeginCommand(Command(Epoch, 3), Current, 100).Receipt.Result == ERPGRidingResultCode::StaleState);
    Session.BeginCommand(Command(Epoch, MAX_uint32), Current, 100);
    Session.BeginCommand(Command(Epoch, 0), Current, 100);
    const auto Retryable = Command(Epoch, 4, 3);
    TestTrue(TEXT("Duplicates and invalid requests consume burst"), Session.BeginCommand(Retryable, Current, 100).Receipt.Result == ERPGRidingResultCode::RateLimited);
    const auto Retry = Session.BeginCommand(Retryable, Current, 100.25);
    TestTrue(TEXT("Rate limited sequence retries unchanged"), Retry.Disposition == ERPGRidingRequestDisposition::Reserved);
    TestTrue(TEXT("Retry completes"), Session.CompleteCommand(Retry.Token, {ERPGRidingResultCode::NoChange, Current}));
    const auto NewEpoch = Session.BindPawn();
    TestTrue(TEXT("Rebinding does not refill budget"), Session.BeginCommand(Command(NewEpoch, 1), State(NewEpoch), 100.25).Receipt.Result == ERPGRidingResultCode::RateLimited);
    TestFalse(TEXT("Old completion cannot change new binding"), Session.CompleteCommand(Start.Token, {ERPGRidingResultCode::Applied, Current}));
    TestTrue(TEXT("Old epoch rejected after refill"), Session.BeginCommand(Request, State(NewEpoch), 100.5).Disposition == ERPGRidingRequestDisposition::Rejected);
    FRPGRidingSession Unready;
    Unready.SetIdentity(Identity(), 100);
    const auto UnreadyEpoch = Unready.BindPawn();
    auto Mount = Command(UnreadyEpoch, 1); Mount.bDesiredMounted = true;
    TestTrue(TEXT("Unadmitted mount fails closed"), Unready.BeginCommand(Mount, State(UnreadyEpoch), 100).Receipt.Result == ERPGRidingResultCode::NotReady);
    const auto Dismount = Unready.BeginCommand(Command(UnreadyEpoch, 2), State(UnreadyEpoch), 100);
    TestTrue(TEXT("Manual dismount bypasses snapshot readiness"), Dismount.Disposition == ERPGRidingRequestDisposition::Reserved);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingSessionLifetimeTest, "ProjectRPG.Riding.Session.Lifetime", Flags)
bool FRidingSessionLifetimeTest::RunTest(const FString&)
{
    FRPGRidingSession Session;
    const auto Who = Identity();
    TestTrue(TEXT("Ready fixture"), Ready(Session, Who));
    auto Epoch = Session.BindPawn();
    TestEqual(TEXT("New connection Dash protection"), Session.GetTiming(100).DashRemaining, 10.0);
    TestEqual(TEXT("New connection Emote protection"), Session.GetTiming(100).EmoteRemaining, 2.0);
    TestFalse(TEXT("Entry cooldown prevents dash"), Session.TryReserveApprovedAction(Action(Epoch, 1), 100));
    TestTrue(TEXT("Duplicate identity notification harmless"), Session.SetIdentity(Who, 101));
    TestEqual(TEXT("Identity notification does not renew protection"), Session.GetTiming(101).DashRemaining, 9.0);
    TestTrue(TEXT("First approved dash consumes once"), Session.TryReserveApprovedAction(Action(Epoch, 1), 110));
    TestFalse(TEXT("Same action version cannot consume again"), Session.TryReserveApprovedAction(Action(Epoch, 1), 120));
    TestTrue(TEXT("New action after cooldown"), Session.TryReserveApprovedAction(Action(Epoch, 2), 120));
    TestTrue(TEXT("Hostile outcome starts lock"), Session.RecordApprovedHostileOutcome(121));
    TestTrue(TEXT("Unbind current Pawn"), Session.UnbindPawn(Epoch));
    const auto PreviousEpoch = Epoch;
    Epoch = Session.BindPawn();
    TestFalse(TEXT("Old Pawn cannot unbind replacement"), Session.UnbindPawn(PreviousEpoch));
    TestEqual(TEXT("Pawn replacement preserves dash"), Session.GetTiming(121).DashRemaining, 9.0);
    TestEqual(TEXT("Pawn replacement preserves combat"), Session.GetTiming(121).CombatRemaining, 10.0);
    TestFalse(TEXT("Combat blocks action reservation"), Session.TryReserveApprovedAction(Action(Epoch, 1), 130));
    TestTrue(TEXT("Action after combat ends"), Session.TryReserveApprovedAction(Action(Epoch, 1), 131));
    TestFalse(TEXT("Backward clock cannot authorize"), Session.TryReserveApprovedAction(Action(Epoch, 2), 130));
    TestFalse(TEXT("NaN cannot authorize"), Session.TryReserveApprovedAction(Action(Epoch, 2), std::numeric_limits<double>::quiet_NaN()));
    TestFalse(TEXT("Huge clock cannot erase cooldown"), Session.TryReserveApprovedAction(Action(Epoch, 2), std::numeric_limits<double>::max()));
    TestEqual(TEXT("Rejected clocks preserve timer"), Session.GetTiming(131).DashRemaining, 10.0);
    Session.Revoke();
    TestFalse(TEXT("Revocation discards timing"), Session.GetTiming(131).bValid);
    TestFalse(TEXT("Revocation rejects binding"), Session.BindPawn().IsValid());
    Session.ClearIdentity();
    TestFalse(TEXT("Revoked session cannot revive"), Session.SetIdentity(Identity(), 132));
    TestFalse(TEXT("Revoked session cannot admit"), Session.ConfirmAdmission(Who, Policy()));
    return true;
}
#endif
