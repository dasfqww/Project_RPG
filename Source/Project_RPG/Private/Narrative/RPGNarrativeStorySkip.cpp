#include "Narrative/RPGNarrativeStorySkip.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace RPGNarrativeSkipJson
{
bool String(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, FString& Out)
{
    const auto* V = O->Values.Find(Field); return V && V->IsValid() && (*V)->Type == EJson::String && (*V)->TryGetString(Out);
}
bool Boolean(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, bool& Out)
{
    const auto* V = O->Values.Find(Field); return V && V->IsValid() && (*V)->Type == EJson::Boolean && (*V)->TryGetBool(Out);
}
bool Integer(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, int32& Out)
{
    const auto* V = O->Values.Find(Field); double Number = 0;
    if (!V || !V->IsValid() || (*V)->Type != EJson::Number || !(*V)->TryGetNumber(Number)
        || !FMath::IsFinite(Number) || Number < 0 || Number > MAX_int32 || Number != FMath::FloorToDouble(Number)) return false;
    Out = static_cast<int32>(Number); return true;
}
bool Revision(const TSharedPtr<FJsonObject>& O, int64& Out)
{
    FString S;
    if (!String(O, TEXT("expectedRevision"), S) || S.IsEmpty() || S.Len() > 19) return false;
    int64 N = 0;
    for (TCHAR C : S) { if (C < '0' || C > '9' || N > (MAX_int64 - (C - '0')) / 10) return false; N = N * 10 + (C - '0'); }
    if (N == MAX_int64) return false;
    Out = N; return true;
}
bool Title(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FText& Out)
{
    const auto* V = O->Values.Find(Key);
    if (!V || !V->IsValid() || (*V)->Type == EJson::Null) return true;
    FString Text;
    if ((*V)->Type != EJson::String || !(*V)->TryGetString(Text) || Text.Len() > 256 || Text.TrimStartAndEnd().IsEmpty()) return false;
    Out = FText::FromString(Text); return true;
}
bool Ids(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, TArray<FString>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!O->TryGetArrayField(Key, Values) || Values->Num() > 256) return false;
    FString Last;
    for (const auto& V : *Values)
    {
        FString Id;
        if (!V.IsValid() || V->Type != EJson::String || !V->TryGetString(Id) || !FRPGNarrativeSkipCodec::ValidId(Id)
            || (!Last.IsEmpty() && Last.Compare(Id, ESearchCase::CaseSensitive) >= 0)) return false;
        Out.Add(Id); Last = MoveTemp(Id);
    }
    return true;
}
bool DefiniteRejection(const FRPGNarrativeHttpResult& R)
{
    if (R.StatusCode != 400 && R.StatusCode != 409) return false;
    TSharedPtr<FJsonObject> O; FString Code;
    if (R.Body.Len() > 8192 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(R.Body), O) || !O
        || !O->TryGetStringField(TEXT("error"), Code)) return false;
    static const TSet<FString> Codes = {TEXT("invalid_story_skip_command"), TEXT("story_skip_dungeon_busy"),
        TEXT("story_skip_already_finished"), TEXT("story_skip_not_eligible"), TEXT("story_skip_not_authored"),
        TEXT("story_skip_prerequisite_missing"), TEXT("story_skip_preview_changed"), TEXT("narrative_revision_conflict"),
        TEXT("narrative_catalog_changed")};
    // State/compatibility errors can also be raised while reading a COMMITTED receipt.
    // An idempotency conflict requires recovery too; it must not become a new intent.
    return Codes.Contains(Code);
}
}
bool FRPGNarrativeSkipCodec::ValidId(const FString& Id)
{
    if (Id.IsEmpty() || Id.Len() > 128) return false;
    for (TCHAR C : Id) if (!((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9')
        || C == '.' || C == '_' || C == '-')) return false;
    return true;
}
bool FRPGNarrativeSkipCodec::DecodePreview(const FString& Body, const FGuid Character, const FString& Chapter, FRPGNarrativeSkipPreview& Out)
{
    using namespace RPGNarrativeSkipJson;
    if (!Character.IsValid() || !ValidId(Chapter) || Body.Len() > 256 * 1024) return false;
    TSharedPtr<FJsonObject> O; FRPGNarrativeSkipPreview P; FString Id;
    int32 Cost = -1; bool Rewards = true, Training = false;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), O) || !O
        || !String(O, TEXT("characterId"), Id) || !FGuid::Parse(Id, P.Character) || P.Character != Character
        || !String(O, TEXT("chapterId"), P.Chapter) || P.Chapter != Chapter
        || !Integer(O, TEXT("catalogVersion"), P.CatalogVersion) || P.CatalogVersion < 1 || !Revision(O, P.Revision)
        || !Integer(O, TEXT("cost"), Cost) || Cost != 0
        || !Boolean(O, TEXT("grantsQuestRewards"), Rewards) || Rewards
        || !Boolean(O, TEXT("requiresOriginalTraining"), Training) || !Training
        || !Title(O, TEXT("title"), P.Title) || !Ids(O, TEXT("storyUnlocks"), P.Unlocks)
        || !String(O, TEXT("previewHash"), P.Hash) || P.Hash.Len() != 64) return false;
    for (TCHAR C : P.Hash) if (!((C >= '0' && C <= '9') || (C >= 'a' && C <= 'f'))) return false;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr; FString Last;
    if (!O->TryGetArrayField(TEXT("quests"), Values) || Values->IsEmpty() || Values->Num() > 256) return false;
    for (const auto& V : *Values)
    {
        if (!V.IsValid() || V->Type != EJson::Object) return false;
        auto Q = V->AsObject(); FRPGNarrativeSkipQuest Quest;
        if (!Q || !String(Q, TEXT("questId"), Quest.Id) || !ValidId(Quest.Id)
            || (!Last.IsEmpty() && Last.Compare(Quest.Id, ESearchCase::CaseSensitive) >= 0) || !Title(Q, TEXT("title"), Quest.Title)) return false;
        Last = Quest.Id; P.Quests.Add(MoveTemp(Quest));
    }
    Out = MoveTemp(P); return true;
}
bool FRPGNarrativeSkipCodec::Encode(const FRPGNarrativeSkipPreview& P, const FGuid Request, FString& Out)
{
    if (!Request.IsValid() || !P.Character.IsValid() || !ValidId(P.Chapter) || P.Hash.Len() != 64
        || P.Revision < 0 || P.Revision == MAX_int64 || P.CatalogVersion < 1) return false;
    for (TCHAR C : P.Hash) if (!((C >= '0' && C <= '9') || (C >= 'a' && C <= 'f'))) return false;
    auto O = MakeShared<FJsonObject>();
    O->SetStringField(TEXT("requestId"), Request.ToString(EGuidFormats::DigitsWithHyphensLower));
    O->SetStringField(TEXT("expectedRevision"), LexToString(P.Revision)); O->SetNumberField(TEXT("catalogVersion"), P.CatalogVersion);
    O->SetStringField(TEXT("chapterId"), P.Chapter); O->SetStringField(TEXT("previewHash"), P.Hash); O->SetBoolField(TEXT("confirmed"), true);
    FString Encoded;
    if (!FJsonSerializer::Serialize(O, TJsonWriterFactory<>::Create(&Encoded))) return false;
    Out = MoveTemp(Encoded); return true;
}
bool FRPGNarrativeSkipCodec::DecodeCommit(const FString& Body, const FRPGNarrativeSkipPreview& P, FRPGNarrativeSnapshot& Out)
{
    FRPGNarrativeSnapshot S; TSharedPtr<FJsonObject> O; TArray<FString> Skipped;
    if (!FRPGNarrativeCodec::Decode(Body, P.Character, S) || P.Revision == MAX_int64 || S.Revision != P.Revision + 1
        || S.CatalogVersion != P.CatalogVersion || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), O) || !O
        || !RPGNarrativeSkipJson::Ids(O, TEXT("storySkippedChapters"), Skipped) || !Skipped.Contains(P.Chapter)
        || Skipped.ContainsByPredicate([&](const auto& Id) { return !S.CompletedChapters.Contains(Id); })) return false;
    Out = MoveTemp(S); return true;
}
FRPGNarrativeSkipClient::FRPGNarrativeSkipClient(TSharedRef<IRPGNarrativeTransport> InTransport, FContext InContext,
    TFunction<bool(FGuid, const FString&)> InFinished, TFunction<void(FRPGNarrativeSnapshot)> InCommitted)
    : Transport(MoveTemp(InTransport)), Context(MoveTemp(InContext)), Finished(MoveTemp(InFinished)), Committed(MoveTemp(InCommitted)) {}
