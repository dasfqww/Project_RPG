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
    const FHttpRequestPtr Request = ActiveRequest;
    if (!Request->ProcessRequest() && ActiveRequest == Request)
        Request->OnProcessRequestComplete().ExecuteIfBound(Request, nullptr, false);
    return true;
}
void FRPGHttpNarrativeTransport::Cancel()
{
    ++Generation;
    if (ActiveRequest)
    {
        ActiveRequest->OnProcessRequestComplete().Unbind(); ActiveRequest->CancelRequest(); ActiveRequest.Reset();
    }
}
