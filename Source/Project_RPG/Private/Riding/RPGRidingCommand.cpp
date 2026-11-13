#include "Riding/RPGRidingCommand.h"
#include "Riding/RPGRidingIntentJson.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace
{
    FString Id(FGuid G) { return G.ToString(EGuidFormats::DigitsWithHyphensLower); }
    bool Serialize(const TSharedRef<FJsonObject>& O, FString& Text)
    { return FJsonSerializer::Serialize(O, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)); }
    void Optional(const TSharedRef<FJsonObject>& O, const TCHAR* Key, const FString& Value)
    { if (Value.IsEmpty()) O->SetField(Key, MakeShared<FJsonValueNull>()); else O->SetStringField(Key, Value); }
    bool RegisterBody(FGuid Request, FGuid Item, int64 Revision, int32 Version, FString& Body)
    {
        Body.Reset(); if (!Request.IsValid() || !Item.IsValid() || Revision <= 0 || Revision == MAX_int64 || Version <= 0) return false;
        auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("requestId"), Id(Request)); O->SetStringField(TEXT("itemId"), Id(Item));
        O->SetStringField(TEXT("expectedItemRevision"), LexToString(Revision)); O->SetNumberField(TEXT("catalogVersion"), Version); return Serialize(O, Body);
    }
    bool PreferencesBody(FGuid Request, const FRPGRidingPreferencesCommand& C, FString& Body)
    {
        Body.Reset(); if (!Request.IsValid() || C.ExpectedRevision < 0 || C.ExpectedRevision == MAX_int64 || C.Favorites.Num() > RPGRiding::MaxFavorites
            || (!C.SelectedMountId.IsEmpty() && !RPGRiding::IsValidMountId(C.SelectedMountId))) return false;
        if (C.Shortcut.Kind == ERPGRidingSelectorKind::Specific)
        { if (!RPGRiding::IsValidMountId(C.Shortcut.MountId)) return false; }
        else if ((C.Shortcut.Kind != ERPGRidingSelectorKind::Selected && C.Shortcut.Kind != ERPGRidingSelectorKind::RandomFavorites) || !C.Shortcut.MountId.IsEmpty()) return false;
        TSet<FString> Seen; auto Favorites = C.Favorites;
        for (const auto& Mount : Favorites) { if (!RPGRiding::IsValidMountId(Mount) || Seen.Contains(Mount)) return false; Seen.Add(Mount); }
        Favorites.Sort(); TArray<TSharedPtr<FJsonValue>> Values; for (const auto& Mount : Favorites) Values.Add(MakeShared<FJsonValueString>(Mount));
        auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("requestId"), Id(Request)); O->SetStringField(TEXT("expectedRevision"), LexToString(C.ExpectedRevision));
        Optional(O, TEXT("selectedMountId"), C.SelectedMountId); O->SetArrayField(TEXT("favoriteMountIds"), Values);
        auto Shortcut = MakeShared<FJsonObject>(); Shortcut->SetStringField(TEXT("mode"), C.Shortcut.Kind == ERPGRidingSelectorKind::Specific ? TEXT("Specific")
            : C.Shortcut.Kind == ERPGRidingSelectorKind::Selected ? TEXT("Selected") : TEXT("RandomFavorites")); Optional(Shortcut, TEXT("mountId"), C.Shortcut.MountId);
        O->SetObjectField(TEXT("shortcut"), Shortcut); return Serialize(O, Body);
    }
    bool Canonical(ERPGRidingPersistentOperation Operation, const FString& Text, FGuid& Request, FString& Body)
    {
        using namespace RPGRidingIntentJson;
        if (Text.IsEmpty() || Text.Len() > 16384 || !UniqueJson(Text)) return false;
        TSharedPtr<FJsonObject> O; if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), O) || !O || !Guid(O, TEXT("requestId"), Request)) return false;
        if (Operation == ERPGRidingPersistentOperation::Register)
        {
            FGuid Item; int64 Revision = 0; int32 Version = 0;
            return O->Values.Num() == 4 && Guid(O, TEXT("itemId"), Item) && Integer(O, TEXT("expectedItemRevision"), Revision)
                && Number(O, TEXT("catalogVersion"), Version) && RegisterBody(Request, Item, Revision, Version, Body);
        }
        if (Operation != ERPGRidingPersistentOperation::Preferences || O->Values.Num() != 5) return false;
        FRPGRidingPreferencesCommand C; const TArray<TSharedPtr<FJsonValue>>* Favorites = nullptr; const TSharedPtr<FJsonObject>* Shortcut = nullptr; FString Mode;
        if (!Integer(O, TEXT("expectedRevision"), C.ExpectedRevision) || !OptionalId(O, TEXT("selectedMountId"), C.SelectedMountId)
            || !O->TryGetArrayField(TEXT("favoriteMountIds"), Favorites) || Favorites->Num() > RPGRiding::MaxFavorites
            || !O->TryGetObjectField(TEXT("shortcut"), Shortcut) || !*Shortcut || (*Shortcut)->Values.Num() != 2
            || !String(*Shortcut, TEXT("mode"), Mode) || !OptionalId(*Shortcut, TEXT("mountId"), C.Shortcut.MountId)) return false;
        for (const auto& F : *Favorites) { FString Value; if (!F || F->Type != EJson::String || !F->TryGetString(Value)) return false; C.Favorites.Add(Value); }
        if (Mode == TEXT("Specific")) C.Shortcut.Kind = ERPGRidingSelectorKind::Specific;
        else if (Mode == TEXT("Selected")) C.Shortcut.Kind = ERPGRidingSelectorKind::Selected;
        else if (Mode == TEXT("RandomFavorites")) C.Shortcut.Kind = ERPGRidingSelectorKind::RandomFavorites;
        else return false;
        return PreferencesBody(Request, C, Body);
    }
}
bool FRPGRidingIntentScope::Create(FString Endpoint, FString Account, FGuid Character, FRPGRidingIntentScope& Out)
{
    Out = {}; Endpoint.TrimStartAndEndInline(); while (Endpoint.EndsWith(TEXT("/"))) Endpoint.LeftChopInline(1);
    FRPGRidingIntentScope C{MoveTemp(Endpoint), MoveTemp(Account), Character}; if (!C.IsValid()) return false; Out = MoveTemp(C); return true;
}
bool FRPGRidingIntentScope::IsValid() const
{
    if (!Character.IsValid() || Account.Len() != 17 || Endpoint.Len() > 2048 || Endpoint.EndsWith(TEXT("/"))
        || !(Endpoint.StartsWith(TEXT("http://")) || Endpoint.StartsWith(TEXT("https://"))) || Endpoint.Contains(TEXT("@"))
        || Endpoint.Contains(TEXT("?")) || Endpoint.Contains(TEXT("#")) || Endpoint.Contains(TEXT("\\"))) return false;
    const int32 Host = Endpoint.Find(TEXT("://")) + 3; if (Endpoint.Len() <= Host || Endpoint[Host] == '/') return false;
    for (TCHAR C : Endpoint) if (FChar::IsControl(C) || FChar::IsWhitespace(C)) return false;
    for (TCHAR C : Account) if (C < '0' || C > '9') return false;
    return true;
}
bool FRPGRidingPendingIntent::IsValid() const
{ FGuid Request; FString CanonicalBody; return Scope.IsValid() && RequestId.IsValid() && Canonical(Operation, Body, Request, CanonicalBody) && Request == RequestId && Body == CanonicalBody; }
FString FRPGRidingPendingIntent::Method() const
{ return IsValid() ? Operation == ERPGRidingPersistentOperation::Register ? TEXT("POST") : TEXT("PUT") : FString(); }
FString FRPGRidingPendingIntent::Path() const
{ return IsValid() ? TEXT("/api/characters/") + Id(Scope.Character) + TEXT("/riding/") + (Operation == ERPGRidingPersistentOperation::Register ? TEXT("register") : TEXT("preferences")) : FString(); }
bool RPGRidingCommand::Register(const FRPGRidingIntentScope& Scope, FGuid Request, FGuid Item, int64 Revision, int32 Version, FRPGRidingPendingIntent& Out)
{
    Out = {}; FString Body; if (!Scope.IsValid() || !RegisterBody(Request, Item, Revision, Version, Body)) return false;
    Out = {Scope, Request, ERPGRidingPersistentOperation::Register, MoveTemp(Body)}; return true;
}
bool RPGRidingCommand::Preferences(const FRPGRidingIntentScope& Scope, FGuid Request, const FRPGRidingPreferencesCommand& Command, FRPGRidingPendingIntent& Out)
{
    Out = {}; FString Body; if (!Scope.IsValid() || !PreferencesBody(Request, Command, Body)) return false;
    Out = {Scope, Request, ERPGRidingPersistentOperation::Preferences, MoveTemp(Body)}; return true;
}