FRPGNarrativeSkipClient::~FRPGNarrativeSkipClient() { Transport->Cancel(); }
bool FRPGNarrativeSkipClient::IsSelected(const FGuid Id) const
{
    int64 R = 0; int32 C = 0; FString E, S;
    return Id.IsValid() && Context && Context(Id, R, C, false) && Transport->GetContext(E, S)
        && (!Character.IsValid() || (Character == Id && E == Endpoint && S == SteamId));
}
bool FRPGNarrativeSkipClient::Matches() const { return Character.IsValid() && IsSelected(Character); }
bool FRPGNarrativeSkipClient::CanStart(const FGuid Id) const
{
    int64 R = 0; int32 C = 0;
    return !IsBusy() && !HasPending() && IsSelected(Id) && Context(Id, R, C, true) && R >= 0 && R < MAX_int64 && C > 0;
}
bool FRPGNarrativeSkipClient::IsFinished(const FGuid Id, const FString& Chapter) const { return Finished && Finished(Id, Chapter); }
int64 FRPGNarrativeSkipClient::GetRevision(const FGuid Id) const
{
    int64 R = -1; int32 C = 0; return IsSelected(Id) && Context(Id, R, C, false) ? R : -1;
}
ERPGNarrativeSkipState FRPGNarrativeSkipClient::GetState(const FGuid Id) const { return IsSelected(Id) ? State : ERPGNarrativeSkipState::Idle; }
const FRPGNarrativeSkipPreview* FRPGNarrativeSkipClient::GetPreview(const FGuid Id) const
{
    int64 R = 0; int32 C = 0;
    return State == ERPGNarrativeSkipState::Ready && Review && CanStart(Id) && Context(Id, R, C, true)
        && Review->Revision == R && Review->CatalogVersion == C && !IsFinished(Id, Review->Chapter) ? &Review.GetValue() : nullptr;
}
bool FRPGNarrativeSkipClient::Preview(const FGuid Id, const FString& Chapter)
{
    auto KeepAlive = AsShared();
    if (!CanStart(Id) || !FRPGNarrativeSkipCodec::ValidId(Chapter) || IsFinished(Id, Chapter)) return false;
    FString E, S; if (!Transport->GetContext(E, S)) return false;
    Character = Id; Endpoint = MoveTemp(E); SteamId = MoveTemp(S); Review.Reset();
    const uint64 Token = ++Generation; Transport->Cancel(); State = ERPGNarrativeSkipState::Previewing; OnChanged.Broadcast();
    if (Token != Generation) return false;
    if (!Matches() || State != ERPGNarrativeSkipState::Previewing)
    { State = ERPGNarrativeSkipState::Failed; OnChanged.Broadcast(); return false; }
    TWeakPtr<FRPGNarrativeSkipClient> Weak = AsShared();
    const bool Sent = Transport->SendSkipPreview(Id, Chapter, [Weak, Token, Chapter](FRPGNarrativeHttpResult R)
        { if (auto Owner = Weak.Pin()) Owner->ReceivePreview(Token, Chapter, MoveTemp(R)); });
    if (!Sent && Token == Generation && State == ERPGNarrativeSkipState::Previewing) { State = ERPGNarrativeSkipState::Failed; OnChanged.Broadcast(); }
    return Sent;
}
void FRPGNarrativeSkipClient::ReceivePreview(const uint64 Token, FString Chapter, FRPGNarrativeHttpResult R)
{
    if (Token != Generation || State != ERPGNarrativeSkipState::Previewing) return;
    FRPGNarrativeSkipPreview P; int64 Revision = 0; int32 Catalog = 0;
    const bool Valid = Matches() && R.bCurrentPlayerContext && R.bTransportSuccessful && R.StatusCode == 200
        && Context(Character, Revision, Catalog, true) && FRPGNarrativeSkipCodec::DecodePreview(R.Body, Character, Chapter, P)
        && P.Revision == Revision && P.CatalogVersion == Catalog && !IsFinished(Character, Chapter);
    if (Valid) Review = MoveTemp(P);
    State = Valid ? ERPGNarrativeSkipState::Ready : ERPGNarrativeSkipState::Failed; OnChanged.Broadcast();
}
bool FRPGNarrativeSkipClient::Confirm(const FGuid Id)
{
    auto KeepAlive = AsShared();
    const auto* P = GetPreview(Id); if (!P || !FRPGNarrativeSkipCodec::Encode(*P, FGuid::NewGuid(), PendingBody)) return false;
    return SendPending();
}
bool FRPGNarrativeSkipClient::Retry(const FGuid Id)
{
    auto KeepAlive = AsShared(); return !IsBusy() && HasPending() && Id == Character && Matches() && SendPending();
}
bool FRPGNarrativeSkipClient::SendPending()
{
    if (!HasPending() || !Review || !Matches()) return false;
    const uint64 Token = ++Generation; Transport->Cancel(); State = ERPGNarrativeSkipState::Applying; OnChanged.Broadcast();
    if (Token != Generation) return false;
    if (!Matches() || State != ERPGNarrativeSkipState::Applying)
    { State = ERPGNarrativeSkipState::Uncertain; OnChanged.Broadcast(); return false; }
    TWeakPtr<FRPGNarrativeSkipClient> Weak = AsShared();
    const bool Sent = Transport->SendSkip(Character, PendingBody, [Weak, Token](FRPGNarrativeHttpResult R)
        { if (auto Owner = Weak.Pin()) Owner->ReceiveCommit(Token, MoveTemp(R)); });
    if (!Sent && Token == Generation && State == ERPGNarrativeSkipState::Applying) { State = ERPGNarrativeSkipState::Uncertain; OnChanged.Broadcast(); }
    return Sent;
}
void FRPGNarrativeSkipClient::ReceiveCommit(const uint64 Token, FRPGNarrativeHttpResult R)
{
    if (Token != Generation || State != ERPGNarrativeSkipState::Applying || !Review || !HasPending()) return;
    const bool Current = Matches() && R.bCurrentPlayerContext;
    FRPGNarrativeSnapshot S;
    if (Current && R.bTransportSuccessful && R.StatusCode == 200 && FRPGNarrativeSkipCodec::DecodeCommit(R.Body, *Review, S))
    {
        PendingBody.Reset(); Review.Reset(); State = ERPGNarrativeSkipState::Completed;
        if (Committed) Committed(MoveTemp(S));
    }
    else if (Current && R.bTransportSuccessful && RPGNarrativeSkipJson::DefiniteRejection(R))
    { PendingBody.Reset(); Review.Reset(); State = ERPGNarrativeSkipState::Failed; }
    else State = ERPGNarrativeSkipState::Uncertain;
    if (Token == Generation) OnChanged.Broadcast();
}
void FRPGNarrativeSkipClient::CancelPreview()
{
    auto KeepAlive = AsShared(); if (IsBusy() || HasPending()) return;
    Review.Reset(); State = ERPGNarrativeSkipState::Idle; OnChanged.Broadcast();
}
void FRPGNarrativeSkipClient::Reset()
{
    auto KeepAlive = AsShared(); ++Generation; Transport->Cancel(); Review.Reset(); PendingBody.Reset();
    Character.Invalidate(); Endpoint.Reset(); SteamId.Reset(); State = ERPGNarrativeSkipState::Idle; OnChanged.Broadcast();
}
