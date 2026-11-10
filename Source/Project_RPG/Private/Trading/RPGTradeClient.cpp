#include "Trading/RPGTradeClient.h"
#include "RPGTradeJson.h"

#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace RPGTradeClient
{

FString Id(const FGuid& Value) { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
FString Decimal(int64 Value) { return LexToString(Value); }

// Check depth/duplicate names before the DOM parser can collapse duplicate keys.
bool ReadObject(const FString& Text, FObject& Out, int32 MaximumBytes)
{
    if (MaximumBytes < 1 || MaximumBytes > 4*1024*1024 || Text.IsEmpty() || Text.Len() > MaximumBytes || FTCHARToUTF8(*Text).Length() > MaximumBytes) return false;
    struct FScope { bool bObject; TSet<FString> Keys; };
    TArray<FScope> Stack;
    auto Reader = TJsonReaderFactory<>::Create(Text);
    EJsonNotation Token;
    int32 Roots = 0;
    while (Reader->ReadNext(Token))
    {
        if (Token == EJsonNotation::Error) return false;
        if (Token == EJsonNotation::ObjectEnd || Token == EJsonNotation::ArrayEnd)
        {
            if (Stack.IsEmpty()) return false;
            Stack.Pop();
            continue;
        }
        if (Stack.IsEmpty()) { if (++Roots != 1 || Token != EJsonNotation::ObjectStart) return false; }
        else if (Stack.Last().bObject)
        {
            const FString& Key = Reader->GetIdentifier();
            if (Stack.Last().Keys.Contains(Key)) return false;
            Stack.Last().Keys.Add(Key);
        }
        if (Token == EJsonNotation::ObjectStart || Token == EJsonNotation::ArrayStart)
        {
            if (Stack.Num() >= 12) return false;
            Stack.Add({Token == EJsonNotation::ObjectStart, {}});
        }
    }
    return Roots == 1 && Stack.IsEmpty() && Reader->GetErrorMessage().IsEmpty()
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid();
}

bool String(const FObject& Obj, const TCHAR* Key, FString& Out)
{
    return Obj && Obj->HasTypedField<EJson::String>(Key) && Obj->TryGetStringField(Key, Out);
}
bool Guid(const FObject& Obj, const TCHAR* Key, FGuid& Out)
{
    FString Value;
    return String(Obj, Key, Value) && FGuid::ParseExact(Value, EGuidFormats::DigitsWithHyphens, Out) && Out.IsValid();
}
bool Integer(const FObject& Obj, const TCHAR* Key, int64& Out, bool bPositive)
{
    FString Value;
    if (!String(Obj, Key, Value) || Value.IsEmpty() || Value.Len() > 19 || (Value.Len() > 1 && Value[0] == '0')) return false;
    int64 Number = 0;
    for (TCHAR C : Value)
    {
        if (C < '0' || C > '9' || Number > (MAX_int64 - (C - '0')) / 10) return false;
        Number = Number * 10 + (C - '0');
    }
    if (bPositive && Number == 0) return false;
    Out = Number;
    return true;
}
bool Date(const FObject& Obj, const TCHAR* Key, FDateTime& Out)
{
    FString Value;
    return String(Obj, Key, Value) && Value.Len() <= 40 && FDateTime::ParseIso8601(*Value, Out);
}
bool Boolean(const FObject& Obj, const TCHAR* Key, bool& Out)
{
    return Obj && Obj->HasTypedField<EJson::Boolean>(Key) && Obj->TryGetBoolField(Key, Out);
}
bool ReadOffer(const FObject& Obj, FRPGTradeOffer& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Items;
    if (!Obj || !Integer(Obj, TEXT("currencyAmount"), Out.CurrencyAmount, false)
        || !Obj->TryGetArrayField(TEXT("items"), Items) || Items->Num() > 8) return false;
    for (const auto& Value : *Items)
    {
        if (!Value || Value->Type != EJson::Object) return false;
        const auto ItemObj = Value->AsObject();
        FRPGTradeItem Item;
        double Quantity;
        if (!Guid(ItemObj, TEXT("itemId"), Item.ItemId) || !Integer(ItemObj, TEXT("expectedRevision"), Item.ExpectedRevision, true)
            || !ItemObj->HasTypedField<EJson::Number>(TEXT("quantity")) || !ItemObj->TryGetNumberField(TEXT("quantity"), Quantity)
            || !FMath::IsFinite(Quantity) || Quantity < 1 || Quantity > MAX_int32 || FMath::FloorToDouble(Quantity) != Quantity) return false;
        Item.Quantity = static_cast<int32>(Quantity);
        Out.Items.Add(Item);
    }
    return Out.IsValid();
}
bool ReadSession(const FObject& Obj, const FGuid& Character, FRPGTradeSession& Out)
{
    if (!Obj || !Guid(Obj, TEXT("tradeId"), Out.TradeId) || !Guid(Obj, TEXT("initiatorCharacterId"), Out.InitiatorCharacterId)
        || !Guid(Obj, TEXT("inviteeCharacterId"), Out.InviteeCharacterId) || Out.InitiatorCharacterId == Out.InviteeCharacterId
        || (Character != Out.InitiatorCharacterId && Character != Out.InviteeCharacterId)
        || !String(Obj, TEXT("currencyCode"), Out.CurrencyCode) || Out.CurrencyCode.IsEmpty() || Out.CurrencyCode.Len() > 64
        || !Integer(Obj, TEXT("revision"), Out.Revision, true) || !Integer(Obj, TEXT("offerRevision"), Out.OfferRevision, true)
        || Out.OfferRevision > Out.Revision || !Date(Obj, TEXT("expiresAt"), Out.ExpiresAt)) return false;
    for (TCHAR C : Out.CurrencyCode)
        if (!((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '.' || C == '_' || C == '-')) return false;
    FString Status;
    bool bCompleted;
    if (!String(Obj, TEXT("status"), Status) || !Boolean(Obj, TEXT("completed"), bCompleted)
        || !Boolean(Obj, TEXT("initiatorReady"), Out.bInitiatorReady) || !Boolean(Obj, TEXT("inviteeReady"), Out.bInviteeReady)
        || !Boolean(Obj, TEXT("initiatorConfirmed"), Out.bInitiatorConfirmed) || !Boolean(Obj, TEXT("inviteeConfirmed"), Out.bInviteeConfirmed)) return false;
    if (Status == TEXT("Invited")) Out.Status = ERPGTradeStatus::Invited;
    else if (Status == TEXT("Negotiating")) Out.Status = ERPGTradeStatus::Negotiating;
    else if (Status == TEXT("Completed")) Out.Status = ERPGTradeStatus::Completed;
    else if (Status == TEXT("Cancelled")) Out.Status = ERPGTradeStatus::Cancelled;
    else if (Status == TEXT("Expired")) Out.Status = ERPGTradeStatus::Expired;
    else return false;
    if (bCompleted != (Out.Status == ERPGTradeStatus::Completed)) return false;
    const FObject* First;
    const FObject* Second;
    if (!Obj->TryGetObjectField(TEXT("initiatorOffer"), First) || !Obj->TryGetObjectField(TEXT("inviteeOffer"), Second)
        || !ReadOffer(*First, Out.InitiatorOffer) || !ReadOffer(*Second, Out.InviteeOffer)) return false;
    for (const auto& A : Out.InitiatorOffer.Items)
        for (const auto& B : Out.InviteeOffer.Items) if (A.ItemId == B.ItemId) return false;
    const bool bAnyConsent = Out.bInitiatorReady || Out.bInviteeReady || Out.bInitiatorConfirmed || Out.bInviteeConfirmed;
    if ((Out.Status == ERPGTradeStatus::Invited || Out.Status == ERPGTradeStatus::Cancelled || Out.Status == ERPGTradeStatus::Expired) && bAnyConsent) return false;
    if (Out.Status == ERPGTradeStatus::Invited && (!Out.InitiatorOffer.IsEmpty() || !Out.InviteeOffer.IsEmpty())) return false;
    if (bAnyConsent && Out.InitiatorOffer.IsEmpty() && Out.InviteeOffer.IsEmpty()) return false;
    if ((Out.bInitiatorConfirmed || Out.bInviteeConfirmed) && !(Out.bInitiatorReady && Out.bInviteeReady)) return false;
    return bCompleted == (Out.bInitiatorConfirmed && Out.bInviteeConfirmed);
}
FObject WriteOffer(const FRPGTradeOffer& Offer)
{
    auto Obj = MakeShared<FJsonObject>();
    Obj->SetStringField(TEXT("currencyAmount"), Decimal(Offer.CurrencyAmount));
    TArray<TSharedPtr<FJsonValue>> Items;
    for (const auto& Item : Offer.Items)
    {
        auto Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("itemId"), Id(Item.ItemId));
        Entry->SetStringField(TEXT("expectedRevision"), Decimal(Item.ExpectedRevision));
        Entry->SetNumberField(TEXT("quantity"), Item.Quantity);
        Items.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Obj->SetArrayField(TEXT("items"), Items);
    return Obj;
}
const TCHAR* ActionName(ERPGTradeAction Action)
{
    switch (Action)
    {
    case ERPGTradeAction::Invite: return TEXT("invite");
    case ERPGTradeAction::Accept: return TEXT("accept");
    case ERPGTradeAction::Offer: return TEXT("offer");
    case ERPGTradeAction::Ready: return TEXT("ready");
    case ERPGTradeAction::Withdraw: return TEXT("withdraw");
    case ERPGTradeAction::Confirm: return TEXT("confirm");
    case ERPGTradeAction::Cancel: return TEXT("cancel");
    default: return nullptr;
    }
}

bool ReadIntent(const FRPGTradePendingIntent& Intent, FGuid& Invitee)
{
    Invitee.Invalidate();
    const auto& R=Intent.Request;
    if (!Intent.Scope.IsValid() || !R.RequestId.IsValid() || !R.TradeId.IsValid()) return false;
    const FString Prefix=TEXT("/api/characters/")+Id(Intent.Scope.Character)+TEXT("/trades/")+Id(R.TradeId)+TEXT("/");
    const TCHAR* Action=nullptr;
    for (const auto A : {ERPGTradeAction::Invite,ERPGTradeAction::Accept,ERPGTradeAction::Offer,ERPGTradeAction::Ready,ERPGTradeAction::Withdraw,ERPGTradeAction::Confirm,ERPGTradeAction::Cancel})
        if (R.RelativePath==Prefix+ActionName(A)) Action=ActionName(A);
    FObject Obj; FGuid Request; int64 Revision=0,OfferRevision=0;
    if (!Action || !ReadObject(R.Body,Obj,16384) || !Guid(Obj,TEXT("requestId"),Request) || Request!=R.RequestId) return false;
    if (FCString::Strcmp(Action,TEXT("invite"))==0)
        return Obj->Values.Num()==2 && Guid(Obj,TEXT("inviteeCharacterId"),Invitee) && Invitee!=Intent.Scope.Character;
    const bool Consent=FCString::Strcmp(Action,TEXT("ready"))==0 || FCString::Strcmp(Action,TEXT("confirm"))==0;
    const bool Offer=FCString::Strcmp(Action,TEXT("offer"))==0;
    if (Obj->Values.Num()!=(Consent||Offer ? 3 : 2) || !Integer(Obj,TEXT("expectedRevision"),Revision,true) || Revision==MAX_int64) return false;
    if (Consent && (!Integer(Obj,TEXT("expectedOfferRevision"),OfferRevision,true) || OfferRevision==MAX_int64 || OfferRevision>Revision)) return false;
    if (Offer)
    {
        const FObject* OfferObj;FRPGTradeOffer Value;
        if (!Obj->TryGetObjectField(TEXT("offer"),OfferObj) || (*OfferObj)->Values.Num()!=2 || !ReadOffer(*OfferObj,Value)) return false;
        // Unlike session display, an original request must contain integer JSON tokens.
        FObject Lexical;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(R.Body),Lexical,FJsonSerializer::EFlags::StoreNumbersAsStrings)) return false;
        const auto& Rows=(*OfferObj)->GetArrayField(TEXT("items"));
        const auto& RawRows=Lexical->GetObjectField(TEXT("offer"))->GetArrayField(TEXT("items"));
        for (int32 I=0;I<Rows.Num();++I)
        {
            int64 Quantity=0;FString Token;const auto Raw=RawRows[I]->AsObject()->TryGetField(TEXT("quantity"));
            if (Rows[I]->AsObject()->Values.Num()!=3 || !Raw || !Raw->TryGetString(Token))return false;
            auto Number=MakeShared<FJsonObject>();Number->SetStringField(TEXT("quantity"),Token);
            if(!Integer(Number,TEXT("quantity"),Quantity,true) || Quantity>MAX_int32)return false;
        }
    }
    return true;
}
}

