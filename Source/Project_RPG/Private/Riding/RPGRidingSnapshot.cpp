#include "Riding/RPGRidingSnapshot.h"
#include "Online/RPGDungeonConnectionRecovery.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    bool UniqueJson(const FString& Text)
    {
        auto Reader = TJsonReaderFactory<>::Create(Text); EJsonNotation Kind;
        struct FFrame { bool bObject; TSet<FString> Names; }; TArray<FFrame> Stack;
        int32 Tokens = 0;
        while (Reader->ReadNext(Kind))
        {
            if (++Tokens > 32768 || Kind == EJsonNotation::Error) return false;
            const bool bEnd = Kind == EJsonNotation::ObjectEnd || Kind == EJsonNotation::ArrayEnd;
            if (!bEnd && !Stack.IsEmpty() && Stack.Last().bObject)
            {
                const FString Name = Reader->GetIdentifier();
                if (Stack.Last().Names.Contains(Name)) return false;
                Stack.Last().Names.Add(Name);
            }
            if (Kind == EJsonNotation::ObjectStart || Kind == EJsonNotation::ArrayStart)
            { if (Stack.Num() >= 8) return false; Stack.Add({Kind == EJsonNotation::ObjectStart, {}}); }
            else if (bEnd) { if (Stack.IsEmpty()) return false; Stack.Pop(); }
        }
        return Stack.IsEmpty() && Reader->GetErrorMessage().IsEmpty();
    }
    bool Integer(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, int64& Out)
    {
        FString S; if (!O || !O->HasTypedField<EJson::String>(Key) || !O->TryGetStringField(Key, S) || S.IsEmpty() || S.Len() > 19 || (S.Len() > 1 && S[0] == '0')) return false;
        for (TCHAR C : S) if (C < '0' || C > '9') return false;
        if (S.Len() == 19 && S.Compare(TEXT("9223372036854775807"), ESearchCase::CaseSensitive) > 0) return false;
        return LexTryParseString(Out, *S);
    }
    bool Number(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, int32& Out)
    {
        double N = 0; if (!O || !O->HasTypedField<EJson::Number>(Key) || !O->TryGetNumberField(Key, N) || !FMath::IsFinite(N) || N < 1 || N > MAX_int32 || FMath::FloorToDouble(N) != N) return false;
        Out = static_cast<int32>(N); return true;
    }
    bool String(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FString& Out)
    { return O && O->HasTypedField<EJson::String>(Key) && O->TryGetStringField(Key, Out); }
    bool Guid(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FGuid& Out)
    { FString S; return String(O, Key, S) && FGuid::ParseExact(S, EGuidFormats::DigitsWithHyphens, Out) && Out.IsValid(); }
    bool Date(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FDateTime& Out)
    { FString S; return String(O, Key, S) && S.Len() <= 40 && FDateTime::ParseIso8601(*S, Out); }
    bool OptionalId(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FString& Out)
    {
        Out.Reset(); if (!O || !O->HasField(Key)) return false;
        if (O->HasTypedField<EJson::Null>(Key)) return true;
        return String(O, Key, Out) && RPGRiding::IsValidMountId(Out);
    }
    bool Scope(IRPGDungeonRecoveryHost& Host, const FRPGRidingSnapshotContext& C)
    {
        FGuid Session;
        return IsInGameThread() && IsValid(&Host.GetRecoveryOwner()) && C.World.IsValid() && !C.World->bIsTearingDown
            && Host.GetRecoveryOwner().GetWorld() == C.World && Host.IsRecoveryServerRuntime() && Host.IsRecoveryServerConfigured()
            && Host.HasValidRecoveryServiceCredential() && Host.GetRecoveryServerId() == C.ServerId
            && FGuid::Parse(Host.GetRecoverySessionId(), Session) && Session == C.Ticket.Identity.DungeonSessionId;
    }
}

