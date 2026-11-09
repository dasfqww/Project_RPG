#if WITH_DEV_AUTOMATION_TESTS
#include "Narrative/RPGNarrativeKill.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include <limits>

namespace RPGNarrativeKillTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
FRPGNarrativeKillCommand Command()
{
    FRPGNarrativeKillCommand C;
    C.SessionId = FGuid::NewGuid(); C.CharacterId = FGuid::NewGuid(); C.EventId = FGuid::NewGuid();
    C.EnemyId = TEXT("Enemy.Wolf"); C.ConnectionGeneration = 9007199254740993LL; C.ObservedAt = FDateTime::UtcNow();
    return C;
}
FString Receipt(const FRPGNarrativeKillCommand& C, const FString& Goals)
{
    return FString::Printf(TEXT(R"({"eventId":"%s","characterId":"%s","revision":"9007199254740993","appliedObjectives":%s})"),
        *C.EventId.ToString(), *C.CharacterId.ToString(), *Goals);
}
FString Goal(const FString& Count = TEXT("1"), const FString& Required = TEXT("3"))
{
    return FString::Printf(TEXT(R"({"questId":"Q.Hunt","objectiveId":"wolves","count":%s,"requiredCount":%s})"), *Count, *Required);
}
}
using namespace RPGNarrativeKillTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeKillCommandTest, "ProjectRPG.Narrative.Kill.Command", Flags)
bool FRPGNarrativeKillCommandTest::RunTest(const FString&)
{
    auto C = Command(); const FString Frozen = C.Encode();
    TSharedPtr<FJsonObject> Body;
    TestTrue(TEXT("JSON encoded"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Frozen), Body));
    if (!Body) return false;
    TestEqual(TEXT("Exact int64 string"), Body->GetStringField(TEXT("connectionGeneration")), FString(TEXT("9007199254740993")));
    TestEqual(TEXT("No caller count or target field"), Body->Values.Num(), 4);
    TestEqual(TEXT("Enemy ID"), Body->GetStringField(TEXT("enemyId")), C.EnemyId);
    FRPGNarrativeKillAttempt A;
    TestTrue(TEXT("Begin"), A.Begin(C));
    C.EnemyId = TEXT("Changed"); C.ConnectionGeneration++; C.ObservedAt += FTimespan::FromSeconds(1);
    TestEqual(TEXT("Caller changes cannot rebuild approved body"), A.GetBody(), Frozen);
    TestFalse(TEXT("Pending observation cannot be replaced"), A.Begin(C));
    auto Invalid = Command(); Invalid.EnemyId = TEXT("../wolf"); TestFalse(TEXT("Invalid logical key"), Invalid.IsValid());
    Invalid = Command(); Invalid.EventId.Invalidate(); TestTrue(TEXT("Invalid event encodes nothing"), Invalid.Encode().IsEmpty());
    Invalid = Command(); Invalid.ConnectionGeneration = 0; TestFalse(TEXT("Generation required"), Invalid.IsValid());
    Invalid = Command(); Invalid.ObservedAt = FDateTime(0); TestFalse(TEXT("Observation time required"), Invalid.IsValid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeKillReceiptTest, "ProjectRPG.Narrative.Kill.Receipt", Flags)
bool FRPGNarrativeKillReceiptTest::RunTest(const FString&)
{
    const auto C = Command();
    TestTrue(TEXT("Partial receipt"), FRPGNarrativeKillAttempt::DecodeReceipt(Receipt(C, TEXT("[") + Goal() + TEXT("]")), C));
    TestTrue(TEXT("No-effect is still a receipt"), FRPGNarrativeKillAttempt::DecodeReceipt(Receipt(C, TEXT("[]")), C));
    TestTrue(TEXT("Bounded complete count"), FRPGNarrativeKillAttempt::DecodeReceipt(Receipt(C, TEXT("[") + Goal(TEXT("10000"), TEXT("10000")) + TEXT("]")), C));
    for (const FString Invalid : {TEXT("0"), TEXT("-1"), TEXT("4"), TEXT("1.5"), TEXT("null"), TEXT("\"1\"")})
        TestFalse(TEXT("Reject invalid/string count"), FRPGNarrativeKillAttempt::DecodeReceipt(Receipt(C, TEXT("[") + Goal(Invalid) + TEXT("]")), C));
    for (const FString Invalid : {TEXT("0"), TEXT("10001"), TEXT("2.5"), TEXT("null"), TEXT("\"3\"")})
        TestFalse(TEXT("Reject required count"), FRPGNarrativeKillAttempt::DecodeReceipt(Receipt(C, TEXT("[") + Goal(TEXT("1"), Invalid) + TEXT("]")), C));
    const FString Good = Receipt(C, TEXT("[") + Goal() + TEXT("]"));
    TestFalse(TEXT("Missing numeric field"), FRPGNarrativeKillAttempt::DecodeReceipt(Good.Replace(TEXT(",\"count\":1"), TEXT("")), C));
    TestFalse(TEXT("Duplicate objective pair"), FRPGNarrativeKillAttempt::DecodeReceipt(Receipt(C, TEXT("[") + Goal() + TEXT(",") + Goal() + TEXT("]")), C));
    auto Other = C; Other.EventId = FGuid::NewGuid();
    TestFalse(TEXT("Different event"), FRPGNarrativeKillAttempt::DecodeReceipt(Good, Other));
    Other = C; Other.CharacterId = FGuid::NewGuid();
    TestFalse(TEXT("Different owner"), FRPGNarrativeKillAttempt::DecodeReceipt(Good, Other));
    TestFalse(TEXT("Overflow revision"), FRPGNarrativeKillAttempt::DecodeReceipt(Good.Replace(TEXT("9007199254740993"), TEXT("9223372036854775808")), C));
    TestFalse(TEXT("Truncated body"), FRPGNarrativeKillAttempt::DecodeReceipt(Good.Left(Good.Len() - 1), C));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeKillRetryTest, "ProjectRPG.Narrative.Kill.Retry", Flags)