bool FRPGTradeIntentScope::Create(FString Endpoint, const FString& Account, FGuid Character, FRPGTradeIntentScope& Out)
{
    Out={};Endpoint.TrimStartAndEndInline();while(Endpoint.EndsWith(TEXT("/")))Endpoint.LeftChopInline(1);
    FRPGTradeIntentScope Candidate{MoveTemp(Endpoint),Account,Character};
    if(!Candidate.IsValid())return false;Out=MoveTemp(Candidate);return true;
}
bool FRPGTradeIntentScope::IsValid() const
{
    if(!Character.IsValid() || Account.IsEmpty() || Account.Len()>128 || Endpoint.Len()<=8 || Endpoint.Len()>2048
        || !(Endpoint.StartsWith(TEXT("http://")) || Endpoint.StartsWith(TEXT("https://"))) || Endpoint.EndsWith(TEXT("/"))
        || Endpoint.Contains(TEXT("@")) || Endpoint.Contains(TEXT("?")) || Endpoint.Contains(TEXT("#")) || Endpoint.Contains(TEXT("\\")))return false;
    for(TCHAR C:Endpoint)if(FChar::IsControl(C)||FChar::IsWhitespace(C))return false;
    for(TCHAR C:Account)if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='.'||C=='_'||C=='-'))return false;
    return true;
}
bool FRPGTradeIntentScope::operator==(const FRPGTradeIntentScope& Other) const
{return Endpoint==Other.Endpoint && Account==Other.Account && Character==Other.Character;}
bool FRPGTradePendingIntent::IsValid() const {FGuid Invitee;return RPGTradeClient::ReadIntent(*this,Invitee);}

