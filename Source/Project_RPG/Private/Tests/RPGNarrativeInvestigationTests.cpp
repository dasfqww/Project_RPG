#if WITH_DEV_AUTOMATION_TESTS
#include "Narrative/RPGNarrativeInteraction.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace RPGInvestigationTests
{
FRPGNarrativeInteractionCommand Observation()
{
    FRPGNarrativeInteractionCommand Result;
    Result.EventId = FGuid::NewGuid(); Result.SessionId = FGuid::NewGuid(); Result.CharacterId = FGuid::NewGuid();
    Result.ConnectionGeneration = 9007199254740993LL; Result.TargetId = TEXT("Scene.Seal01");
    FDateTime::ParseIso8601(TEXT("2026-11-08T01:02:03.456Z"), Result.ObservedAt);
    return Result;
}
FString Receipt(const FRPGNarrativeInteractionCommand& Command, const FString& Revision = TEXT("9223372036854775807"),
    const FString& Goals = TEXT("[]"))
{
    return FString::Printf(TEXT("{\"eventId\":\"%s\",\"characterId\":\"%s\",\"revision\":\"%s\",\"appliedObjectives\":%s}"),
        *Command.EventId.ToString(), *Command.CharacterId.ToString(), *Revision, *Goals);
}
FRPGNarrativeInteractionHttpResult Response(const FString& Body, int32 Code = 200)
{
    FRPGNarrativeInteractionHttpResult Result;
    Result.bCurrentServerContext = true; Result.bTransportSuccessful = true; Result.StatusCode = Code; Result.Body = Body; return Result;
}
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}
using namespace RPGInvestigationTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInvestigationCommandTest, "ProjectRPG.Narrative.Investigation.Command", Flags)
bool FRPGInvestigationCommandTest::RunTest(const FString&)
{
    auto Command = Observation(); TestTrue(TEXT("Valid server observation"), Command.IsValid());
    TSharedPtr<FJsonObject> Object;
    TestTrue(TEXT("Valid wire body"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Command.Encode()), Object));
    if (!Object) return false;
    FString Generation, Timestamp;
    TestTrue(TEXT("Generation uses a string"), Object->TryGetStringField(TEXT("connectionGeneration"), Generation));
    TestEqual(TEXT("Generation retains int64 precision"), Generation, TEXT("9007199254740993"));
    TestTrue(TEXT("Timestamp is supplied"), Object->TryGetStringField(TEXT("observedAtUtc"), Timestamp));
    FDateTime Parsed; TestTrue(TEXT("UTC timestamp roundtrip"), FDateTime::ParseIso8601(*Timestamp, Parsed));
    TestEqual(TEXT("Observation remains fixed"), Parsed, Command.ObservedAt);
    TestEqual(TEXT("No client progress flags or counts"), Object->Values.Num(), 4);
    for (const FString Bad : {FString(), FString(TEXT("a/b")), FString(TEXT("a b")), FString::ChrN(129, 'a')})
    { Command.TargetId = Bad; TestTrue(TEXT("Invalid target cannot dispatch"), Command.Encode().IsEmpty()); }
    Command = Observation(); Command.ConnectionGeneration = 0; TestFalse(TEXT("Confirmed generation required"), Command.IsValid());
    Command = Observation(); Command.SessionId.Invalidate(); TestFalse(TEXT("Assigned session required"), Command.IsValid());
    Command = Observation(); Command.CharacterId.Invalidate(); TestFalse(TEXT("Owner required"), Command.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInvestigationReceiptTest, "ProjectRPG.Narrative.Investigation.Receipt", Flags)
bool FRPGInvestigationReceiptTest::RunTest(const FString&)
{
    const auto Command = Observation();
    TestTrue(TEXT("No-effect receipt is successful"), FRPGNarrativeInteractionAttempt::DecodeReceipt(Receipt(Command), Command));
    TestTrue(TEXT("Parallel goals accepted"), FRPGNarrativeInteractionAttempt::DecodeReceipt(Receipt(Command, TEXT("9007199254740993"),
        TEXT("[{\"questId\":\"Q.Main\",\"objectiveId\":\"Seal\"},{\"questId\":\"Q.Side\",\"objectiveId\":\"Seal\"}]")), Command));
    auto Other = Command; Other.CharacterId = FGuid::NewGuid();
    TestFalse(TEXT("Wrong owner"), FRPGNarrativeInteractionAttempt::DecodeReceipt(Receipt(Other), Command));
    Other = Command; Other.EventId = FGuid::NewGuid();
    TestFalse(TEXT("Wrong event"), FRPGNarrativeInteractionAttempt::DecodeReceipt(Receipt(Other), Command));
    for (const FString Bad : {FString(TEXT("-1")), FString(TEXT("1.0")), FString(TEXT("9223372036854775808")), FString(TEXT(""))})
        TestFalse(TEXT("Revision must be nonnegative int64 text"), FRPGNarrativeInteractionAttempt::DecodeReceipt(Receipt(Command, Bad), Command));
    auto Numeric = Receipt(Command, TEXT("9")).Replace(TEXT("\"revision\":\"9\""), TEXT("\"revision\":9"));
    TestFalse(TEXT("Numeric revision refused"), FRPGNarrativeInteractionAttempt::DecodeReceipt(Numeric, Command));
    for (const FString Goals : {FString(TEXT("null")), FString(TEXT("[null]")), FString(TEXT("[{}]")),
        FString(TEXT("[{\"questId\":\"Q\",\"objectiveId\":\"a/b\"}]")), FString(TEXT("[{\"questId\":1,\"objectiveId\":\"A\"}]")),
        FString(TEXT("[{\"questId\":\"Q\",\"objectiveId\":\"A\"},{\"questId\":\"Q\",\"objectiveId\":\"A\"}]"))})
        TestFalse(TEXT("Malformed or duplicate goals refused"), FRPGNarrativeInteractionAttempt::DecodeReceipt(Receipt(Command, TEXT("1"), Goals), Command));
    FString Many = TEXT("[");
    for (int32 Index = 0; Index < 257; ++Index)
    { if (Index) Many += TEXT(","); Many += FString::Printf(TEXT("{\"questId\":\"Q\",\"objectiveId\":\"A%d\"}"), Index); }
    Many += TEXT("]");
    TestFalse(TEXT("Catalog bound enforced"), FRPGNarrativeInteractionAttempt::DecodeReceipt(Receipt(Command, TEXT("1"), Many), Command));
    TestFalse(TEXT("Response size bounded"), FRPGNarrativeInteractionAttempt::DecodeReceipt(FString::ChrN(256 * 1024 + 1, ' '), Command));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInvestigationRetryTest, "ProjectRPG.Narrative.Investigation.Retry", Flags)
