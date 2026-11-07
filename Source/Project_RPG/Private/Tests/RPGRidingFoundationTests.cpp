#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Riding/RPGRidingTypes.h"
#include "Riding/RPGRidingStatePolicy.h"
#include "Riding/RPGRidingPolicy.h"
#include "Riding/RPGMountDefinition.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace RPGRidingFoundationTests
{
FRPGRidingEvaluationContext Ready()
{
    FRPGRidingEvaluationContext C;
    C.Current.Key.RiderEpoch = FGuid::NewGuid(); C.Current.Key.StateRevision = 1;
    C.bHasAuthority = C.bReady = C.bAlive = C.bOnGround = true;
    C.bCandidateOwned = C.bDefinitionCompatible = C.bRegionAllowed = true;
    C.CandidateMountId = TEXT("mount.horse.brown"); C.CandidateDefinitionVersion = 1;
    return C;
}
FRPGRidingSetCommand Command(const FRPGRidingEvaluationContext& C, bool bMount = true)
{
    FRPGRidingSetCommand R;
    R.RiderEpoch = C.Current.Key.RiderEpoch; R.ExpectedStateRevision = C.Current.Key.StateRevision;
    R.RequestSeq = 1; R.bDesiredMounted = bMount;
    return R;
}
void Mount(FRPGRidingEvaluationContext& C)
{
    C.Current.Mode = ERPGRidingMode::Mounted; C.Current.MountId = C.CandidateMountId;
    C.Current.DefinitionVersion = C.CandidateDefinitionVersion;
    C.Current.Movement = {1, 600, 2048, 2000, 500};
}
void Define(URPGMountDefinition& D)
{
    // Metadata-only fixture: not evidence these assets exist, cook, or animate.
    D.MountId = TEXT("mount.horse.brown"); D.DisplayName = FText::FromString(TEXT("Horse")); D.SaddleSocket = TEXT("Saddle");
    D.Icon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Riding/TestIcon.TestIcon")));
    D.Mesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Riding/TestMesh.TestMesh")));
    D.AnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Game/Riding/TestAnim.TestAnim_C")));
    D.RiderPoseLayer = D.AnimClass;
    D.DashPresentation = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(TEXT("/Game/Riding/TestDash.TestDash")));
    D.EmotePresentation = D.DashPresentation;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRidingValuesTest, "ProjectRPG.Riding.Foundation.Values",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRidingValuesTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Stable lowercase ID"), RPGRiding::IsValidMountId(TEXT("mount.horse_1-brown")));
    for (const FString Id : {FString(), FString(TEXT("Mount.horse")), FString(TEXT(".horse")), FString(TEXT("말")), FString(TEXT("horse/a")), FString::ChrN(65, TEXT('a'))})
        TestFalse(TEXT("Reject malformed ID"), RPGRiding::IsValidMountId(Id));
    TestTrue(TEXT("64 byte ID"), RPGRiding::IsValidMountId(FString::ChrN(64, TEXT('a'))));
    auto C = RPGRidingFoundationTests::Ready();
    auto R = RPGRidingFoundationTests::Command(C);
    TestTrue(TEXT("Selected command"), RPGRiding::IsValidCommand(R));
    R.Selector.MountId = C.CandidateMountId;
    TestFalse(TEXT("Selected cannot hide a specific ID"), RPGRiding::IsValidCommand(R));
    R.Selector.Kind = ERPGRidingSelectorKind::Specific;
    TestTrue(TEXT("Specific command"), RPGRiding::IsValidCommand(R));
    R.bDesiredMounted = false;
    TestFalse(TEXT("Dismount has no selector payload"), RPGRiding::IsValidCommand(R));
    R = RPGRidingFoundationTests::Command(C); R.RequestSeq = 0;
    TestFalse(TEXT("Zero sequence"), RPGRiding::IsValidCommand(R));
    auto Older = C.Current.Key; Older.ActionSequence = 4;
    auto Newer = Older; Newer.StateRevision = 2; Newer.ActionSequence = 0;
    TestTrue(TEXT("State revision precedes action sequence"), RPGRiding::Compare(Newer, Older) == ERPGRidingStateOrder::Newer);
    Newer.RiderEpoch = FGuid::NewGuid();
    TestTrue(TEXT("Never order epochs"), RPGRiding::Compare(Newer, Older) == ERPGRidingStateOrder::Unrelated);
    TestTrue(TEXT("Invalid epoch is not a current state"), RPGRiding::Compare({}, {}) == ERPGRidingStateOrder::Unrelated);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRidingCanonicalTest, "ProjectRPG.Riding.Foundation.CanonicalState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRidingCanonicalTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Empty unbound state"), RPGRiding::IsCanonical({}));
    auto C = RPGRidingFoundationTests::Ready(); RPGRidingFoundationTests::Mount(C);
    TestTrue(TEXT("Mounted with no action"), RPGRiding::IsCanonical(C.Current));
    C.Current.Mode = ERPGRidingMode::OnFoot;
    TestFalse(TEXT("Dismount cannot retain definition"), RPGRiding::IsCanonical(C.Current));
    C.Current.Mode = ERPGRidingMode::Mounted;
    C.Current.Action = ERPGRidingAction::Dash; C.Current.ExecutionId = FGuid::NewGuid(); C.Current.Key.ActionSequence = 1;
    C.Current.ActionDuration = 0.6f; C.Current.ActionDirection = FVector::ForwardVector;
    TestTrue(TEXT("Canonical active dash"), RPGRiding::IsCanonical(C.Current));
    C.Current.ActionDirection = FVector::UpVector;
    TestFalse(TEXT("Dash cannot encode flying direction"), RPGRiding::IsCanonical(C.Current));
    C.Current.Action = ERPGRidingAction::Emote; C.Current.ActionDirection = FVector::ZeroVector;
    TestTrue(TEXT("Emote has no movement"), RPGRiding::IsCanonical(C.Current));
    C.Current.ActionDuration = std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("NaN duration"), RPGRiding::IsCanonical(C.Current));
    C.Current = RPGRidingFoundationTests::Ready().Current;
    C.Current.ActionStartServerWorldSeconds = std::numeric_limits<double>::infinity();
    TestFalse(TEXT("Inactive action cannot retain time"), RPGRiding::IsCanonical(C.Current));
    for (int32 Value = 2; Value < 256; ++Value)
    {
        auto S = RPGRidingFoundationTests::Ready().Current; S.Mode = static_cast<ERPGRidingMode>(Value);
        TestFalse(TEXT("Unknown mode rejected"), RPGRiding::IsCanonical(S));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRidingAdmissionTest, "ProjectRPG.Riding.Foundation.AdmissionGuards",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRidingAdmissionTest::RunTest(const FString& Parameters)
{
    using namespace RPGRidingFoundationTests;
    struct FCase { bool FRPGRidingEvaluationContext::*Flag; bool Value; ERPGRidingResultCode Expected; };
    const FCase Cases[] = {
        {&FRPGRidingEvaluationContext::bHasAuthority, false, ERPGRidingResultCode::AuthorityRevoked},
        {&FRPGRidingEvaluationContext::bAuthorityRevoked, true, ERPGRidingResultCode::AuthorityRevoked},
        {&FRPGRidingEvaluationContext::bReady, false, ERPGRidingResultCode::NotReady},
        {&FRPGRidingEvaluationContext::bAlive, false, ERPGRidingResultCode::ActionBlocked},
        {&FRPGRidingEvaluationContext::bActionBlocked, true, ERPGRidingResultCode::ActionBlocked},
        {&FRPGRidingEvaluationContext::bSlowActive, true, ERPGRidingResultCode::ActionBlocked},
        {&FRPGRidingEvaluationContext::bOnGround, false, ERPGRidingResultCode::WrongMovementMode},
        {&FRPGRidingEvaluationContext::bCandidateOwned, false, ERPGRidingResultCode::NotOwned},
        {&FRPGRidingEvaluationContext::bDefinitionCompatible, false, ERPGRidingResultCode::DefinitionMismatch},
        {&FRPGRidingEvaluationContext::bRegionAllowed, false, ERPGRidingResultCode::RegionDenied},
        {&FRPGRidingEvaluationContext::bInCombat, true, ERPGRidingResultCode::InCombat}};
    for (const auto& Case : Cases)
    {
        auto C = Ready(); C.*(Case.Flag) = Case.Value;
        const auto D = RPGRidingStatePolicy::Evaluate(C, Command(C));
        TestTrue(TEXT("Guard reason"), D.Result == Case.Expected);
        TestTrue(TEXT("Rejected command has no transition or candidate"), D.Transition == ERPGRidingTransition::None && D.ResolvedMountId.IsEmpty());
    }
    auto C = Ready(); auto R = Command(C); R.ExpectedStateRevision++;
    TestTrue(TEXT("Old command rejected"), RPGRidingStatePolicy::Evaluate(C, R).Result == ERPGRidingResultCode::StaleState);
    R = Command(C); R.RiderEpoch = FGuid::NewGuid();
    TestTrue(TEXT("Old pawn rejected"), RPGRidingStatePolicy::Evaluate(C, R).Result == ERPGRidingResultCode::StaleState);
    R = Command(C); R.Selector.Kind = ERPGRidingSelectorKind::Specific; R.Selector.MountId = TEXT("mount.other");
    TestTrue(TEXT("Candidate must match the specific command"), RPGRidingStatePolicy::Evaluate(C, R).Result == ERPGRidingResultCode::DefinitionMismatch);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRidingTransitionTest, "ProjectRPG.Riding.Foundation.Transitions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRidingTransitionTest::RunTest(const FString& Parameters)
{
    using namespace RPGRidingFoundationTests;
    auto C = Ready(); const auto R = Command(C);
    TestTrue(TEXT("New mount"), RPGRidingStatePolicy::Evaluate(C, R).Transition == ERPGRidingTransition::Mount);
    TestTrue(TEXT("Evaluation has no state mutation"), C.Current.Mode == ERPGRidingMode::OnFoot);
    Mount(C); C.bInCombat = true;
    TestTrue(TEXT("Same mount is idempotent"), RPGRidingStatePolicy::Evaluate(C, R).Result == ERPGRidingResultCode::NoChange);
    C.CandidateMountId = TEXT("mount.wolf");
    TestTrue(TEXT("Combat prevents replacement"), RPGRidingStatePolicy::Evaluate(C, R).Result == ERPGRidingResultCode::InCombat);
    C.bInCombat = false;
    TestTrue(TEXT("Replacement"), RPGRidingStatePolicy::Evaluate(C, R).Transition == ERPGRidingTransition::Replace);
    C.Current.Action = ERPGRidingAction::Dash; C.Current.ExecutionId = FGuid::NewGuid(); C.Current.Key.ActionSequence = 1;
    C.Current.ActionDirection = FVector::ForwardVector; C.Current.ActionDuration = 0.6f;
    TestTrue(TEXT("Cannot replace during dash"), RPGRidingStatePolicy::Evaluate(C, R).Result == ERPGRidingResultCode::Busy);
    C.bReady = C.bAlive = C.bOnGround = C.bCandidateOwned = C.bDefinitionCompatible = C.bRegionAllowed = false;
    C.bInCombat = C.bActionBlocked = C.bSlowActive = true;
    TestTrue(TEXT("Dismount bypasses all mounting guards and active dash"), RPGRidingStatePolicy::Evaluate(C, Command(C, false)).Transition == ERPGRidingTransition::Dismount);
    C.bAuthorityRevoked = true;
    TestTrue(TEXT("Revoked RPC still refused; internal cleanup is separate"), RPGRidingStatePolicy::Evaluate(C, Command(C, false)).Result == ERPGRidingResultCode::AuthorityRevoked);
    C = Ready();
    TestTrue(TEXT("Already on foot"), RPGRidingStatePolicy::Evaluate(C, Command(C, false)).Result == ERPGRidingResultCode::NoChange);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRidingAuthoringTest, "ProjectRPG.Riding.Foundation.Authoring",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRidingAuthoringTest::RunTest(const FString& Parameters)
{
    TArray<FText> Errors;
    TStrongObjectPtr<URPGRidingPolicy> Policy(NewObject<URPGRidingPolicy>());
    FRPGRidingPolicyValues Runtime;
    TestFalse(TEXT("Default missing manifest fails closed"), Policy->TryBuildRuntimeValues(Runtime, Errors));
    Policy->Values.GameplayHash = FString::ChrN(64, TEXT('a')); Errors.Reset();
    TestTrue(TEXT("Authored policy"), Policy->TryBuildRuntimeValues(Runtime, Errors));
    TestEqual(TEXT("Fixed initial speed"), Runtime.RideSpeed, 600.f);
    Policy->Values.RideSpeed = 700;
    TestEqual(TEXT("Runtime copy independent of later asset edits"), Runtime.RideSpeed, 600.f);
    for (const float Value : {0.f, -1.f, 10.01f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        Policy->Values.DashCooldown = Value; Errors.Reset();
        TestFalse(TEXT("Cooldown cannot defeat admission protection"), Policy->TryBuildRuntimeValues(Runtime, Errors));
        TestTrue(TEXT("Failed output has no admitted manifest"), Runtime.GameplayHash.IsEmpty());
    }
    TStrongObjectPtr<URPGMountDefinition> A(NewObject<URPGMountDefinition>()), B(NewObject<URPGMountDefinition>());
    RPGRidingFoundationTests::Define(*A); RPGRidingFoundationTests::Define(*B);
    Errors.Reset(); TestTrue(TEXT("Metadata fixture; no synchronous loads"), A->ValidateMetadata(Errors));
    TestEqual(TEXT("Stable asset type"), A->GetPrimaryAssetId().PrimaryAssetType.ToString(), FString(TEXT("Mount")));
    const auto AssetId = A->GetPrimaryAssetId(); A->Rename(TEXT("RenamedMountFixture"));
    TestTrue(TEXT("Asset rename keeps stable identity"), A->GetPrimaryAssetId() == AssetId);
    TArray<const URPGMountDefinition*> Catalog{A.Get(), B.Get()}; Errors.Reset();
    TestFalse(TEXT("Duplicate stable identity"), URPGMountDefinition::ValidateCatalog(Catalog, Errors));
    B->MountId = TEXT("mount.wolf"); Errors.Reset();
    TestTrue(TEXT("Second mount adds data without a new policy"), URPGMountDefinition::ValidateCatalog(Catalog, Errors));
    B->RiderOffset.SetScale3D(FVector(-1, 1, 1)); Errors.Reset();
    TestFalse(TEXT("Negative rider scale"), B->ValidateMetadata(Errors));
    B->MountId = TEXT("INVALID");
    TestFalse(TEXT("Bad ID has no fallback asset identity"), B->GetPrimaryAssetId().IsValid());
    Catalog.Init(A.Get(), 513); Errors.Reset();
    TestFalse(TEXT("Bound catalog size"), URPGMountDefinition::ValidateCatalog(Catalog, Errors));
    return true;
}
#endif
