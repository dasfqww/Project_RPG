#include "Narrative/RPGNarrativeHistory.h"
#include "Narrative/RPGNarrativeTransportInterface.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool IdValid(const FString& Id)
{
    if (Id.IsEmpty() || Id.Len() > 128) return false;
    for (const TCHAR C : Id) if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '.' || C == '-' || C == '_')) return false;
    return true;
}
}
bool FRPGNarrativeAccountHistory::Decode(const FString& Json, const FGuid ExpectedCharacter, FRPGNarrativeAccountHistory& Out)
{
    if (!ExpectedCharacter.IsValid() || Json.IsEmpty() || Json.Len() > 256 * 1024) return false;
    TSharedPtr<FJsonObject> Object;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object) return false;
    const auto Owner = Object->TryGetField(TEXT("characterId")), Version = Object->TryGetField(TEXT("catalogVersion"));
    FRPGNarrativeAccountHistory Parsed;
    if (!Owner || Owner->Type != EJson::String || !FGuid::Parse(Owner->AsString(), Parsed.Character) || Parsed.Character != ExpectedCharacter
        || !Version || Version->Type != EJson::Number) return false;
    const double Number = Version->AsNumber();
    if (!FMath::IsFinite(Number) || Number < 1 || Number > MAX_int32 || FMath::FloorToDouble(Number) != Number) return false;
    Parsed.CatalogVersion = static_cast<int32>(Number);
    const TArray<TSharedPtr<FJsonValue>>* Chapters = nullptr;
    if (!Object->TryGetArrayField(TEXT("chapters"), Chapters) || Chapters->Num() > 256) return false;
    FString Previous;
    for (const auto& Value : *Chapters)
    {
        const TSharedPtr<FJsonObject>* Item = nullptr;
        if (!Value || !Value->TryGetObject(Item) || !Item || !*Item) return false;
        const auto Id = (*Item)->TryGetField(TEXT("chapterId")), Selected = (*Item)->TryGetField(TEXT("completedOnSelectedCharacter")),
            Other = (*Item)->TryGetField(TEXT("completedOnOtherCharacter")), Title = (*Item)->TryGetField(TEXT("title"));
        if (!Id || Id->Type != EJson::String || !IdValid(Id->AsString()) || (!Previous.IsEmpty() && Previous.Compare(Id->AsString(), ESearchCase::CaseSensitive) >= 0)
            || !Selected || Selected->Type != EJson::Boolean || !Other || Other->Type != EJson::Boolean
            || (!Selected->AsBool() && !Other->AsBool())) return false;
        FRPGNarrativeChapterHistory Chapter;
        Chapter.Id = Id->AsString(); Chapter.bSelected = Selected->AsBool(); Chapter.bOther = Other->AsBool();
        if (Title && Title->Type != EJson::Null)
        {
            if (Title->Type != EJson::String || Title->AsString().Len() > 256 || Title->AsString().TrimStartAndEnd().IsEmpty()) return false;
            Chapter.Title = FText::FromString(Title->AsString());
        }
        Previous = Chapter.Id; Parsed.Chapters.Add(MoveTemp(Chapter));
    }
    Out = MoveTemp(Parsed); return true;
}
FRPGNarrativeHistoryClient::FRPGNarrativeHistoryClient(TSharedRef<IRPGNarrativeTransport> InTransport, TFunction<bool(FGuid)> InSelection)
    : Transport(MoveTemp(InTransport)), Selection(MoveTemp(InSelection)) {}
FRPGNarrativeHistoryClient::~FRPGNarrativeHistoryClient() { Transport->Cancel(); }
bool FRPGNarrativeHistoryClient::IsSelected(const FGuid Id) const { return IsInGameThread() && Id.IsValid() && Selection && Selection(Id); }
bool FRPGNarrativeHistoryClient::ContextMatches() const
{
    FString CurrentEndpoint, CurrentSteam;
    return IsSelected(Character) && Transport->GetContext(CurrentEndpoint, CurrentSteam) && CurrentEndpoint == Endpoint && CurrentSteam == Steam;
}
ERPGNarrativeHistoryState FRPGNarrativeHistoryClient::GetState(const FGuid Id) const
{
    return Id == Character && ContextMatches() ? State : ERPGNarrativeHistoryState::Idle;
}
const FRPGNarrativeAccountHistory* FRPGNarrativeHistoryClient::GetView(const FGuid Id) const
{
    return GetState(Id) == ERPGNarrativeHistoryState::Ready ? &View : nullptr;
}
void FRPGNarrativeHistoryClient::Reset()
{
    [[maybe_unused]] const auto KeepAlive = AsShared();
    ++Generation; Transport->Cancel(); Character.Invalidate(); Endpoint.Reset(); Steam.Reset();
    View = {}; State = ERPGNarrativeHistoryState::Idle; OnChanged.Broadcast();
}
bool FRPGNarrativeHistoryClient::Refresh(const FGuid Id)
{
    const auto KeepAlive = AsShared();
    if (!IsSelected(Id)) return false;
    FString CurrentEndpoint, CurrentSteam;
    if (!Transport->GetContext(CurrentEndpoint, CurrentSteam) || CurrentEndpoint.IsEmpty() || CurrentSteam.IsEmpty()) return false;
    if (Id == Character && ContextMatches() && State == ERPGNarrativeHistoryState::Loading) return false;
    ++Generation; Transport->Cancel(); View = {}; Character = Id;
    Endpoint = MoveTemp(CurrentEndpoint); Steam = MoveTemp(CurrentSteam); State = ERPGNarrativeHistoryState::Loading;
    const uint64 Request = Generation; const TWeakPtr<FRPGNarrativeHistoryClient> WeakThis = KeepAlive;
    OnChanged.Broadcast();
    // A UI callback may change selection or reset this read before dispatch.
    if (Generation != Request) return false;
    if (!ContextMatches()) { State = ERPGNarrativeHistoryState::Failed; OnChanged.Broadcast(); return false; }
    const bool Sent = Transport->SendHistory(Id, [WeakThis, Request](FRPGNarrativeHttpResult Result)
        { if (auto Self = WeakThis.Pin()) Self->Receive(Request, MoveTemp(Result)); });
    if (!Sent && Generation == Request && State == ERPGNarrativeHistoryState::Loading)
    { State = ERPGNarrativeHistoryState::Failed; OnChanged.Broadcast(); }
    return Sent;
}
void FRPGNarrativeHistoryClient::Receive(const uint64 Request, FRPGNarrativeHttpResult Result)
{
    if (!IsInGameThread() || Generation != Request || State != ERPGNarrativeHistoryState::Loading) return;
    FRPGNarrativeAccountHistory Parsed;
    const bool Valid = Result.bCurrentPlayerContext && ContextMatches() && Result.bTransportSuccessful && Result.StatusCode == 200
        && FRPGNarrativeAccountHistory::Decode(Result.Body, Character, Parsed);
    View = Valid ? MoveTemp(Parsed) : FRPGNarrativeAccountHistory{};
    State = Valid ? ERPGNarrativeHistoryState::Ready : ERPGNarrativeHistoryState::Failed;
    OnChanged.Broadcast();
}
