#include "Narrative/RPGNarrativeKill.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FRPGNarrativeKillCommand::IsValid() const
{
    return SessionId.IsValid() && CharacterId.IsValid() && EventId.IsValid() && ConnectionGeneration > 0
        && FRPGNarrativeInteractionCommand::IsValidId(EnemyId) && ObservedAt.GetTicks() > 0;
}
FString FRPGNarrativeKillCommand::Encode() const
{
    if (!IsValid()) return {};
    const auto Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("eventId"), EventId.ToString(EGuidFormats::DigitsWithHyphensLower));
    Object->SetStringField(TEXT("enemyId"), EnemyId);
    Object->SetStringField(TEXT("connectionGeneration"), LexToString(ConnectionGeneration));
    Object->SetStringField(TEXT("observedAtUtc"), ObservedAt.ToIso8601());
    FString Json;
    FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
    return Json;
}
bool FRPGNarrativeKillAttempt::Begin(const FRPGNarrativeKillCommand& Command)
{
    if (Pending || !Command.IsValid()) return false;
    Pending = Command; Body = Command.Encode(); Attempts = 0; return true;
}
bool FRPGNarrativeKillAttempt::StartRequest()
{
    if (!Pending || bInFlight) return false;
    bInFlight = true; Attempts = FMath::Min(Attempts + 1, 32); return true;
}
ERPGNarrativeInteractionOutcome FRPGNarrativeKillAttempt::Receive(const FRPGNarrativeInteractionHttpResult& Result)
{
    if (!Pending || !bInFlight) return ERPGNarrativeInteractionOutcome::Unknown;
    bInFlight = false;
    if (!Result.bCurrentServerContext || !Result.bTransportSuccessful) return ERPGNarrativeInteractionOutcome::Unknown;
    if (Result.StatusCode == 200 && DecodeReceipt(Result.Body, Pending.GetValue()))
    { Pending.Reset(); return ERPGNarrativeInteractionOutcome::Succeeded; }
    // The repository returns an exact stored receipt before checking this window.
    // A typed window rejection is the only error that safely resolves an uncertain write here.
    if (Result.StatusCode == 409 && Result.Body.Len() < 4096)
    {
        TSharedPtr<FJsonObject> Error;
        if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Result.Body), Error) && Error)
            if (const auto Code = Error->TryGetField(TEXT("error")); Code && Code->Type == EJson::String
                && Code->AsString() == TEXT("kill_event_out_of_window"))
            { Pending.Reset(); return ERPGNarrativeInteractionOutcome::Rejected; }
    }
    return ERPGNarrativeInteractionOutcome::Unknown;
}
bool FRPGNarrativeKillAttempt::DecodeReceipt(const FString& Json, const FRPGNarrativeKillCommand& Expected)
{
    if (!Expected.IsValid()) return false;
    FRPGNarrativeInteractionCommand Identity;
    Identity.EventId = Expected.EventId; Identity.CharacterId = Expected.CharacterId;
    // Shared identity/revision/unique objective validation, followed by kill-specific numeric validation.
    if (!FRPGNarrativeInteractionAttempt::DecodeReceipt(Json, Identity)) return false;
    TSharedPtr<FJsonObject> Object;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object) return false;
    for (const auto& Value : Object->GetArrayField(TEXT("appliedObjectives")))
    {
        const auto Item = Value->AsObject();
        const auto Count = Item->TryGetField(TEXT("count")), Required = Item->TryGetField(TEXT("requiredCount"));
        if (!Count || !Required || Count->Type != EJson::Number || Required->Type != EJson::Number) return false;
        const double C = Count->AsNumber(), R = Required->AsNumber();
        if (!FMath::IsFinite(C) || !FMath::IsFinite(R) || R < 1 || R > 10000 || C < 1 || C > R
            || FMath::FloorToDouble(C) != C || FMath::FloorToDouble(R) != R) return false;
    }
    return true;
}
void FRPGNarrativeKillLife::Arm(const bool bServerReady, const float Health, const bool bDead)
{
    if (!bConsumed && bServerReady && FMath::IsFinite(Health) && Health > 0.f && !bDead) bArmed = true;
}
bool FRPGNarrativeKillLife::Consume(const bool bServerReady, const float Health, const bool bDead)
{
    if (!bArmed || bConsumed || !bServerReady || !FMath::IsFinite(Health) || Health != 0.f || !bDead) return false;
    bConsumed = true; return true;
}
