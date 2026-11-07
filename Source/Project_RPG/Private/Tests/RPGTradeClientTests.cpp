#if WITH_DEV_AUTOMATION_TESTS
#include "Trading/RPGTradeClient.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace RPGTradeClientTests
{
const FGuid First(0x11111111, 0x11111111, 0x11111111, 0x11111111);
const FGuid Second(0x22222222, 0x22222222, 0x22222222, 0x22222222);
const FGuid Trade(0x33333333, 0x33333333, 0x33333333, 0x33333333);
const FGuid Item(0x44444444, 0x44444444, 0x44444444, 0x44444444);
FString Id(const FGuid& Value) { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
FString Snapshot(int64 Revision = 9, int64 OfferRevision = 3, const FGuid& TradeId = Trade)
{
    return FString::Printf(TEXT(R"({"tradeId":"%s","initiatorCharacterId":"%s","inviteeCharacterId":"%s","currencyCode":"Gold","revision":"%lld","offerRevision":"%lld","expiresAt":"2026-11-08T13:00:00Z","status":"Negotiating","completed":false,"initiatorOffer":{"currencyAmount":"9007199254740993","items":[{"itemId":"%s","expectedRevision":"9007199254740995","quantity":2}]},"inviteeOffer":{"currencyAmount":"0","items":[]},"initiatorReady":false,"inviteeReady":false,"initiatorConfirmed":false,"inviteeConfirmed":false})"),
        *Id(TradeId), *Id(First), *Id(Second), Revision, OfferRevision, *Id(Item));
}
FString Invitation(const FGuid& TradeId = Trade)
{
    auto Text = Snapshot(1, 1, TradeId).Replace(TEXT("Negotiating"), TEXT("Invited"));
    Text = Text.Replace(TEXT("9007199254740993"), TEXT("0"));
    const auto Start = Text.Find(TEXT("[{\"itemId\""));
    const auto End = Text.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
    Text.RemoveAt(Start + 1, End - Start - 1);
    return Text;
}
FRPGTradeClient Client(const FGuid& Character = First)
{
    FRPGTradeClient Result; Result.Bind(TEXT("https://backend.example/"), TEXT("account"), Character); return Result;
}
ERPGTradeResponse Read(FRPGTradeClient& C, const FString& State)
{ return C.CompleteRead(C.BeginRead(), 200, TEXT("{\"session\":") + State + TEXT("}")); }
FString Receipt(const FGuid& Request, const FString& State, const TCHAR* Rejection = TEXT("null"))
{
    return TEXT("{\"requestId\":\"") + Id(Request) + TEXT("\",\"session\":") + State
        + TEXT(",\"rejection\":") + Rejection + TEXT(",\"committedAt\":\"2026-11-08T12:00:00Z\"}");
}
TSharedPtr<FJsonObject> Body(const FRPGTradeClient& C)
{
    TSharedPtr<FJsonObject> Obj;
    if (C.GetPending()) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(C.GetPending()->Body), Obj);
    return Obj;
}
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeWireTest, "ProjectRPG.Trading.Client.Wire", RPGTradeClientTests::Flags)
bool FRPGTradeWireTest::RunTest(const FString&)
{
    using namespace RPGTradeClientTests;
    auto C = Client();
    TestTrue(TEXT("valid session"), Read(C, Snapshot()) == ERPGTradeResponse::Applied);
    if (!C.GetSession()) return false;
    TestEqual(TEXT("money above double precision is exact"), C.GetSession()->InitiatorOffer.CurrencyAmount, int64(9007199254740993LL));
    TestEqual(TEXT("item revision exact"), C.GetSession()->InitiatorOffer.Items[0].ExpectedRevision, int64(9007199254740995LL));
    for (const auto& Bad : {TEXT("01"), TEXT("-1"), TEXT("+1"), TEXT("1.0"), TEXT("1e3"), TEXT(" 1"), TEXT("9223372036854775808"), TEXT("")})
    {
        auto Invalid = Client();
        TestTrue(TEXT("noncanonical money rejected"), Read(Invalid, Snapshot().Replace(TEXT("9007199254740993"), Bad)) == ERPGTradeResponse::Unknown);
        TestNull(TEXT("invalid parse is atomic"), Invalid.GetSession());
    }
    const TArray<FString> InvalidStates = {
        Snapshot().Replace(TEXT("\"revision\":\"9\""), TEXT("\"revision\":9")),
        Snapshot().Replace(TEXT("\"revision\":\"9\""), TEXT("\"revision\":\"9\",\"revision\":\"10\"")),
        Snapshot().Replace(TEXT("\"quantity\":2"), TEXT("\"quantity\":1.5")),
        Snapshot().Replace(TEXT("\"quantity\":2"), TEXT("\"quantity\":2147483648")),
        Snapshot().Replace(TEXT("\"quantity\":2"), TEXT("\"quantity\":\"2\"")),
        Snapshot().Replace(TEXT("\"offerRevision\":\"3\""), TEXT("\"offerRevision\":\"10\"")),
        Snapshot().Replace(TEXT("9007199254740995"), TEXT("9223372036854775807")),
        Snapshot().Replace(TEXT("Negotiating"), TEXT("Agreed")),
        Snapshot().Replace(TEXT("\"completed\":false"), TEXT("\"completed\":true")),
        Snapshot().Replace(TEXT("\"initiatorConfirmed\":false"), TEXT("\"initiatorConfirmed\":true")),
        Snapshot().Replace(TEXT("\"initiatorReady\":false"), TEXT("\"initiatorReady\":\"false\"")),
        Snapshot().Replace(*Id(Second), *Id(First)),
        Snapshot().Replace(TEXT("2026-11-08T13:00:00Z"), TEXT("bad-date")),
        Snapshot().Replace(TEXT("Gold"), TEXT("Gold/")),
        Snapshot().LeftChop(1),
        FString::ChrN(32769, ' ')
    };
    for (const auto& Bad : InvalidStates)
    {
        auto Invalid = Client();
        TestTrue(TEXT("malformed or inconsistent snapshot"), Read(Invalid, Bad) == ERPGTradeResponse::Unknown);
    }
    auto Limit = Client();
    TestTrue(TEXT("int64 max is a valid read-only snapshot revision"), Read(Limit, Snapshot(MAX_int64).Replace(TEXT("9007199254740993"), TEXT("9223372036854775807"))) == ERPGTradeResponse::Applied);
    TestFalse(TEXT("exhausted revision cannot produce a command"), Limit.Prepare(ERPGTradeAction::Cancel));
    auto Outsider = Client(FGuid::NewGuid());
    TestTrue(TEXT("foreign participant denied"), Read(Outsider, Snapshot()) == ERPGTradeResponse::Unknown);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeCommandsTest, "ProjectRPG.Trading.Client.Commands", RPGTradeClientTests::Flags)