bool FRPGNarrativeKillRetryTest::RunTest(const FString&)
{
    const auto C = Command(); FRPGNarrativeKillAttempt A; A.Begin(C); const FString Frozen = A.GetBody();
    for (int32 I = 0; I < 10; ++I)
    {
        TestTrue(TEXT("Retry remains dispatchable beyond four requests"), A.StartRequest());
        TestFalse(TEXT("Never dispatch concurrently for one event"), A.StartRequest());
        FRPGNarrativeInteractionHttpResult R; R.bCurrentServerContext = true; R.bTransportSuccessful = true;
        R.StatusCode = I == 0 ? 200 : I == 1 ? 403 : I == 2 ? 409 : 503;
        R.Body = I == 2 ? TEXT("{\"error\":\"idempotency_conflict\"}") : TEXT("lost or malformed receipt");
        TestEqual(TEXT("Uncertain reply preserves observation"), A.Receive(R), ERPGNarrativeInteractionOutcome::Unknown);
        TestEqual(TEXT("Every retry uses identical bytes"), A.GetBody(), Frozen);
        TestTrue(TEXT("Bounded backoff"), A.RetryDelay() <= 30.f);
    }
    FRPGNarrativeInteractionHttpResult Success; Success.bCurrentServerContext = Success.bTransportSuccessful = true;
    Success.StatusCode = 200; Success.Body = Receipt(C, TEXT("[") + Goal() + TEXT("]"));
    A.StartRequest(); auto Stale = Success; Stale.bCurrentServerContext = false;
    TestEqual(TEXT("Stale server response is unknown"), A.Receive(Stale), ERPGNarrativeInteractionOutcome::Unknown);
    A.StartRequest(); TestEqual(TEXT("Exact replay resolves"), A.Receive(Success), ERPGNarrativeInteractionOutcome::Succeeded);
    TestFalse(TEXT("Resolved event cannot dispatch"), A.StartRequest());
    TestEqual(TEXT("Late duplicate cannot resolve twice"), A.Receive(Success), ERPGNarrativeInteractionOutcome::Unknown);
    TestTrue(TEXT("Next event starts separately"), A.Begin(Command()));
    TestEqual(TEXT("New event resets backoff"), A.RetryDelay(), 1.f);
    A.StartRequest(); FRPGNarrativeInteractionHttpResult Window;
    Window.bCurrentServerContext = Window.bTransportSuccessful = true; Window.StatusCode = 409;
    Window.Body = TEXT("{\"error\":\"kill_event_out_of_window\"}");
    TestEqual(TEXT("Receipt-before-window contract permits rejection"), A.Receive(Window), ERPGNarrativeInteractionOutcome::Rejected);
    TestFalse(TEXT("Rejected observation finished"), A.IsPending());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeKillLifeTest, "ProjectRPG.Narrative.Kill.Life", Flags)
bool FRPGNarrativeKillLifeTest::RunTest(const FString&)
{
    FRPGNarrativeKillLife Life;
    TestFalse(TEXT("Uninitialized zero health isn't a death"), Life.Consume(true, 0, true));
    Life.Arm(false, 100, false); TestFalse(TEXT("Client/unready cannot arm"), Life.Consume(true, 0, true));
    Life.Arm(true, 0, true); TestFalse(TEXT("Already dead at startup gives no retro-credit"), Life.Consume(true, 0, true));
    Life.Arm(true, 100, false);
    TestFalse(TEXT("Tag alone is insufficient"), Life.Consume(true, 10, true));
    TestFalse(TEXT("Zero health without confirmed tag is insufficient"), Life.Consume(true, 0, false));
    TestFalse(TEXT("Retired/unready victim is insufficient"), Life.Consume(false, 0, true));
    TestFalse(TEXT("Nonfinite health is invalid"), Life.Consume(true, std::numeric_limits<float>::quiet_NaN(), true));
    TestTrue(TEXT("Ready live-to-dead is consumed"), Life.Consume(true, 0, true));
    TestFalse(TEXT("Repeated tag is not a second kill"), Life.Consume(true, 0, true));
    Life.Arm(true, 100, false); TestFalse(TEXT("An actor cannot be rearmed by healing/tag toggles"), Life.Consume(true, 0, true));
    FRPGNarrativeKillLife NextLife; NextLife.Arm(true, 100, false);
    TestTrue(TEXT("A new actor has a new life"), NextLife.Consume(true, 0, true));
    return true;
}
#endif
