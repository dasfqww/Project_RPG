#include "Trading/RPGTradePreview.h"
#include "RPGTradeJson.h"
#include "Dom/JsonObject.h"

namespace RPGTradePreview
{
using namespace RPGTradeClient;
bool Text(const FObject& Obj, const TCHAR* Key, FString& Out)
{
    if (!String(Obj, Key, Out) || Out.IsEmpty() || Out.Len() > 128 || Out.TrimStartAndEnd().IsEmpty()) return false;
    for (TCHAR C : Out) if (FChar::IsControl(C)) return false;
    return true;
}
bool Number(const FObject& Obj, const TCHAR* Key, int32& Out, int32 Minimum = 0)
{
    double N;
    if (!Obj || !Obj->HasTypedField<EJson::Number>(Key) || !Obj->TryGetNumberField(Key,N) || !FMath::IsFinite(N)
        || N < Minimum || N > MAX_int32 || FMath::FloorToDouble(N) != N) return false;
    Out = static_cast<int32>(N); return true;
}
bool NullableDate(const FObject& Obj, const TCHAR* Key, TOptional<FDateTime>& Out)
{
    const auto V = Obj->TryGetField(Key);
    if (!V) return false;
    if (V->Type == EJson::Null) return true;
    FDateTime D; if (!Date(Obj,Key,D)) return false; Out = D; return true;
}
bool Details(const FObject& Obj, const FDateTime& Observed, FRPGTradeItemDetails& Out)
{
    if (!Text(Obj,TEXT("definitionType"),Out.DefinitionType) || !Text(Obj,TEXT("definitionName"),Out.DefinitionName)
        || !Number(Obj,TEXT("definitionVersion"),Out.DefinitionVersion,1) || !Text(Obj,TEXT("bindState"),Out.BindState)
        || (Out.BindState != TEXT("Unbound") && Out.BindState != TEXT("BindOnEquip"))
        || !NullableDate(Obj,TEXT("expiresAtUtc"),Out.ExpiresAt) || (Out.ExpiresAt.IsSet() && Out.ExpiresAt.GetValue() <= Observed)) return false;
    const FObject* Durability;
    if (!Obj->TryGetObjectField(TEXT("durability"),Durability) || !Number(*Durability,TEXT("current"),Out.Durability)
        || !Number(*Durability,TEXT("maximum"),Out.MaximumDurability) || Out.Durability > Out.MaximumDurability) return false;
    const TArray<TSharedPtr<FJsonValue>> *Tags, *Stats;
    if (!Obj->TryGetArrayField(TEXT("instanceTags"),Tags) || Tags->Num() > 64 || !Obj->TryGetArrayField(TEXT("statValues"),Stats) || Stats->Num() > 128) return false;
    TSet<FString> Seen;
    for (const auto& V : *Tags)
    {
        if (!V || V->Type != EJson::String) return false;
        const FString Tag = V->AsString();
        if (Tag.IsEmpty() || Tag.Len() > 128 || Tag.TrimStartAndEnd().IsEmpty() || Seen.Contains(Tag)) return false;
        for (TCHAR C : Tag) if (FChar::IsControl(C)) return false;
        Seen.Add(Tag); Out.Tags.Add(Tag);
    }
    Seen.Reset();
    for (const auto& V : *Stats)
    {
        if (!V || V->Type != EJson::Object) return false;
        const auto Entry = V->AsObject(); FRPGTradePreviewStat Stat;
        if (!Text(Entry,TEXT("statTag"),Stat.Tag) || Seen.Contains(Stat.Tag) || !Entry->HasTypedField<EJson::Number>(TEXT("value"))
            || !Entry->TryGetNumberField(TEXT("value"),Stat.Value) || !FMath::IsFinite(Stat.Value)) return false;
        Seen.Add(Stat.Tag); Out.Stats.Add(Stat);
    }
    const auto Growth = Obj->TryGetField(TEXT("equipmentProgression"));
    if (!Growth) return false;
    if (Growth->Type != EJson::Null)
    {
        if (Growth->Type != EJson::Object) return false;
        FRPGTradePreviewGrowth G;
        if (!Text(Growth->AsObject(),TEXT("growthBandId"),G.Band) || !Number(Growth->AsObject(),TEXT("ruleVersion"),G.RuleVersion,1)
            || !Number(Growth->AsObject(),TEXT("level"),G.Level)) return false;
        Out.Growth = G;
    }
    return true;
}
bool Items(const FObject& Obj, const TCHAR* Key, const FRPGTradeOffer& Expected, const FDateTime& Observed, bool bOpen, TArray<FRPGTradePreviewItem>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Array;
    if (!Obj->TryGetArrayField(Key,Array) || Array->Num() != Expected.Items.Num() || Array->Num() > 8) return false;
    for (int32 I=0; I<Array->Num(); ++I)
    {
        const auto& V = (*Array)[I]; if (!V || V->Type != EJson::Object) return false;
        const auto Entry = V->AsObject(); FRPGTradePreviewItem Item;
        FString Availability;
        if (!Guid(Entry,TEXT("itemId"),Item.Offered.ItemId) || !Integer(Entry,TEXT("expectedRevision"),Item.Offered.ExpectedRevision,true)
            || !Number(Entry,TEXT("quantity"),Item.Offered.Quantity,1) || !(Item.Offered == Expected.Items[I])
            || !String(Entry,TEXT("availability"),Availability)) return false;
        const auto Detail = Entry->TryGetField(TEXT("details")); if (!Detail) return false;
        if (Availability == TEXT("Available"))
        {
            if (!bOpen || Detail->Type != EJson::Object) return false;
            FRPGTradeItemDetails D; if (!Details(Detail->AsObject(),Observed,D)) return false;
            Item.Details = MoveTemp(D);
        }
        else if (Availability != TEXT("Unavailable") || Detail->Type != EJson::Null) return false;
        Out.Add(MoveTemp(Item));
    }
    return true;
}
}
bool FRPGTradeItemDetails::operator==(const FRPGTradeItemDetails& O) const
{
    return DefinitionType == O.DefinitionType && DefinitionName == O.DefinitionName && DefinitionVersion == O.DefinitionVersion
        && BindState == O.BindState && Durability == O.Durability && MaximumDurability == O.MaximumDurability
        && Tags == O.Tags && Stats == O.Stats && Growth == O.Growth && ExpiresAt == O.ExpiresAt;
}
bool FRPGTradePreview::SameContent(const FRPGTradePreview& O) const
{ return Session == O.Session && bOpen == O.bOpen && bAllAvailable == O.bAllAvailable && InitiatorItems == O.InitiatorItems && InviteeItems == O.InviteeItems; }
bool FRPGTradePreview::Decode(const FString& Body, FGuid Character, const FRPGTradeSession& Expected, FRPGTradePreview& Out)
{
    using namespace RPGTradeClient;
    FObject Obj; FRPGTradePreview P; const FObject* SessionObj;
    if (!ReadObject(Body,Obj,131072) || !Obj->TryGetObjectField(TEXT("session"),SessionObj) || !ReadSession(*SessionObj,Character,P.Session)
        || !(P.Session == Expected) || !Date(Obj,TEXT("observedAt"),P.ObservedAt) || !Boolean(Obj,TEXT("isOpen"),P.bOpen)
        || P.bOpen != (P.Session.IsOpen() && P.ObservedAt < P.Session.ExpiresAt) || !Boolean(Obj,TEXT("allItemsAvailable"),P.bAllAvailable)
        || !RPGTradePreview::Items(Obj,TEXT("initiatorItems"),P.Session.InitiatorOffer,P.ObservedAt,P.bOpen,P.InitiatorItems)
        || !RPGTradePreview::Items(Obj,TEXT("inviteeItems"),P.Session.InviteeOffer,P.ObservedAt,P.bOpen,P.InviteeItems)) return false;
    bool All = P.bOpen;
    for (const auto& Item : P.InitiatorItems) All &= Item.Details.IsSet();
    for (const auto& Item : P.InviteeItems) All &= Item.Details.IsSet();
    if (All != P.bAllAvailable) return false;
    Out = MoveTemp(P); return true;
}
