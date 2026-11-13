#include "Riding/RPGRidingCollection.h"
#include "Dom/JsonObject.h"
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
}

bool RPGRidingCollection::Decode(const FString& Body, FGuid ExpectedCharacter, FRPGRidingCollection& Out)
{
    Out = {};
    if (!ExpectedCharacter.IsValid() || Body.IsEmpty() || Body.Len() > MaximumBytes || !UniqueJson(Body)) return false;
    TSharedPtr<FJsonObject> O;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), O) || !O) return false;
    FRPGRidingCollection C;
    bool bPreferencesReady = false;
    auto Boolean = [&O](const TCHAR* Key, bool& Value)
    { return O->HasTypedField<EJson::Boolean>(Key) && O->TryGetBoolField(Key, Value); };
    if (!Guid(O, TEXT("characterId"), C.CharacterId) || C.CharacterId != ExpectedCharacter || !Guid(O, TEXT("rosterId"), C.RosterId)
        || !Integer(O, TEXT("rosterRevision"), C.RosterRevision) || !Integer(O, TEXT("preferencesRevision"), C.PreferencesRevision)
        || !Boolean(TEXT("catalogReady"), C.bCatalogReady) || !Boolean(TEXT("registrationEnabled"), C.bRegistrationEnabled)
        || !Boolean(TEXT("preferencesReady"), bPreferencesReady) || !bPreferencesReady
        || !Boolean(TEXT("preferencesEnabled"), C.bPreferencesEnabled)
        || C.bRegistrationEnabled != C.bCatalogReady || C.bPreferencesEnabled != C.bCatalogReady) return false;
    if (C.bCatalogReady)
    {
        if (!Number(O, TEXT("catalogVersion"), C.CatalogVersion) || !String(O, TEXT("gameplayHash"), C.GameplayHash) || C.GameplayHash.Len() != 64) return false;
        for (TCHAR Ch : C.GameplayHash) if (!((Ch >= '0' && Ch <= '9') || (Ch >= 'a' && Ch <= 'f'))) return false;
    }
    else if (!O->HasTypedField<EJson::Null>(TEXT("catalogVersion")) || !O->HasTypedField<EJson::Null>(TEXT("gameplayHash"))) return false;
    const TArray<TSharedPtr<FJsonValue>>* Mounts = nullptr;
    if (!O->TryGetArrayField(TEXT("mounts"), Mounts) || Mounts->Num() > RPGRiding::MaxMountDefinitions) return false;
    TSet<FString> Owned;
    for (const auto& Entry : *Mounts)
    {
        if (!Entry || Entry->Type != EJson::Object) return false; const auto M = Entry->AsObject(); FRPGRidingOwnedMount Value;
        if (!String(M, TEXT("mountId"), Value.MountId) || !RPGRiding::IsValidMountId(Value.MountId) || Owned.Contains(Value.MountId)
            || !Number(M, TEXT("definitionVersion"), Value.DefinitionVersion) || !Date(M, TEXT("unlockedAtUtc"), Value.UnlockedAtUtc)
            || !Guid(M, TEXT("sourceRequestId"), Value.SourceRequestId)) return false;
        Owned.Add(Value.MountId); C.Mounts.Add(MoveTemp(Value));
    }
    const TSharedPtr<FJsonObject>* P = nullptr; const TSharedPtr<FJsonObject>* Shortcut = nullptr; const TArray<TSharedPtr<FJsonValue>>* Favorites = nullptr;
    if (!O->TryGetObjectField(TEXT("preferences"), P) || !*P || !OptionalId(*P, TEXT("selectedMountId"), C.SelectedMountId)
        || (!C.SelectedMountId.IsEmpty() && !Owned.Contains(C.SelectedMountId))
        || !(*P)->TryGetArrayField(TEXT("favoriteMountIds"), Favorites) || Favorites->Num() > RPGRiding::MaxFavorites
        || !(*P)->TryGetObjectField(TEXT("shortcut"), Shortcut) || !*Shortcut) return false;
    TSet<FString> Seen;
    for (const auto& F : *Favorites)
    { FString Id; if (!F || F->Type != EJson::String || !F->TryGetString(Id) || !Owned.Contains(Id) || Seen.Contains(Id)) return false; Seen.Add(Id); C.Favorites.Add(Id); }
    FString Mode;
    if (!String(*Shortcut, TEXT("mode"), Mode) || !OptionalId(*Shortcut, TEXT("mountId"), C.Shortcut.MountId)) return false;
    if (Mode == TEXT("Selected")) C.Shortcut.Kind = ERPGRidingSelectorKind::Selected;
    else if (Mode == TEXT("RandomFavorites")) C.Shortcut.Kind = ERPGRidingSelectorKind::RandomFavorites;
    else if (Mode == TEXT("Specific")) C.Shortcut.Kind = ERPGRidingSelectorKind::Specific;
    else return false;
    if (C.Shortcut.Kind == ERPGRidingSelectorKind::Specific ? !Owned.Contains(C.Shortcut.MountId) : !C.Shortcut.MountId.IsEmpty()) return false;
    Out = MoveTemp(C); return true;
}
