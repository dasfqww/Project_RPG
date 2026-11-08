#include "Narrative/RPGNarrativeInteraction.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool StringField(const FJsonObject& Object, const TCHAR* Name, FString& Out)
{
    const auto Value = Object.TryGetField(Name);
    return Value && Value->Type == EJson::String && Value->TryGetString(Out);
}
}

bool FRPGNarrativeInteractionCommand::IsValidId(const FString& Id)
{
    if (Id.IsEmpty() || Id.Len() > 128) return false;
    for (const TCHAR C : Id)
        if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '.' || C == '_' || C == '-')) return false;
    return true;
}
bool FRPGNarrativeInteractionCommand::IsValid() const
{
    return SessionId.IsValid() && CharacterId.IsValid() && EventId.IsValid() && ConnectionGeneration > 0
        && IsValidId(TargetId) && ObservedAt.GetTicks() > 0;
}
FString FRPGNarrativeInteractionCommand::Encode() const
{
    if (!IsValid()) return {};
    const auto Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("eventId"), EventId.ToString(EGuidFormats::DigitsWithHyphensLower));
    Object->SetStringField(TEXT("targetId"), TargetId);
    Object->SetStringField(TEXT("connectionGeneration"), LexToString(ConnectionGeneration));
    Object->SetStringField(TEXT("observedAtUtc"), ObservedAt.ToIso8601());
    FString Json;
    FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
    return Json;
}
bool FRPGNarrativeInteractionAttempt::Begin(const FRPGNarrativeInteractionCommand& Observation)
{
    if (IsPending() || !Observation.IsValid()) return false;
    Pending = Observation; Body = Observation.Encode(); Attempts = 0; bInFlight = false;
    return true;
}
bool FRPGNarrativeInteractionAttempt::StartRequest()
{
    if (!IsPending() || bInFlight) return false;
    ++Attempts; bInFlight = true; return true;
}
void FRPGNarrativeInteractionAttempt::Reset()
{
    Pending.Reset(); Body.Reset(); Attempts = 0; bInFlight = false;
}
ERPGNarrativeInteractionOutcome FRPGNarrativeInteractionAttempt::Receive(const FRPGNarrativeInteractionHttpResult& Result)
{
    if (!Pending || !bInFlight) return ERPGNarrativeInteractionOutcome::Unknown;
    bInFlight = false;
    if (!Result.bCurrentServerContext || !Result.bTransportSuccessful) return ERPGNarrativeInteractionOutcome::Unknown;
    if (Result.StatusCode == 200 && DecodeReceipt(Result.Body, Pending.GetValue()))
    { Reset(); return ERPGNarrativeInteractionOutcome::Succeeded; }
    // Exact receipts are resolved before the backend's observation window check. A typed window
    // rejection therefore establishes that this frozen event did not commit, even after a lost reply.
    if (Result.StatusCode == 409 && Result.Body.Len() < 4096)
    {
        TSharedPtr<FJsonObject> Error; FString Code;
        if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Result.Body), Error) && Error
            && StringField(*Error, TEXT("error"), Code) && Code == TEXT("interaction_event_out_of_window"))
        { Reset(); return ERPGNarrativeInteractionOutcome::Rejected; }
    }
    return ERPGNarrativeInteractionOutcome::Unknown;
}
bool FRPGNarrativeInteractionAttempt::DecodeReceipt(const FString& Json, const FRPGNarrativeInteractionCommand& Expected)
{
    if (Json.IsEmpty() || Json.Len() > 256 * 1024) return false;
    TSharedPtr<FJsonObject> Object;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object) return false;
    FString Event, Character, Revision;
    FGuid EventId, CharacterId;
    if (!StringField(*Object, TEXT("eventId"), Event) || !FGuid::Parse(Event, EventId) || EventId != Expected.EventId
        || !StringField(*Object, TEXT("characterId"), Character) || !FGuid::Parse(Character, CharacterId) || CharacterId != Expected.CharacterId
        || !StringField(*Object, TEXT("revision"), Revision) || Revision.IsEmpty() || Revision.Len() > 19) return false;
    int64 ParsedRevision = 0;
    for (const TCHAR C : Revision)
    {
        if (C < '0' || C > '9' || ParsedRevision > (MAX_int64 - (C - '0')) / 10) return false;
        ParsedRevision = ParsedRevision * 10 + C - '0';
    }
    const TArray<TSharedPtr<FJsonValue>>* Objectives = nullptr;
    if (!Object->TryGetArrayField(TEXT("appliedObjectives"), Objectives) || Objectives->Num() > 256) return false;
    TSet<FString> Seen;
    for (const auto& Value : *Objectives)
    {
        const TSharedPtr<FJsonObject>* Item = nullptr; FString Quest, Goal;
        if (!Value || !Value->TryGetObject(Item) || !Item || !*Item
            || !StringField(**Item, TEXT("questId"), Quest) || !FRPGNarrativeInteractionCommand::IsValidId(Quest)
            || !StringField(**Item, TEXT("objectiveId"), Goal) || !FRPGNarrativeInteractionCommand::IsValidId(Goal)) return false;
        const FString Key = Quest + TEXT("/") + Goal;
        if (Seen.Contains(Key)) return false;
        Seen.Add(Key);
    }
    return true;
}