bool FRPGTradeCommandsTest::RunTest(const FString&)
{
    using namespace RPGTradeClientTests;
    auto C = Client();
    TestFalse(TEXT("self invite"), C.PrepareInvite(First));
    TestTrue(TEXT("new invite"), C.PrepareInvite(Second));
    auto Obj = Body(C);
    if (!TestTrue(TEXT("serialized invite"), Obj.IsValid())) return false;
    TestEqual(TEXT("invite has only allowed two fields"), Obj->Values.Num(), 2);
    TestEqual(TEXT("invitee"), Obj->GetStringField(TEXT("inviteeCharacterId")), Id(Second));
    TestTrue(TEXT("route includes selected actor and unique trade"), C.GetPending()->RelativePath == TEXT("/api/characters/") + Id(First) + TEXT("/trades/") + Id(C.GetPending()->TradeId) + TEXT("/invite"));
    TestFalse(TEXT("pending serializes commands"), C.PrepareInvite(Second));

    auto Inviter = Client(); Read(Inviter, Invitation());
    TestFalse(TEXT("initiator cannot accept"), Inviter.Prepare(ERPGTradeAction::Accept));
    auto Invitee = Client(Second); Read(Invitee, Invitation());
    TestTrue(TEXT("invitee can accept"), Invitee.Prepare(ERPGTradeAction::Accept));
    TestEqual(TEXT("accept field count"), Body(Invitee)->Values.Num(), 2);
    TestEqual(TEXT("accept expected revision string"), Body(Invitee)->GetStringField(TEXT("expectedRevision")), FString(TEXT("1")));

    for (const auto Action : {ERPGTradeAction::Ready, ERPGTradeAction::Withdraw, ERPGTradeAction::Cancel})
    {
        auto Next = Client(); Read(Next, Snapshot(9007199254740997LL));
        TestTrue(TEXT("allowed action"), Next.Prepare(Action));
        const auto Request = Body(Next);
        if (!Request) return false;
        TestEqual(TEXT("revision does not round"), Request->GetStringField(TEXT("expectedRevision")), FString(TEXT("9007199254740997")));
        TestEqual(TEXT("action fields"), Request->Values.Num(), Action == ERPGTradeAction::Ready ? 3 : 2);
        if (Action == ERPGTradeAction::Ready) TestEqual(TEXT("consent offer revision"), Request->GetStringField(TEXT("expectedOfferRevision")), FString(TEXT("3")));
    }
    auto OfferClient = Client(); Read(OfferClient, Snapshot());
    TestFalse(TEXT("confirm requires both ready"), OfferClient.Prepare(ERPGTradeAction::Confirm));
    FRPGTradeOffer Offer; Offer.CurrencyAmount = MAX_int64; Offer.Items.Add({Item, 9007199254740995LL, MAX_int32});
    TestTrue(TEXT("valid offer request"), OfferClient.Prepare(ERPGTradeAction::Offer, &Offer));
    auto OfferObj = Body(OfferClient);
    TestEqual(TEXT("offer has three fields"), OfferObj->Values.Num(), 3);
    TestEqual(TEXT("max amount serialized as string"), OfferObj->GetObjectField(TEXT("offer"))->GetStringField(TEXT("currencyAmount")), FString(TEXT("9223372036854775807")));
    const FRPGTradeItem Duplicate = Offer.Items[0];
    Offer.Items.Add(Duplicate); TestFalse(TEXT("duplicate offer rejected"), Offer.IsValid());
    Offer.Items.Empty(); Offer.CurrencyAmount = -1; TestFalse(TEXT("negative amount rejected"), Offer.IsValid());
    Offer.CurrencyAmount = 0;
    for (int32 Index = 0; Index < 9; ++Index) Offer.Items.Add({FGuid::NewGuid(), 1, 1});
    TestFalse(TEXT("item bound"), Offer.IsValid());
    auto Ready = Client();
    Read(Ready, Snapshot().Replace(TEXT("\"initiatorReady\":false"), TEXT("\"initiatorReady\":true")).Replace(TEXT("\"inviteeReady\":false"), TEXT("\"inviteeReady\":true")));
    TestTrue(TEXT("both ready can confirm"), Ready.Prepare(ERPGTradeAction::Confirm));
    TestEqual(TEXT("confirm pins offer revision"), Body(Ready)->GetStringField(TEXT("expectedOfferRevision")), FString(TEXT("3")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeRetryTest, "ProjectRPG.Trading.Client.UncertainRetry", RPGTradeClientTests::Flags)
bool FRPGTradeRetryTest::RunTest(const FString&)
{
    using namespace RPGTradeClientTests;
    auto C = Client(); C.PrepareInvite(Second);
    const auto Original = *C.GetPending();
    FRPGTradeTicket FirstAttempt; TestTrue(TEXT("dispatch"), C.BeginAttempt(FirstAttempt));
    FRPGTradeTicket Unused; TestFalse(TEXT("no overlapping dispatch"), C.BeginAttempt(Unused));
    TestTrue(TEXT("lost response is unknown"), C.CompleteAttempt(FirstAttempt, 0, TEXT("")) == ERPGTradeResponse::Unknown);
    TestTrue(TEXT("same identity reconnect"), C.Bind(TEXT("https://backend.example"), TEXT("account"), First));
    TestTrue(TEXT("active null is not a receipt"), Read(C, TEXT("null")) == ERPGTradeResponse::NoActiveTrade);
    TestNotNull(TEXT("null query retains request"), C.GetPending());
    TestFalse(TEXT("no new invite after uncertain commit"), C.PrepareInvite(Second));
    TestTrue(TEXT("latest poll while pending"), Read(C, Snapshot(9, 3, Original.TradeId)) == ERPGTradeResponse::Applied);
    FRPGTradeTicket Retry; C.BeginAttempt(Retry);
    TestEqual(TEXT("same request bytes after latest state changed"), C.GetPending()->Body, Original.Body);
    TestEqual(TEXT("same path"), C.GetPending()->RelativePath, Original.RelativePath);
    TestTrue(TEXT("old callback ignored"), C.CompleteAttempt(FirstAttempt, 200, Receipt(Original.RequestId, Invitation(Original.TradeId))) == ERPGTradeResponse::Ignored);
    TestTrue(TEXT("retry 403 cannot undo earlier uncertainty"), C.CompleteAttempt(Retry, 403, TEXT("{\"error\":\"character_not_owned\"}")) == ERPGTradeResponse::Unknown);
    C.BeginAttempt(Retry);
    TestTrue(TEXT("historical matching receipt resolves"), C.CompleteAttempt(Retry, 200, Receipt(Original.RequestId, Invitation(Original.TradeId))) == ERPGTradeResponse::Succeeded);
    TestNull(TEXT("pending released by receipt"), C.GetPending());
    TestEqual(TEXT("receipt cannot regress latest state"), C.GetSession()->Revision, int64(9));
    TestTrue(TEXT("duplicate delivery ignored"), C.CompleteAttempt(Retry, 200, Receipt(Original.RequestId, Invitation(Original.TradeId))) == ERPGTradeResponse::Ignored);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeOrderingTest, "ProjectRPG.Trading.Client.Ordering", RPGTradeClientTests::Flags)
bool FRPGTradeOrderingTest::RunTest(const FString&)
{
    using namespace RPGTradeClientTests;
    auto C = Client();
    const auto Old = C.BeginRead(); const auto New = C.BeginRead(Trade);
    TestTrue(TEXT("superseded GET ignored"), C.CompleteRead(Old, 200, TEXT("{\"session\":") + Snapshot(99) + TEXT("}")) == ERPGTradeResponse::Ignored);
    TestTrue(TEXT("new GET applies"), C.CompleteRead(New, 200, TEXT("{\"session\":") + Snapshot() + TEXT("}")) == ERPGTradeResponse::Applied);
    TestTrue(TEXT("old revision accepted without replacement"), Read(C, Snapshot(8)) == ERPGTradeResponse::Applied);
    TestEqual(TEXT("revision monotonic"), C.GetSession()->Revision, int64(9));
    TestTrue(TEXT("same revision different content rejected"), Read(C, Snapshot().Replace(TEXT("9007199254740993"), TEXT("5"))) == ERPGTradeResponse::Unknown);
    TestTrue(TEXT("offer content requires offer revision change"), Read(C, Snapshot(10).Replace(TEXT("9007199254740993"), TEXT("5"))) == ERPGTradeResponse::Unknown);
    TestTrue(TEXT("offer revision cannot regress"), Read(C, Snapshot(10, 2)) == ERPGTradeResponse::Unknown);
    TestTrue(TEXT("another trade cannot replace selected trade"), Read(C, Snapshot(10, 3, FGuid::NewGuid())) == ERPGTradeResponse::Unknown);
    TestFalse(TEXT("cannot clear open selection"), C.ClearClosedSession());
    const auto WrongPath = C.BeginRead(FGuid::NewGuid());
    TestTrue(TEXT("specific GET must match route"), C.CompleteRead(WrongPath, 200, TEXT("{\"session\":") + Snapshot(10) + TEXT("}")) == ERPGTradeResponse::Unknown);
    TestTrue(TEXT("closed state read"), Read(C, Snapshot(10).Replace(TEXT("Negotiating"), TEXT("Cancelled"))) == ERPGTradeResponse::Applied);
    TestFalse(TEXT("closed cannot command"), C.Prepare(ERPGTradeAction::Cancel));
    TestTrue(TEXT("closed cannot reopen"), Read(C, Snapshot(11)) == ERPGTradeResponse::Unknown);
    TestTrue(TEXT("explicit closed release"), C.ClearClosedSession());
    TestTrue(TEXT("next trade after release"), C.PrepareInvite(Second));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeLifetimeTest, "ProjectRPG.Trading.Client.Lifetime", RPGTradeClientTests::Flags)
bool FRPGTradeLifetimeTest::RunTest(const FString&)
{
    using namespace RPGTradeClientTests;
    for (int32 Boundary = 0; Boundary < 4; ++Boundary)
    {
        auto C = Client(); C.PrepareInvite(Second); const auto Request = *C.GetPending();
        FRPGTradeTicket Attempt; C.BeginAttempt(Attempt); const auto Poll = C.BeginRead();
        if (Boundary == 0) C.Bind(TEXT("https://other.example"), TEXT("account"), First);
        if (Boundary == 1) C.Bind(TEXT("https://backend.example"), TEXT("other"), First);
        if (Boundary == 2) C.Bind(TEXT("https://backend.example"), TEXT("account"), Second);
        if (Boundary == 3) C.Reset();
        TestNull(TEXT("identity boundary clears pending"), C.GetPending());
        TestTrue(TEXT("old account callback ignored"), C.CompleteAttempt(Attempt, 200, Receipt(Request.RequestId, Invitation(Request.TradeId))) == ERPGTradeResponse::Ignored);
        TestTrue(TEXT("old GET ignored"), C.CompleteRead(Poll, 200, TEXT("{\"session\":") + Snapshot() + TEXT("}")) == ERPGTradeResponse::Ignored);
        TestNull(TEXT("no cross-context state"), C.GetSession());
    }
    auto C = Client(); C.PrepareInvite(Second); FRPGTradeTicket Attempt; C.BeginAttempt(Attempt);
    const auto Request = *C.GetPending();
    TestTrue(TEXT("unmatched receipt unknown"), C.CompleteAttempt(Attempt, 200, Receipt(FGuid::NewGuid(), Invitation(Request.TradeId))) == ERPGTradeResponse::Unknown);
    C.BeginAttempt(Attempt);
    TestTrue(TEXT("wrong trade unknown"), C.CompleteAttempt(Attempt, 200, Receipt(Request.RequestId, Invitation())) == ERPGTradeResponse::Unknown);
    TestNull(TEXT("invalid receipt cannot install session"), C.GetSession());
    TestFalse(TEXT("invalid bind resets"), C.Bind(TEXT(""), TEXT("account"), First));
    TestNull(TEXT("invalid context has no request"), C.GetPending());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeReceiptsTest, "ProjectRPG.Trading.Client.Receipts", RPGTradeClientTests::Flags)
bool FRPGTradeReceiptsTest::RunTest(const FString&)
{
    using namespace RPGTradeClientTests;
    auto C = Client(); Read(C, Snapshot()); C.Prepare(ERPGTradeAction::Ready);
    const auto Request = *C.GetPending(); FRPGTradeTicket Attempt; C.BeginAttempt(Attempt);
    TestTrue(TEXT("persisted refusal applies state"), C.CompleteAttempt(Attempt, 409, Receipt(Request.RequestId, Snapshot(10), TEXT("\"insufficient_currency\""))) == ERPGTradeResponse::Rejected);
    TestEqual(TEXT("refusal revision"), C.GetSession()->Revision, int64(10));
    TestEqual(TEXT("refusal code"), C.GetLastRejection(), FString(TEXT("insufficient_currency")));
    TestNull(TEXT("refusal resolves pending"), C.GetPending());
    TestTrue(TEXT("new command uses refusal state"), C.Prepare(ERPGTradeAction::Cancel));
    TestEqual(TEXT("new revision"), Body(C)->GetStringField(TEXT("expectedRevision")), FString(TEXT("10")));
    C.BeginAttempt(Attempt);
    TestTrue(TEXT("definitive first refusal"), C.CompleteAttempt(Attempt, 409, TEXT("{\"error\":\"trade_revision_conflict\"}")) == ERPGTradeResponse::Rejected);
    TestNull(TEXT("first refusal clears request"), C.GetPending());
    for (int32 Status : {0, 200, 401, 408, 429, 500, 503})
    {
        auto Unknown = Client(); Unknown.PrepareInvite(Second); FRPGTradeTicket T; Unknown.BeginAttempt(T);
        TestTrue(TEXT("ambiguous status retains request"), Unknown.CompleteAttempt(T, Status, TEXT("{\"error\":\"unavailable\"}")) == ERPGTradeResponse::Unknown);
        TestNotNull(TEXT("pending retained"), Unknown.GetPending());
    }
    auto Complete = Client();
    const auto Ready = Snapshot().Replace(TEXT("\"initiatorReady\":false"), TEXT("\"initiatorReady\":true")).Replace(TEXT("\"inviteeReady\":false"), TEXT("\"inviteeReady\":true"));
    Read(Complete, Ready); Complete.Prepare(ERPGTradeAction::Confirm); const auto FinalRequest = *Complete.GetPending(); Complete.BeginAttempt(Attempt);
    const auto Final = Ready.Replace(TEXT("\"revision\":\"9\""), TEXT("\"revision\":\"10\"")).Replace(TEXT("Negotiating"), TEXT("Completed"))
        .Replace(TEXT("\"completed\":false"), TEXT("\"completed\":true")).Replace(TEXT("\"initiatorConfirmed\":false"), TEXT("\"initiatorConfirmed\":true"))
        .Replace(TEXT("\"inviteeConfirmed\":false"), TEXT("\"inviteeConfirmed\":true"));
    TestTrue(TEXT("completed receipt"), Complete.CompleteAttempt(Attempt, 200, Receipt(FinalRequest.RequestId, Final)) == ERPGTradeResponse::Succeeded);
    TestTrue(TEXT("completed display state"), Complete.GetSession()->Status == ERPGTradeStatus::Completed);
    TestFalse(TEXT("cannot cancel committed exchange"), Complete.Prepare(ERPGTradeAction::Cancel));
    return true;
}
#endif
