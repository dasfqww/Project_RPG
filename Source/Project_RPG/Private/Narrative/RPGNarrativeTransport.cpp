#include "Narrative/RPGNarrativeTransport.h"
#include "Manager/HttpWebManager.h"

FRPGHttpNarrativeTransport::FRPGHttpNarrativeTransport(UHttpWebManager* InWeb) : Web(InWeb) {}
FRPGHttpNarrativeTransport::~FRPGHttpNarrativeTransport() { Cancel(); }
bool FRPGHttpNarrativeTransport::GetContext(FString& Endpoint, FString& SteamId) const
{
    return Web.IsValid() && Web->GetPlayerEquipmentContext(Endpoint, SteamId);
}
bool FRPGHttpNarrativeTransport::Send(const FGuid CharacterId, const FString& ChoiceBody,
    TFunction<void(FRPGNarrativeHttpResult)> Completion)
{
    return SendRequest(CharacterId, ChoiceBody, false, MoveTemp(Completion));
}
bool FRPGHttpNarrativeTransport::SendDungeon(const FGuid CharacterId, const FString& Body,
    TFunction<void(FRPGNarrativeHttpResult)> Completion)
{
    return !Body.IsEmpty() && SendRequest(CharacterId, Body, true, MoveTemp(Completion));
}
bool FRPGHttpNarrativeTransport::SendRequest(const FGuid CharacterId, const FString& ChoiceBody, const bool bDungeon,
    TFunction<void(FRPGNarrativeHttpResult)> Completion, const bool bHistory)
{
    if (!Web.IsValid() || ActiveRequest || !Completion) return false;
    const uint64 ExpectedGeneration = ++Generation;
    TWeakPtr<FRPGHttpNarrativeTransport> WeakThis = AsShared();
    ActiveRequest = Web->CreatePlayerNarrativeRequest(CharacterId, ChoiceBody,
        [WeakThis, ExpectedGeneration, Completion = MoveTemp(Completion)](FRPGNarrativeHttpResult Result) mutable
        {
            auto Owner = WeakThis.Pin();
            if (!Owner || Owner->Generation != ExpectedGeneration) return;
            Owner->ActiveRequest.Reset();
            Completion(MoveTemp(Result));
        });
    if (!ActiveRequest) return false;
    if (bDungeon || bHistory)
    {
        // Reuse the player's build-only request and context checks; replace only its owner-scoped path.
        FString Endpoint, SteamId;
        if (!GetContext(Endpoint, SteamId)) { Cancel(); return false; }
        ActiveRequest->SetURL(Endpoint / FString::Printf(TEXT("characters/%s/narrative/%s"),
            *CharacterId.ToString(EGuidFormats::DigitsWithHyphensLower), bHistory ? TEXT("account-history") : TEXT("dungeon-objectives")));
    }
    const FHttpRequestPtr Request = ActiveRequest;
    if (!Request->ProcessRequest() && ActiveRequest == Request)
        Request->OnProcessRequestComplete().ExecuteIfBound(Request, nullptr, false);
    return true;
}
bool FRPGHttpNarrativeTransport::SendHistory(const FGuid CharacterId, TFunction<void(FRPGNarrativeHttpResult)> Completion)
{
    return SendRequest(CharacterId, FString(), false, MoveTemp(Completion), true);
}
void FRPGHttpNarrativeTransport::Cancel()
{
    ++Generation;
    if (ActiveRequest)
    {
        ActiveRequest->OnProcessRequestComplete().Unbind(); ActiveRequest->CancelRequest(); ActiveRequest.Reset();
    }
}