bool FRPGInvestigationRetryTest::RunTest(const FString&)
{
    FRPGNarrativeInteractionAttempt Attempt; const auto Command = Observation();
    TestTrue(TEXT("Begin approval once"), Attempt.Begin(Command)); const FString Frozen = Attempt.GetBody();
    TestFalse(TEXT("Another target cannot replace unresolved event"), Attempt.Begin(Observation()));
    for (int32 Index = 0; Index < 4; ++Index)
    {
        TestTrue(TEXT("Retry budget available"), Attempt.CanAutoRetry());
        TestTrue(TEXT("Request begins"), Attempt.StartRequest()); TestFalse(TEXT("Only one in flight"), Attempt.StartRequest());
        TestEqual(TEXT("Lost response remains unknown"), Attempt.Receive({}), ERPGNarrativeInteractionOutcome::Unknown);
        TestEqual(TEXT("Same body on every attempt"), Attempt.GetBody(), Frozen);
        TestEqual(TEXT("Same observation time"), Attempt.GetCommand()->ObservedAt, Command.ObservedAt);
        TestEqual(TEXT("Bounded exponential delay"), Attempt.RetryDelay(), static_cast<float>(1 << FMath::Clamp(Index, 0, 2)));
    }
    TestFalse(TEXT("Automatic attempts exhausted"), Attempt.CanAutoRetry());
    Attempt.AllowManualRetry(); TestTrue(TEXT("User may retry same event"), Attempt.CanAutoRetry());
    TestEqual(TEXT("Manual retry preserves body"), Attempt.GetBody(), Frozen);
    Attempt.StartRequest(); TestEqual(TEXT("Receipt resolves unknown outcome"), Attempt.Receive(Response(Receipt(Command))), ERPGNarrativeInteractionOutcome::Succeeded);
    TestFalse(TEXT("Resolved attempt released"), Attempt.IsPending());
    TestTrue(TEXT("Next real action can get a new event"), Attempt.Begin(Observation()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInvestigationUnknownTest, "ProjectRPG.Narrative.Investigation.Unknown", Flags)
bool FRPGInvestigationUnknownTest::RunTest(const FString&)
{
    FRPGNarrativeInteractionAttempt Attempt; const auto Command = Observation(); Attempt.Begin(Command);
    for (const int32 Code : {200, 400, 401, 403, 409, 429, 500, 503})
    {
        Attempt.StartRequest();
        TestEqual(TEXT("HTTP errors or malformed success cannot erase an uncertain write"),
            Attempt.Receive(Response(TEXT("{\"error\":\"idempotency_conflict\"}"), Code)), ERPGNarrativeInteractionOutcome::Unknown);
        TestTrue(TEXT("Original event retained"), Attempt.IsPending());
    }
    auto Stale = Response(Receipt(Command)); Stale.bCurrentServerContext = false;
    Attempt.StartRequest(); TestEqual(TEXT("Old server context ignored"), Attempt.Receive(Stale), ERPGNarrativeInteractionOutcome::Unknown);
    auto Other = Command; Other.CharacterId = FGuid::NewGuid();
    Attempt.StartRequest(); TestEqual(TEXT("Other owner's success ignored"), Attempt.Receive(Response(Receipt(Other))), ERPGNarrativeInteractionOutcome::Unknown);
    Attempt.StartRequest(); TestEqual(TEXT("Exact no-effect receipt is final"), Attempt.Receive(Response(Receipt(Command, TEXT("0")))), ERPGNarrativeInteractionOutcome::Succeeded);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInvestigationWindowTest, "ProjectRPG.Narrative.Investigation.Window", Flags)
bool FRPGInvestigationWindowTest::RunTest(const FString&)
{
    FRPGNarrativeInteractionAttempt Attempt; Attempt.Begin(Observation());
    auto Window = Response(TEXT("{\"error\":\"interaction_event_out_of_window\"}"), 409);
    Window.bCurrentServerContext = false; Attempt.StartRequest();
    TestEqual(TEXT("Stale context cannot establish rejection"), Attempt.Receive(Window), ERPGNarrativeInteractionOutcome::Unknown);
    Window.bCurrentServerContext = true; Window.StatusCode = 500; Attempt.StartRequest();
    TestEqual(TEXT("Wrong status cannot establish rejection"), Attempt.Receive(Window), ERPGNarrativeInteractionOutcome::Unknown);
    Window.StatusCode = 409; Attempt.StartRequest();
    TestEqual(TEXT("Typed authoritative window rejection releases event"), Attempt.Receive(Window), ERPGNarrativeInteractionOutcome::Rejected);
    TestFalse(TEXT("No timestamp rebasing or automatic replacement"), Attempt.IsPending());
    return true;
}
#endif