bool RPGRidingSnapshot::Decode(const FString& Body, const FRPGRidingSnapshotContext& E, FDateTime ReceivedUtc,
    double ReceivedMonotonic, FRPGRidingDecodedSnapshot& Out)
{
    Out = {};
    TArray<FText> Errors;
    if (Body.IsEmpty() || Body.Len() > MaximumBytes || !E.Ticket.Identity.IsValid() || !E.Ticket.RequestId.IsValid()
        || !E.Policy.Validate(Errors) || E.ServerId.IsEmpty() || !FMath::IsFinite(ReceivedMonotonic) || ReceivedMonotonic < E.SentMonotonic || !UniqueJson(Body)) return false;
    TSharedPtr<FJsonObject> O; if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), O) || !O) return false;
    FRPGRidingDecodedSnapshot Value; auto& S = Value.Snapshot; S.Identity = E.Ticket.Identity;
    FString Server, Hash; FGuid Character, Session; int64 Generation = 0; FDateTime Observed, Deadline;
    if (!String(O, TEXT("serverId"), Server) || Server != E.ServerId
        || !Guid(O, TEXT("dungeonSessionId"), Session) || Session != S.Identity.DungeonSessionId
        || !Guid(O, TEXT("characterId"), Character) || Character != S.Identity.CharacterId || !Guid(O, TEXT("rosterId"), S.RosterId)
        || !Integer(O, TEXT("connectionGeneration"), Generation) || Generation != S.Identity.ConnectionGeneration
        || !Integer(O, TEXT("rosterRevision"), S.OwnershipRevision) || !Integer(O, TEXT("preferencesRevision"), S.PreferencesRevision)
        || !Number(O, TEXT("protocolVersion"), S.ProtocolVersion) || S.ProtocolVersion != E.Policy.ProtocolVersion
        || !Number(O, TEXT("catalogVersion"), S.CatalogVersion) || S.CatalogVersion != E.Policy.CatalogVersion
        || !String(O, TEXT("gameplayHash"), Hash) || Hash != E.Policy.GameplayHash
        || !Date(O, TEXT("observedAtUtc"), Observed) || !Date(O, TEXT("validUntilUtc"), Deadline)
        || Deadline <= ReceivedUtc || Deadline <= Observed || Observed > ReceivedUtc + FTimespan::FromSeconds(5)
        || Observed < E.SentUtc - FTimespan::FromSeconds(5)) return false;
    S.GameplayHash = Hash;
    Value.ValidForSeconds = FMath::Min((Deadline - ReceivedUtc).GetTotalSeconds(),
        (Deadline - Observed).GetTotalSeconds() - (ReceivedMonotonic - E.SentMonotonic));
    if (!FMath::IsFinite(Value.ValidForSeconds) || Value.ValidForSeconds <= 0) return false;
    const TArray<TSharedPtr<FJsonValue>>* Mounts = nullptr;
    if (!O->TryGetArrayField(TEXT("mounts"), Mounts) || Mounts->Num() > RPGRiding::MaxMountDefinitions) return false;
    TSet<FString> Owned;
    for (const auto& Entry : *Mounts)
    {
        if (!Entry || Entry->Type != EJson::Object) return false; const auto M = Entry->AsObject(); FString Id; int32 Version = 0; FGuid Source; FDateTime Unlocked;
        if (!M || !String(M, TEXT("mountId"), Id) || !RPGRiding::IsValidMountId(Id) || Owned.Contains(Id)
            || !Number(M, TEXT("definitionVersion"), Version) || !Guid(M, TEXT("sourceRequestId"), Source)
            || !Date(M, TEXT("unlockedAtUtc"), Unlocked) || Unlocked > Observed + FTimespan::FromSeconds(5)) return false;
        Owned.Add(Id); S.OwnedMountIds.Add(Id);
    }
    const TSharedPtr<FJsonObject>* P = nullptr; const TSharedPtr<FJsonObject>* Shortcut = nullptr; const TArray<TSharedPtr<FJsonValue>>* Favorites = nullptr;
    if (!O->TryGetObjectField(TEXT("preferences"), P) || !*P || !OptionalId(*P, TEXT("selectedMountId"), S.SelectedMountId)
        || (!S.SelectedMountId.IsEmpty() && !Owned.Contains(S.SelectedMountId))
        || !(*P)->TryGetArrayField(TEXT("favoriteMountIds"), Favorites) || Favorites->Num() > RPGRiding::MaxFavorites
        || !(*P)->TryGetObjectField(TEXT("shortcut"), Shortcut) || !*Shortcut) return false;
    TSet<FString> Seen;
    for (const auto& F : *Favorites)
    { FString Id; if (!F || F->Type != EJson::String || !F->TryGetString(Id) || !Owned.Contains(Id) || Seen.Contains(Id)) return false; Seen.Add(Id); S.Favorites.Add(Id); }
    FString Mode;
    if (!String(*Shortcut, TEXT("mode"), Mode) || !OptionalId(*Shortcut, TEXT("mountId"), S.Shortcut.MountId)) return false;
    if (Mode == TEXT("Selected")) S.Shortcut.Kind = ERPGRidingSelectorKind::Selected;
    else if (Mode == TEXT("RandomFavorites")) S.Shortcut.Kind = ERPGRidingSelectorKind::RandomFavorites;
    else if (Mode == TEXT("Specific")) S.Shortcut.Kind = ERPGRidingSelectorKind::Specific;
    else return false;
    if (S.Shortcut.Kind == ERPGRidingSelectorKind::Specific ? !Owned.Contains(S.Shortcut.MountId) : !S.Shortcut.MountId.IsEmpty()) return false;
    Out = MoveTemp(Value); return true;
}

