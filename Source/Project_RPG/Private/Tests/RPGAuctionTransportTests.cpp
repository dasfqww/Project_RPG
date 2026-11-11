#if WITH_DEV_AUTOMATION_TESTS
#include "Auction/RPGAuctionTransport.h"
#include "Online/RPGBackendPlayerAuthState.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonSerializer.h"

namespace AuctionHttpTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
struct FRun : TSharedFromThis<FRun>
{
    FAutomationTestBase* Test = nullptr;
    FRPGBackendPlayerAuthState Auth;
    FString Endpoint, Account, Token, SellerAccount, SellerToken, Admin;
    FGuid Character, SellerCharacter, Listing;
    TSharedPtr<FRPGHttpAuctionTransport> Transport;
    FRPGAuctionClient Client, Recovered;
    FRPGAuctionIntent Original;
    FRPGTradeTicket Ticket;
    int32 Stage = 0, CancelledCallbacks = 0;
    bool bWaiting = false;
    double Started = 0, CancelStarted = 0;
    TSharedRef<FRPGHttpAuctionTransport> Make()
    {
        TWeakPtr<FRun> Weak = AsShared();
        return MakeShared<FRPGHttpAuctionTransport>([Weak](FRPGAuctionHttpContext& V)
        { auto R = Weak.Pin(); if (!R) return false; V = {R->Endpoint, R->Account, R->Auth.Authorization(FPlatformTime::Seconds())}; return true; },
        [Weak](const FRPGAuctionHttpContext& V, int32 Code)
        { auto R = Weak.Pin(); const bool Current = R && R->Endpoint == V.Endpoint && R->Account == V.Account && R->Auth.MatchesRequest(V.Authorization);
          if (Current) R->Auth.InvalidateIfUnauthorized(V.Authorization, Code); return Current; });
    }
    IRPGAuctionTransport::FCompletion Reply(TFunction<void(const FRPGAuctionHttpResult&)> Check)
    {
        bWaiting = true;
        return [R = AsShared(), Check = MoveTemp(Check)](FRPGAuctionHttpResult Result)
        { R->Test->TestTrue(TEXT("callback game thread"), IsInGameThread()); Check(Result); R->bWaiting = false; ++R->Stage; };
    }
    bool Dispatch(bool Sent)
    { if (!Sent) { Test->AddError(TEXT("HTTP dispatch unexpectedly refused")); bWaiting = false; Stage = 99; } return Sent; }
};
}
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FAuctionHttpWait, TSharedPtr<AuctionHttpTests::FRun>, State);
bool FAuctionHttpWait::Update()
{
    const auto R = State;
    if (FPlatformTime::Seconds() - R->Started > 60) { R->Test->AddError(TEXT("Owned auction HTTP timed out")); R->Transport->Cancel(); return true; }
    if (R->bWaiting) return false;
    auto& T = *R->Test;
    const auto Stage = R->Stage;
    if (Stage == 0)
    {
        FRPGAuctionSearch Q; Q.DefinitionName = TEXT("Ore"); Q.Limit = 1; Q.MinimumPrice = 699; Q.MaximumPrice = 699;
        R->Dispatch(R->Transport->SendPage(R->Character, Q, false, R->Reply([R](const FRPGAuctionHttpResult& V)
        {
            R->Test->TestEqual(TEXT("real public search"), V.StatusCode, 200);
            FRPGAuctionPage P;
            if (!R->Test->TestTrue(TEXT("actual backend listing page"), RPGAuction::DecodePage(V.Body, false, P) && P.Listings.Num() == 1)) { R->Stage = 98; return; }
            R->Test->TestTrue(TEXT("bind player"), R->Client.Bind(R->Endpoint, R->Account, R->Character));
            R->Test->TestTrue(TEXT("prepare actual listing purchase"), R->Client.PreparePurchase(P.Listings[0]));
            R->Test->TestTrue(TEXT("export original bytes"), R->Client.ExportPending(R->Original));
        })));
    }
    else if (Stage == 1)
    {
        T.TestTrue(TEXT("attempt ticket"), R->Client.BeginAttempt(R->Ticket));
        R->Dispatch(R->Transport->SendIntent(R->Original, R->Reply([R](const FRPGAuctionHttpResult& V)
        {
            R->Test->TestEqual(TEXT("post-commit response loss"), V.StatusCode, 503);
            R->Test->TestEqual(TEXT("server retry delay"), V.RetryAfterSeconds, 30.0);
            R->Test->TestTrue(TEXT("uncertain original retained"), R->Client.CompleteAttempt(R->Ticket, V.StatusCode, V.Body) == ERPGAuctionResponse::Unknown);
            R->Test->TestNotNull(TEXT("pending survives 503"), R->Client.GetPending());
        })));
        T.TestFalse(TEXT("single flight rejects parallel GET"), R->Transport->SendListing(R->Character, R->Listing, [](FRPGAuctionHttpResult) {}));
    }
    else if (Stage == 2)
    {
        T.TestTrue(TEXT("bind recovered model"), R->Recovered.Bind(R->Endpoint, R->Account, R->Character));
        T.TestTrue(TEXT("restore original intent"), R->Recovered.RestorePending(R->Original)); FString Path;
        T.TestTrue(TEXT("recovery ticket"), R->Recovered.BeginRecovery(R->Ticket, Path));
        R->Dispatch(R->Transport->SendReceipt(R->Character, R->Original.Request.RequestId, R->Reply([R](const FRPGAuctionHttpResult& V)
        {
            R->Test->TestEqual(TEXT("committed receipt GET"), V.StatusCode, 200);
            R->Test->TestTrue(TEXT("receipt resolves restored uncertainty"), R->Recovered.CompleteRecovery(R->Ticket, V.StatusCode, V.Body) == ERPGAuctionResponse::Succeeded);
            R->Test->TestTrue(TEXT("recovery requests latest asset refresh"), R->Recovered.NeedsAssetRefresh());
        })));
    }
    else if (Stage == 3)
    {
        T.TestTrue(TEXT("original retry ticket"), R->Client.BeginAttempt(R->Ticket));
        R->Dispatch(R->Transport->SendIntent(R->Original, R->Reply([R](const FRPGAuctionHttpResult& V)
        {
            R->Test->TestEqual(TEXT("same UUID/body replay"), V.StatusCode, 200);
            R->Test->TestTrue(TEXT("purchase replay succeeds"), R->Client.CompleteAttempt(R->Ticket, V.StatusCode, V.Body) == ERPGAuctionResponse::Succeeded);
            R->Test->TestNull(TEXT("pending cleared by matching receipt"), R->Client.GetPending());
            if (const auto* Receipt = R->Client.GetReceipt()) R->Test->TestEqual(TEXT("debit once"), Receipt->Balance, int64(301));
            else R->Test->AddError(TEXT("Purchase receipt missing"));
        })));
    }
    else if (Stage == 4)
    {
        FRPGAuctionSearch Q; Q.DefinitionName = TEXT("광석 &currencyCode=Gold");
        R->Dispatch(R->Transport->SendPage(R->Character, Q, false, R->Reply([R](const FRPGAuctionHttpResult& V)
        { FRPGAuctionPage P; R->Test->TestTrue(TEXT("encoded filter cannot inject query"), V.StatusCode == 200 && RPGAuction::DecodePage(V.Body, false, P) && P.Listings.IsEmpty()); })));
    }
    else if (Stage == 5)
    {
        R->Account = R->SellerAccount; R->Auth.SetOpaqueToken(R->SellerToken);
        R->Dispatch(R->Transport->SendPage(R->SellerCharacter, {}, true, R->Reply([R](const FRPGAuctionHttpResult& V)
        { FRPGAuctionPage P; R->Test->TestTrue(TEXT("seller sold history"), V.StatusCode == 200 && RPGAuction::DecodePage(V.Body, true, P) && P.Listings.Num() == 1 && P.Listings[0].Status == ERPGAuctionStatus::Sold); })));
    }
    else if (Stage == 6)
    {
        R->Dispatch(R->Transport->SendPayable(R->SellerCharacter, R->Listing, R->Reply([R](const FRPGAuctionHttpResult& V)
        { FRPGAuctionPayable P; R->Test->TestTrue(TEXT("seller payable real HTTP"), V.StatusCode == 200 && RPGAuction::DecodePayable(V.Body, P) && P.Remaining == 665); })));
    }
    else if (Stage == 7)
    {
        R->Account = R->Original.Scope.Account; R->Auth.SetOpaqueToken(TEXT("invalid-token"));
        R->Dispatch(R->Transport->SendListing(R->Character, R->Listing, R->Reply([R](const FRPGAuctionHttpResult& V)
        { R->Test->TestEqual(TEXT("real invalid player token"), V.StatusCode, 401); R->Test->TestFalse(TEXT("current credential invalidated"), R->Auth.HasAccessToken(FPlatformTime::Seconds())); })));
    }
    else if (Stage == 8)
    {
        R->Auth.SetOpaqueToken(R->Admin);
        R->Dispatch(R->Transport->SendPage(R->Character, {}, false, R->Reply([R](const FRPGAuctionHttpResult& V)
        { R->Test->TestEqual(TEXT("admin cannot substitute for Player"), V.StatusCode, 403); })));
    }
    else if (Stage == 9)
    {
        R->Auth.SetOpaqueToken(R->Token);
        R->Dispatch(R->Transport->SendPage(R->SellerCharacter, {}, false, R->Reply([R](const FRPGAuctionHttpResult& V)
        { R->Test->TestEqual(TEXT("foreign character denied"), V.StatusCode, 403); })));
    }
    else if (Stage >= 10 && Stage <= 12)
    {
        R->Account = R->Original.Scope.Account; R->Endpoint = R->Original.Scope.Endpoint; R->Auth.SetOpaqueToken(R->Token);
        R->Dispatch(R->Transport->SendReceipt(R->Character, R->Original.Request.RequestId, R->Reply([R](const FRPGAuctionHttpResult& V)
        { R->Test->TestFalse(TEXT("changed auth context rejected"), V.bCurrentPlayerContext); R->Test->TestTrue(TEXT("stale response body/status/delay suppressed"), V.Body.IsEmpty() && V.StatusCode == 0 && V.RetryAfterSeconds == 0 && !V.bTransportSuccessful); })));
        if (Stage == 10) R->Auth.SetOpaqueToken(TEXT("renewed-token"));
        if (Stage == 11) R->Account = TEXT("76561198880000999");
        if (Stage == 12) R->Endpoint += TEXT("/other");
    }
    else if (Stage == 13)
    {
        R->Account = R->Original.Scope.Account; R->Endpoint = R->Original.Scope.Endpoint; R->Auth.SetOpaqueToken(R->Token);
        // Completion clears single-flight state before calling the consumer.
        R->bWaiting = true;
        R->Dispatch(R->Transport->SendReceipt(R->Character, R->Original.Request.RequestId, [R](FRPGAuctionHttpResult V)
        {
            R->Test->TestEqual(TEXT("first reentrant read"), V.StatusCode, 200);
            R->Dispatch(R->Transport->SendReceipt(R->Character, R->Original.Request.RequestId, R->Reply([R](const FRPGAuctionHttpResult& W)
            { R->Test->TestEqual(TEXT("reentrant next read"), W.StatusCode, 200); })));
        }));
    }
    else if (Stage == 14)
    {
        R->Auth.Invalidate(); FString E, A;
        T.TestTrue(TEXT("identity retained while token unavailable"), R->Transport->GetContext(E, A));
        T.TestFalse(TEXT("expired credential waits"), R->Transport->CanSend());
        T.TestFalse(TEXT("cannot dispatch missing credential"), R->Transport->SendIntent(R->Original, [](FRPGAuctionHttpResult) {}));
        R->Auth.SetOpaqueToken(R->Token);
        auto Wrong = R->Original; Wrong.Scope.Account = TEXT("other");
        T.TestFalse(TEXT("foreign intent scope refused"), R->Transport->SendIntent(Wrong, [](FRPGAuctionHttpResult) {}));
        Wrong = R->Original; Wrong.Request.RelativePath += TEXT("?injected=yes");
        T.TestFalse(TEXT("altered intent route refused"), R->Transport->SendIntent(Wrong, [](FRPGAuctionHttpResult) {}));
        FRPGAuctionSearch Bad; Bad.Cursor = TEXT("x&limit=50");
        T.TestFalse(TEXT("invalid cursor refused"), R->Transport->SendPage(R->Character, Bad, false, [](FRPGAuctionHttpResult) {}));
        Bad = {}; Bad.MinimumPrice = 10; Bad.MaximumPrice = 9;
        T.TestFalse(TEXT("inverted bounds refused"), R->Transport->SendPage(R->Character, Bad, false, [](FRPGAuctionHttpResult) {}));
        T.TestTrue(TEXT("cancelled request dispatch"), R->Transport->SendReceipt(R->Character, R->Original.Request.RequestId, [R](FRPGAuctionHttpResult) { ++R->CancelledCallbacks; }));
        R->Transport->Cancel();
        auto Destroyed = R->Make();
        T.TestTrue(TEXT("destroyed request dispatch"), Destroyed->SendReceipt(R->Character, R->Original.Request.RequestId, [R](FRPGAuctionHttpResult) { ++R->CancelledCallbacks; }));
        R->CancelStarted = FPlatformTime::Seconds(); ++R->Stage;
    }
    else if (Stage == 15)
    {
        if (FPlatformTime::Seconds() - R->CancelStarted < 0.5) return false;
        T.TestEqual(TEXT("cancel/destruction suppress late completions"), R->CancelledCallbacks, 0);
        T.TestTrue(TEXT("cancel releases single flight"), R->Transport->CanSend()); return true;
    }
    else return true;
    return false;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionHttpNative, "ProjectRPG.Auction.Transport.MemoryHttp", AuctionHttpTests::Flags)