bool FRPGTradeClient::ExportPending(FRPGTradePendingIntent& Out) const
{
    Out={};if(!Pending.IsSet())return false;
    FRPGTradePendingIntent Candidate{{BoundEndpoint,BoundAccount,BoundCharacter},*Pending};
    if(!Candidate.IsValid())return false;Out=MoveTemp(Candidate);return true;
}
bool FRPGTradeClient::RestorePending(const FRPGTradePendingIntent& Intent)
{
    FGuid Invitee;
    if(Pending.IsSet() || Session.IsSet() || Intent.Scope.Endpoint!=BoundEndpoint || Intent.Scope.Account!=BoundAccount
        || Intent.Scope.Character!=BoundCharacter || !RPGTradeClient::ReadIntent(Intent,Invitee))return false;
    Generation=FGuid::NewGuid();Sequence=0;AttemptTicket={};ReadTicket={};ReadTradeId.Invalidate();
    Pending=Intent.Request;Pending->Delivery=ERPGTradeDelivery::Unknown;PendingInvitee=Invitee;
    bHadUncertainAttempt=true;LastRejection.Reset();return true;
}

bool FRPGTradeItem::operator==(const FRPGTradeItem& Other) const
{ return ItemId == Other.ItemId && ExpectedRevision == Other.ExpectedRevision && Quantity == Other.Quantity; }
bool FRPGTradeOffer::operator==(const FRPGTradeOffer& Other) const
{ return CurrencyAmount == Other.CurrencyAmount && Items == Other.Items; }
bool FRPGTradeOffer::IsValid() const
{
    if (CurrencyAmount < 0 || Items.Num() > 8) return false;
    TSet<FGuid> Seen;
    for (const auto& Item : Items)
    {
        if (!Item.ItemId.IsValid() || Item.ExpectedRevision <= 0 || Item.ExpectedRevision == MAX_int64 || Item.Quantity <= 0 || Seen.Contains(Item.ItemId)) return false;
        Seen.Add(Item.ItemId);
    }
    return true;
}
bool FRPGTradeSession::operator==(const FRPGTradeSession& Other) const
{
    return TradeId == Other.TradeId && InitiatorCharacterId == Other.InitiatorCharacterId && InviteeCharacterId == Other.InviteeCharacterId
        && CurrencyCode == Other.CurrencyCode && Revision == Other.Revision && OfferRevision == Other.OfferRevision && ExpiresAt == Other.ExpiresAt
        && Status == Other.Status && InitiatorOffer == Other.InitiatorOffer && InviteeOffer == Other.InviteeOffer
        && bInitiatorReady == Other.bInitiatorReady && bInviteeReady == Other.bInviteeReady
        && bInitiatorConfirmed == Other.bInitiatorConfirmed && bInviteeConfirmed == Other.bInviteeConfirmed;
}