FHttpRequestPtr RPGRidingSnapshot::Create(IRPGDungeonRecoveryHost& Host, const FRPGRidingSnapshotContext& C,
    TFunction<void(const FRPGRidingSnapshotResponse&)> Completion)
{
    TArray<FText> Errors;
    if (!Completion || !C.Ticket.Identity.IsValid() || !C.Ticket.RequestId.IsValid() || !C.Policy.Validate(Errors) || !Scope(Host, C)) return nullptr;
    const FString Endpoint = Host.GetRecoveryApiUrl(); FString Url = Endpoint; Url.RemoveFromEnd(TEXT("/"));
    if (!Url.StartsWith(TEXT("https://")) && !Url.StartsWith(TEXT("http://"))) return nullptr;
    Url += FString::Printf(TEXT("/api/game-servers/sessions/%s/members/%s/riding?connectionGeneration=%lld&catalogVersion=%d&gameplayHash=%s"),
        *C.Ticket.Identity.DungeonSessionId.ToString(EGuidFormats::DigitsWithHyphensLower), *C.Ticket.Identity.CharacterId.ToString(EGuidFormats::DigitsWithHyphensLower),
        C.Ticket.Identity.ConnectionGeneration, C.Policy.CatalogVersion, *C.Policy.GameplayHash);
    auto Request = FHttpModule::Get().CreateRequest(); Request->SetURL(Url); Request->SetVerb(TEXT("GET"));
    Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
    Request->SetHeader(TEXT("Accept"), TEXT("application/json")); Host.ApplyRecoveryServiceAuthorization(Request);
    const FString Authorization = Request->GetHeader(TEXT("Authorization"));
    if (!Authorization.StartsWith(TEXT("Bearer ")) || Authorization.Len() <= 7) return nullptr;
    Request->SetTimeout(FMath::Clamp(Host.GetRecoveryRequestTimeout(), 1.f, 30.f));
    const TWeakObjectPtr<UObject> Owner = &Host.GetRecoveryOwner(); auto* HostPtr = &Host;
    Request->OnProcessRequestComplete().BindLambda([Owner, HostPtr, C, Endpoint, Authorization, Completion = MoveTemp(Completion)](FHttpRequestPtr, FHttpResponsePtr Http, bool bTransport) mutable
    {
        FRPGRidingSnapshotResponse Result;
        if (Owner.IsValid() && Scope(*HostPtr, C) && HostPtr->GetRecoveryApiUrl() == Endpoint)
        {
            auto Probe = FHttpModule::Get().CreateRequest(); HostPtr->ApplyRecoveryServiceAuthorization(Probe);
            Result.bScopeCurrent = Probe->GetHeader(TEXT("Authorization")) == Authorization;
        }
        Result.StatusCode = Http ? Http->GetResponseCode() : 0;
        Result.bTransportSuccessful = bTransport && Http.IsValid();
        if (Http && Http->GetContent().Num() <= MaximumBytes) Result.Body = Http->GetContentAsString();
        Completion(MoveTemp(Result));
    });
    return Request;
}