bool FAuctionHttpNative::RunTest(const FString&)
{
    auto R = MakeShared<AuctionHttpTests::FRun>(); R->Test = this; R->Started = FPlatformTime::Seconds();
    FString File = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_AUCTION_HTTP_FIXTURE")), Text;
    TSharedPtr<FJsonObject> J;
    if (!TestTrue(TEXT("owned private fixture"), FFileHelper::LoadFileToString(Text, *File) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) && J.IsValid())) return false;
    auto Str = [&J](const TCHAR* Key) { FString V; J->TryGetStringField(Key, V); return V; };
    R->Endpoint = Str(TEXT("endpoint")); R->Account = Str(TEXT("account")); R->Token = Str(TEXT("token"));
    R->SellerAccount = Str(TEXT("sellerAccount")); R->SellerToken = Str(TEXT("sellerToken")); R->Admin = Str(TEXT("admin"));
    if (!TestTrue(TEXT("fixture identities"), FGuid::Parse(Str(TEXT("character")), R->Character) && FGuid::Parse(Str(TEXT("sellerCharacter")), R->SellerCharacter) && FGuid::Parse(Str(TEXT("listing")), R->Listing))) return false;
    R->Auth.SetOpaqueToken(R->Token); R->Transport = R->Make();
    ADD_LATENT_AUTOMATION_COMMAND(FAuctionHttpWait(R)); return true;
}
#endif