void FRPGTradeClient::Reset()
{
    Generation = FGuid::NewGuid(); Sequence = 0;
    BoundEndpoint.Reset(); BoundAccount.Reset(); BoundCharacter.Invalidate();
    Session.Reset(); Pending.Reset(); LastRejection.Reset(); PendingInvitee.Invalidate(); ReadTradeId.Invalidate();
    AttemptTicket = {}; ReadTicket = {}; bHadUncertainAttempt = false;
}
bool FRPGTradeClient::Bind(FString Endpoint, const FString& AccountId, const FGuid& CharacterId)
{
    Endpoint.TrimStartAndEndInline();
    while (Endpoint.EndsWith(TEXT("/"))) Endpoint.LeftChopInline(1);
    bool bValid = (Endpoint.StartsWith(TEXT("https://")) || Endpoint.StartsWith(TEXT("http://"))) && Endpoint.Len() > 8
        && !AccountId.IsEmpty() && AccountId.Len() <= 128 && CharacterId.IsValid();
    for (TCHAR C : AccountId) if (FChar::IsControl(C)) bValid = false;
    if (!bValid) { Reset(); return false; }
    if (BoundEndpoint == Endpoint && BoundAccount == AccountId && BoundCharacter == CharacterId) return true;
    Reset(); BoundEndpoint = MoveTemp(Endpoint); BoundAccount = AccountId; BoundCharacter = CharacterId;
    return true;
}
bool FRPGTradeClient::ClearClosedSession()
{
    if (Pending.IsSet() || (Session.IsSet() && Session->IsOpen())) return false;
    Session.Reset(); ReadTicket = {}; LastRejection.Reset();
    return true;
}
bool FRPGTradeClient::MergeSession(const FRPGTradeSession& Incoming)
{
    if (Pending.IsSet() && (Incoming.TradeId != Pending->TradeId
        || (PendingInvitee.IsValid() && (Incoming.InitiatorCharacterId != BoundCharacter || Incoming.InviteeCharacterId != PendingInvitee)))) return false;
    if (Session.IsSet())
    {
        const auto& Current = Session.GetValue();
        if (Current.TradeId != Incoming.TradeId || Current.InitiatorCharacterId != Incoming.InitiatorCharacterId
            || Current.InviteeCharacterId != Incoming.InviteeCharacterId || Current.CurrencyCode != Incoming.CurrencyCode || Current.ExpiresAt != Incoming.ExpiresAt) return false;
        if (Incoming.Revision < Current.Revision) return true; // Historical receipt may resolve a pending request.
        if (Incoming.Revision == Current.Revision) return Incoming == Current;
        if (Incoming.OfferRevision < Current.OfferRevision || !Current.IsOpen()) return false;
        if (Current.Status == ERPGTradeStatus::Negotiating && Incoming.Status == ERPGTradeStatus::Invited) return false;
        if (Incoming.OfferRevision == Current.OfferRevision
            && (!(Incoming.InitiatorOffer == Current.InitiatorOffer) || !(Incoming.InviteeOffer == Current.InviteeOffer))) return false;
    }
    Session = Incoming;
    return true;
}
bool FRPGTradeClient::PrepareInvite(const FGuid& InviteeCharacterId)
{
    if (!BoundCharacter.IsValid() || Pending.IsSet() || Session.IsSet() || !InviteeCharacterId.IsValid() || InviteeCharacterId == BoundCharacter) return false;
    return StoreRequest(ERPGTradeAction::Invite, FGuid::NewGuid(), InviteeCharacterId, nullptr);
}
bool FRPGTradeClient::Prepare(ERPGTradeAction Action, const FRPGTradeOffer* Offer)
{
    if (!BoundCharacter.IsValid() || Pending.IsSet() || !Session.IsSet() || !Session->IsOpen() || Session->Revision == MAX_int64
        || !RPGTradeClient::ActionName(Action) || Action == ERPGTradeAction::Invite || (Action == ERPGTradeAction::Offer) != (Offer != nullptr)) return false;
    const auto& S = Session.GetValue();
    if (Action == ERPGTradeAction::Accept)
    { if (S.Status != ERPGTradeStatus::Invited || BoundCharacter != S.InviteeCharacterId) return false; }
    else if (Action != ERPGTradeAction::Cancel && S.Status != ERPGTradeStatus::Negotiating) return false;
    if (Offer)
    {
        if (!Offer->IsValid()) return false;
        const auto& Other = BoundCharacter == S.InitiatorCharacterId ? S.InviteeOffer : S.InitiatorOffer;
        for (const auto& A : Offer->Items) for (const auto& B : Other.Items) if (A.ItemId == B.ItemId) return false;
    }
    if ((Action == ERPGTradeAction::Ready || Action == ERPGTradeAction::Confirm) && S.OfferRevision == MAX_int64) return false;
    if (Action == ERPGTradeAction::Ready && S.InitiatorOffer.IsEmpty() && S.InviteeOffer.IsEmpty()) return false;
    if (Action == ERPGTradeAction::Confirm && !(S.bInitiatorReady && S.bInviteeReady)) return false;
    return StoreRequest(Action, S.TradeId, FGuid(), Offer);
}
bool FRPGTradeClient::StoreRequest(ERPGTradeAction Action, const FGuid& TradeId, const FGuid& Invitee, const FRPGTradeOffer* Offer)
{
    using namespace RPGTradeClient;
    FRPGTradeRequest Request;
    Request.RequestId = FGuid::NewGuid(); Request.TradeId = TradeId;
    Request.RelativePath = TEXT("/api/characters/") + Id(BoundCharacter) + TEXT("/trades/") + Id(TradeId) + TEXT("/") + ActionName(Action);
    auto Obj = MakeShared<FJsonObject>();
    Obj->SetStringField(TEXT("requestId"), Id(Request.RequestId));
    if (Action == ERPGTradeAction::Invite) Obj->SetStringField(TEXT("inviteeCharacterId"), Id(Invitee));
    else
    {
        Obj->SetStringField(TEXT("expectedRevision"), Decimal(Session->Revision));
        if (Action == ERPGTradeAction::Ready || Action == ERPGTradeAction::Confirm)
            Obj->SetStringField(TEXT("expectedOfferRevision"), Decimal(Session->OfferRevision));
        if (Offer) Obj->SetObjectField(TEXT("offer"), WriteOffer(*Offer));
    }
    auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Request.Body);
    if (!FJsonSerializer::Serialize(Obj, Writer) || FTCHARToUTF8(*Request.Body).Length() > 16384) return false;
    Pending = MoveTemp(Request); PendingInvitee = Invitee; bHadUncertainAttempt = false; LastRejection.Reset();
    return true;
}
bool FRPGTradeClient::BeginAttempt(FRPGTradeTicket& OutTicket)
{
    if (!Pending.IsSet() || Pending->Delivery == ERPGTradeDelivery::InFlight) return false;
    AttemptTicket = {Generation, ++Sequence}; OutTicket = AttemptTicket;
    Pending->Delivery = ERPGTradeDelivery::InFlight;
    return true;
}
ERPGTradeResponse FRPGTradeClient::CompleteAttempt(const FRPGTradeTicket& Ticket, int32 HttpStatus, const FString& Body)
{
    using namespace RPGTradeClient;
    if (!Pending.IsSet() || Pending->Delivery != ERPGTradeDelivery::InFlight || Ticket.Generation != Generation || !(Ticket == AttemptTicket)) return ERPGTradeResponse::Ignored;
    FObject Obj;
    if (ReadObject(Body, Obj))
    {
        const FObject* SessionObj;
        FGuid RequestId;
        FDateTime CommittedAt;
        FRPGTradeSession Incoming;
        const auto Rejection = Obj->TryGetField(TEXT("rejection"));
        const bool bSuccess = HttpStatus == 200 && Rejection && Rejection->Type == EJson::Null;
        const bool bRejected = HttpStatus == 409 && Rejection && Rejection->Type == EJson::String
            && !Rejection->AsString().IsEmpty() && Rejection->AsString().Len() <= 128;
        if ((bSuccess || bRejected) && Guid(Obj, TEXT("requestId"), RequestId) && RequestId == Pending->RequestId
            && Date(Obj, TEXT("committedAt"), CommittedAt) && Obj->TryGetObjectField(TEXT("session"), SessionObj)
            && ReadSession(*SessionObj, BoundCharacter, Incoming) && MergeSession(Incoming))
        {
            LastRejection = bRejected ? Rejection->AsString() : FString();
            Pending.Reset(); PendingInvitee.Invalidate(); AttemptTicket = {}; bHadUncertainAttempt = false;
            return bRejected ? ERPGTradeResponse::Rejected : ERPGTradeResponse::Succeeded;
        }
        FString Error;
        // Authentication, throttling and transport/proxy errors never prove non-commit.
        // Even a plain 4xx after an earlier uncertain attempt cannot discard its original request.
        if (!bHadUncertainAttempt && (HttpStatus == 400 || HttpStatus == 403 || HttpStatus == 404 || HttpStatus == 409)
            && Obj->Values.Num() == 1 && String(Obj, TEXT("error"), Error) && !Error.IsEmpty() && Error.Len() <= 128)
        {
            LastRejection = Error; Pending.Reset(); PendingInvitee.Invalidate(); AttemptTicket = {};
            return ERPGTradeResponse::Rejected;
        }
    }
    Pending->Delivery = ERPGTradeDelivery::Unknown; bHadUncertainAttempt = true;
    return ERPGTradeResponse::Unknown;
}
FRPGTradeTicket FRPGTradeClient::BeginRead(const FGuid& TradeId)
{
    if (!BoundCharacter.IsValid()) return {};
    ReadTradeId = TradeId; ReadTicket = {Generation, ++Sequence};
    return ReadTicket;
}
ERPGTradeResponse FRPGTradeClient::CompleteRead(const FRPGTradeTicket& Ticket, int32 HttpStatus, const FString& Body, int32 MaximumBytes)
{
    using namespace RPGTradeClient;
    if (!BoundCharacter.IsValid() || Ticket.Generation != Generation || Ticket.Sequence == 0 || !(Ticket == ReadTicket)) return ERPGTradeResponse::Ignored;
    ReadTicket = {};
    FObject Obj;
    if (HttpStatus != 200 || !ReadObject(Body, Obj, MaximumBytes)) return ERPGTradeResponse::Unknown;
    auto Value = Obj->TryGetField(TEXT("session"));
    if (!Value) return ERPGTradeResponse::Unknown;
    if (Value->Type == EJson::Null && !ReadTradeId.IsValid()) return ERPGTradeResponse::NoActiveTrade;
    FRPGTradeSession Incoming;
    if (Value->Type != EJson::Object || !ReadSession(Value->AsObject(), BoundCharacter, Incoming)
        || (ReadTradeId.IsValid() && ReadTradeId != Incoming.TradeId) || !MergeSession(Incoming)) return ERPGTradeResponse::Unknown;
    return ERPGTradeResponse::Applied;
}
