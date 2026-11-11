#include "Auction/RPGAuctionClient.h"
#include "../Trading/RPGTradeJson.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace RPGAuctionInternal
{
using namespace RPGTradeClient;
FString Id(FGuid V) { return V.ToString(EGuidFormats::DigitsWithHyphensLower); }
FString Action(ERPGAuctionAction A)
{
    switch(A) { case ERPGAuctionAction::Register:return TEXT("register");case ERPGAuctionAction::Purchase:return TEXT("purchase");
        case ERPGAuctionAction::Cancel:return TEXT("cancel");case ERPGAuctionAction::Claim:return TEXT("claim");default:return {}; }
}
FString Root(const FRPGTradeIntentScope& S) { return TEXT("/api/characters/")+Id(S.Character)+TEXT("/auctions/"); }
// FDateTime::ParseIso8601 rounds to milliseconds. Preserve the backend's seven fractional digits.
bool Date(const FObject& O,const TCHAR* K,FDateTime& Out)
{
    FString S;if(!String(O,K,S)||S.Len()<20||S.Len()>33||S[4]!='-'||S[7]!='-'||S[10]!='T'||S[13]!=':'||S[16]!=':')return false;
    auto Digits=[&](int32 At,int32 Count,int32& N){N=0;for(int32 I=At;I<At+Count;++I){if(S[I]<'0'||S[I]>'9')return false;N=N*10+S[I]-'0';}return true;};
    int32 Y,M,D,H,Min,Sec;if(!Digits(0,4,Y)||!Digits(5,2,M)||!Digits(8,2,D)||!Digits(11,2,H)||!Digits(14,2,Min)||!Digits(17,2,Sec)||!FDateTime::Validate(Y,M,D,H,Min,Sec,0))return false;
    int32 P=19;int64 Fraction=0;int32 Count=0;
    if(S[P]=='.'){++P;while(P<S.Len()&&S[P]>='0'&&S[P]<='9'){if(++Count>7)return false;Fraction=Fraction*10+S[P++]-'0';}if(Count==0)return false;}
    for(;Count<7;++Count)Fraction*=10;
    int32 Offset=0;
    if(S.Mid(P)!=TEXT("Z"))
    {
        if(S.Len()-P!=6||(S[P]!='+'&&S[P]!='-')||S[P+3]!=':')return false;int32 Oh,Om;
        if(!Digits(P+1,2,Oh)||!Digits(P+4,2,Om)||Oh>14||Om>59||(Oh==14&&Om!=0))return false;Offset=(Oh*60+Om)*(S[P]=='-'?-1:1);
    }
    int64 Ticks=FDateTime(Y,M,D,H,Min,Sec).GetTicks()+Fraction-int64(Offset)*ETimespan::TicksPerMinute;
    if(Ticks<0||Ticks>FDateTime::MaxValue().GetTicks())return false;Out=FDateTime(Ticks);return true;
}
FString Iso(FDateTime T) { return T.ToString(TEXT("%Y-%m-%dT%H:%M:%S"))+FString::Printf(TEXT(".%07lldZ"),T.GetTicks()%ETimespan::TicksPerSecond); }
bool Text(const FObject& O,const TCHAR* K,FString& Out,int32 Max=128)
{
    if(!String(O,K,Out)||Out.IsEmpty()||Out.Len()>Max||Out.TrimStartAndEnd().IsEmpty())return false;
    for(TCHAR C:Out)if(FChar::IsControl(C))return false;return true;
}
bool Code(const FObject& O,const TCHAR* K,FString& Out)
{
    if(!Text(O,K,Out,64))return false;
    for(TCHAR C:Out)if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='.'||C=='_'||C=='-'))return false;return true;
}
bool Number(const FObject& O,const TCHAR* K,int32& Out,int32 Min=0)
{
    double N;if(!O||!O->HasTypedField<EJson::Number>(K)||!O->TryGetNumberField(K,N)||!FMath::IsFinite(N)||N<Min||N>MAX_int32||FMath::FloorToDouble(N)!=N)return false;
    Out=static_cast<int32>(N);return true;
}
bool Null(const FObject& O,const TCHAR* K) { auto V=O->TryGetField(K);return V&&V->Type==EJson::Null; }
bool Listing(const FObject& O,bool bOwn,FRPGAuctionListing& L)
{
    FString Status;const FObject* I;const FObject* D;
    if(!Guid(O,TEXT("listingId"),L.ListingId)||!Integer(O,TEXT("revision"),L.Revision,true)||!Integer(O,TEXT("totalPrice"),L.TotalPrice,true)
        ||!Code(O,TEXT("currencyCode"),L.CurrencyCode)||!Date(O,TEXT("expiresAt"),L.ExpiresAt)||!String(O,TEXT("status"),Status)
        ||!O->TryGetObjectField(TEXT("item"),I))return false;
    if(Status==TEXT("Active"))L.Status=ERPGAuctionStatus::Active;else if(Status==TEXT("Sold"))L.Status=ERPGAuctionStatus::Sold;
    else if(Status==TEXT("Cancelled"))L.Status=ERPGAuctionStatus::Cancelled;else if(Status==TEXT("Expired"))L.Status=ERPGAuctionStatus::Expired;else return false;
    L.bOwn=bOwn;
    if(bOwn)
    {
        if(!Guid(O,TEXT("sellerCharacterId"),L.SellerCharacter))return false;
        if(!Null(O,TEXT("sellerProceeds"))) { int64 P;if(!Integer(O,TEXT("sellerProceeds"),P,false)||P>L.TotalPrice||L.Status!=ERPGAuctionStatus::Sold)return false;L.SellerProceeds=P; }
        else if(L.Status==ERPGAuctionStatus::Sold)return false;
    }
    else if(!Null(O,TEXT("sellerCharacterId"))||!Null(O,TEXT("sellerProceeds")))return false;
    auto& Item=L.Item;
    if(!Text(*I,TEXT("definitionType"),Item.DefinitionType)||!Text(*I,TEXT("definitionName"),Item.DefinitionName)||!Number(*I,TEXT("definitionVersion"),Item.DefinitionVersion,1)
        ||!Number(*I,TEXT("quantity"),L.Quantity,1)||!String(*I,TEXT("bindState"),Item.BindState)
        ||(Item.BindState!=TEXT("Unbound")&&Item.BindState!=TEXT("BindOnEquip"))||!(*I)->TryGetObjectField(TEXT("durability"),D)
        ||!Number(*D,TEXT("current"),Item.Durability)||!Number(*D,TEXT("maximum"),Item.MaximumDurability)||Item.Durability>Item.MaximumDurability)return false;
    auto Expiry=(*I)->TryGetField(TEXT("expiresAtUtc"));if(!Expiry)return false;
    if(Expiry->Type!=EJson::Null){FDateTime T;if(!Date(*I,TEXT("expiresAtUtc"),T)||T<L.ExpiresAt)return false;Item.ExpiresAt=T;}
    const TArray<TSharedPtr<FJsonValue>>* Tags;const TArray<TSharedPtr<FJsonValue>>* Stats;
    if(!(*I)->TryGetArrayField(TEXT("instanceTags"),Tags)||Tags->Num()>64||!(*I)->TryGetArrayField(TEXT("statValues"),Stats)||Stats->Num()>128)return false;
    TSet<FString> Seen;
    for(const auto& V:*Tags){if(!V||V->Type!=EJson::String)return false;auto T=V->AsString();if(T.IsEmpty()||T.TrimStartAndEnd().IsEmpty()||T.Len()>128||Seen.Contains(T))return false;for(TCHAR C:T)if(FChar::IsControl(C))return false;Seen.Add(T);Item.Tags.Add(T);}
    Seen.Reset();
    for(const auto& V:*Stats){if(!V||V->Type!=EJson::Object)return false;FRPGTradePreviewStat S;auto Obj=V->AsObject();
        if(!Text(Obj,TEXT("statTag"),S.Tag)||Seen.Contains(S.Tag)||!Obj->HasTypedField<EJson::Number>(TEXT("value"))||!Obj->TryGetNumberField(TEXT("value"),S.Value)||!FMath::IsFinite(S.Value))return false;
        Seen.Add(S.Tag);Item.Stats.Add(MoveTemp(S));}
    auto Growth=(*I)->TryGetField(TEXT("equipmentProgression"));
    if(Growth&&Growth->Type!=EJson::Null){if(Growth->Type!=EJson::Object)return false;FRPGTradePreviewGrowth G;
        if(L.Quantity!=1||!Text(Growth->AsObject(),TEXT("growthBandId"),G.Band)||!Number(Growth->AsObject(),TEXT("ruleVersion"),G.RuleVersion,1)||!Number(Growth->AsObject(),TEXT("level"),G.Level))return false;Item.Growth=G;}
    return true;
}
struct FCommand { int64 Revision=0,Price=0,Amount=0,ItemRevision=0;int32 Quantity=0;FString Currency;FGuid Item;FDateTime Expires; };
bool Command(const FRPGAuctionIntent& Intent,FCommand& C)
{
    const auto& R=Intent.Request;FObject O;FGuid Nonce;
    if(!Intent.Scope.IsValid()||!R.ListingId.IsValid()||!R.RequestId.IsValid()||Action(R.Action).IsEmpty()
        ||R.RelativePath!=Root(Intent.Scope)+Id(R.ListingId)+TEXT("/")+Action(R.Action)
        ||(R.Delivery!=ERPGTradeDelivery::Prepared&&R.Delivery!=ERPGTradeDelivery::InFlight&&R.Delivery!=ERPGTradeDelivery::Unknown)
        ||!ReadObject(R.Body,O,8192)||!Guid(O,TEXT("requestId"),Nonce)||Nonce!=R.RequestId)return false;
    TSet<FString> Allowed{TEXT("requestId")};
    if(R.Action==ERPGAuctionAction::Register)
    {
        Allowed.Append({TEXT("itemId"),TEXT("expectedItemRevision"),TEXT("quantity"),TEXT("totalPrice"),TEXT("expiresAt")});
        if(!Guid(O,TEXT("itemId"),C.Item)||!Integer(O,TEXT("expectedItemRevision"),C.ItemRevision,true)||C.ItemRevision==MAX_int64
            ||!Number(O,TEXT("quantity"),C.Quantity,1)||!Integer(O,TEXT("totalPrice"),C.Price,true)||!Date(O,TEXT("expiresAt"),C.Expires))return false;
        FObject Lexical;int64 Quantity;FString Token;
        if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(R.Body),Lexical,FJsonSerializer::EFlags::StoreNumbersAsStrings))return false;
        auto Raw=Lexical->TryGetField(TEXT("quantity"));if(!Raw||!Raw->TryGetString(Token))return false;
        auto NumberToken=MakeShared<FJsonObject>();NumberToken->SetStringField(TEXT("quantity"),Token);
        if(!Integer(NumberToken,TEXT("quantity"),Quantity,true)||Quantity>MAX_int32)return false;
    }
    else
    {
        Allowed.Add(TEXT("expectedRevision"));if(!Integer(O,TEXT("expectedRevision"),C.Revision,true)||C.Revision==MAX_int64)return false;
        if(R.Action!=ERPGAuctionAction::Cancel)
        {
            Allowed.Add(TEXT("expectedCurrencyCode"));if(!Code(O,TEXT("expectedCurrencyCode"),C.Currency))return false;
            const TCHAR* Key=R.Action==ERPGAuctionAction::Purchase?TEXT("totalPrice"):TEXT("requestedAmount");Allowed.Add(Key);
            if(!Integer(O,Key,R.Action==ERPGAuctionAction::Purchase?C.Price:C.Amount,true))return false;
        }
    }
    // Every allowed field above is required, so matching cardinality also rejects unknown fields.
    return O->Values.Num()==Allowed.Num();
}
bool Receipt(const FRPGAuctionIntent& Intent,const FString& Body,FRPGAuctionReceipt& R)
{
    FCommand C;FObject O;FString Kind;
    if(!Command(Intent,C)||!ReadObject(Body,O,4*1024*1024)||!Guid(O,TEXT("requestId"),R.RequestId)||R.RequestId!=Intent.Request.RequestId
        ||!Date(O,TEXT("committedAt"),R.CommittedAt)||!String(O,TEXT("kind"),Kind))return false;
    R.Action=Intent.Request.Action;R.ListingId=Intent.Request.ListingId;
    if(R.Action==ERPGAuctionAction::Claim)
    {
        FGuid ListingId;FString Currency;
        if(Kind!=TEXT("claim")||!Guid(O,TEXT("listingId"),ListingId)||ListingId!=R.ListingId||!Code(O,TEXT("currencyCode"),Currency)||Currency!=C.Currency
            ||!Integer(O,TEXT("revision"),R.Revision,true)||!Integer(O,TEXT("paid"),R.Paid,false)||R.Paid>C.Amount
            ||!Integer(O,TEXT("remaining"),R.Remaining,false)||R.Remaining>MAX_int64-R.Paid||!Integer(O,TEXT("balance"),R.Balance,false)
            ||R.Revision!=C.Revision+(R.Paid>0?1:0)||R.Balance<R.Paid)return false;return true;
    }
    const FObject* L;FRPGAuctionListing Listing;const TArray<TSharedPtr<FJsonValue>>* Items;
    bool bPurchase=R.Action==ERPGAuctionAction::Purchase;
    if(Kind!=(bPurchase?TEXT("purchase"):TEXT("escrow"))||!O->TryGetObjectField(TEXT("listing"),L)||!RPGAuctionInternal::Listing(*L,!bPurchase,Listing)
        ||Listing.ListingId!=R.ListingId||Listing.Revision!=(R.Action==ERPGAuctionAction::Register?1:C.Revision+1)
        ||!O->TryGetArrayField(TEXT("items"),Items)||Items->Num()>241||Items->ContainsByPredicate([](const auto& V){return !V||V->Type!=EJson::Object;}))return false;
    if(bPurchase && Items->IsEmpty())return false;
    if(bPurchase)
    { if(Listing.Status!=ERPGAuctionStatus::Sold||Listing.CurrencyCode!=C.Currency||Listing.TotalPrice!=C.Price||!Integer(O,TEXT("balance"),R.Balance,false)||R.CommittedAt>=Listing.ExpiresAt)return false; }
    else if(Listing.SellerCharacter!=Intent.Scope.Character)return false;
    else if(R.Action==ERPGAuctionAction::Register)
    { if(Listing.Status!=ERPGAuctionStatus::Active||Listing.Quantity!=C.Quantity||Listing.TotalPrice!=C.Price||Listing.ExpiresAt!=C.Expires||R.CommittedAt>=Listing.ExpiresAt)return false; }
    else if((Listing.Status!=ERPGAuctionStatus::Cancelled&&Listing.Status!=ERPGAuctionStatus::Expired)
        ||(Listing.Status==ERPGAuctionStatus::Expired)!=(R.CommittedAt>=Listing.ExpiresAt))return false;
    R.Revision=Listing.Revision;R.Listing=MoveTemp(Listing);return true;
}
bool Definitive(const FString& Code)
{
    static const TSet<FString> Codes{TEXT("invalid_auction_command"),TEXT("invalid_auction_purchase"),TEXT("invalid_auction_claim"),TEXT("invalid_auction_price"),TEXT("invalid_auction_duration"),
        TEXT("auctions_disabled"),TEXT("auction_player_required"),TEXT("character_not_owned"),TEXT("account_restricted"),TEXT("auction_not_found"),TEXT("auction_payable_not_found"),
        TEXT("auction_seller_required"),TEXT("auction_payable_owner_required"),TEXT("auction_revision_conflict"),TEXT("auction_payable_revision_conflict"),TEXT("auction_price_conflict"),
        TEXT("auction_currency_conflict"),TEXT("auction_currency_unavailable"),TEXT("auction_self_purchase"),TEXT("auction_requires_lobby"),TEXT("auction_insufficient_balance"),
        TEXT("auction_not_active"),TEXT("auction_expired"),TEXT("auction_item_reserved"),TEXT("auction_listing_limit"),TEXT("auction_escrow_changed"),TEXT("auction_identity_conflict"),
        TEXT("auction_delivery_capacity"),TEXT("auction_return_capacity"),TEXT("auction_wallet_limit"),TEXT("auction_wallet_revision_exhausted"),TEXT("item_trade_not_allowed"),
        TEXT("item_unavailable"),TEXT("item_bound"),TEXT("invalid_trade_quantity"),TEXT("item_revision_conflict"),TEXT("item_not_in_owned_inventory")};
    return Codes.Contains(Code);
}
}
bool FRPGAuctionIntent::IsValid() const { RPGAuctionInternal::FCommand C;return RPGAuctionInternal::Command(*this,C); }
bool RPGAuction::DecodeListing(const FString& Body,bool bOwn,FRPGAuctionListing& Out)
{ RPGTradeClient::FObject O;FRPGAuctionListing L;if(!RPGTradeClient::ReadObject(Body,O,65536)||!RPGAuctionInternal::Listing(O,bOwn,L))return false;Out=MoveTemp(L);return true; }
bool RPGAuction::DecodePage(const FString& Body,bool bOwn,FRPGAuctionPage& Out)
{
    using namespace RPGTradeClient;FObject O;const TArray<TSharedPtr<FJsonValue>>* A;FRPGAuctionPage Page;
    if(!ReadObject(Body,O,2*1024*1024)||!O->TryGetArrayField(TEXT("listings"),A)||A->Num()>50)return false;
    auto Cursor=O->TryGetField(TEXT("nextCursor"));if(!Cursor)return false;
    if(Cursor->Type!=EJson::Null){if(Cursor->Type!=EJson::String)return false;Page.NextCursor=Cursor->AsString();if(Page.NextCursor.IsEmpty()||Page.NextCursor.Len()>512)return false;
        for(TCHAR C:Page.NextCursor)if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='-'||C=='_'))return false;}
    TSet<FGuid> Seen;
    for(const auto& V:*A){FRPGAuctionListing L;if(!V||V->Type!=EJson::Object||!RPGAuctionInternal::Listing(V->AsObject(),bOwn,L)||Seen.Contains(L.ListingId)||(!bOwn&&L.Status!=ERPGAuctionStatus::Active))return false;Seen.Add(L.ListingId);Page.Listings.Add(MoveTemp(L));}
    if(Page.Listings.IsEmpty()&&!Page.NextCursor.IsEmpty())return false;Out=MoveTemp(Page);return true;
}
bool RPGAuction::DecodePayable(const FString& Body,FRPGAuctionPayable& Out)
{
    using namespace RPGTradeClient;FObject O;FRPGAuctionPayable P;
    if(!ReadObject(Body,O,8192)||!Guid(O,TEXT("listingId"),P.ListingId)||!RPGAuctionInternal::Code(O,TEXT("currencyCode"),P.CurrencyCode)||!Integer(O,TEXT("revision"),P.Revision,true)
        ||!Integer(O,TEXT("initialAmount"),P.Initial,false)||!Integer(O,TEXT("remainingAmount"),P.Remaining,false)||P.Remaining>P.Initial)return false;Out=MoveTemp(P);return true;
}
void FRPGAuctionClient::Reset(){Scope={};Generation=FGuid::NewGuid();Sequence=0;Attempt={};Recovery={};Pending.Reset();Receipt.Reset();LastError.Reset();bUncertain=false;bNeedsAssetRefresh=false;}
bool FRPGAuctionClient::Bind(FString Endpoint,const FString& Account,FGuid Character)
{ FRPGTradeIntentScope S;if(!FRPGTradeIntentScope::Create(MoveTemp(Endpoint),Account,Character,S)){Reset();return false;}if(!(Scope==S)){Reset();Scope=MoveTemp(S);}return true; }
bool FRPGAuctionClient::Prepare(ERPGAuctionAction A,FGuid Listing,const TSharedPtr<FJsonObject>& O)
{
    using namespace RPGAuctionInternal;if(Pending.IsSet()||!Scope.IsValid())return false;
    FRPGAuctionRequest R;R.Action=A;R.RequestId=FGuid::NewGuid();R.ListingId=Listing;R.RelativePath=Root(Scope)+Id(Listing)+TEXT("/")+Action(A);O->SetStringField(TEXT("requestId"),Id(R.RequestId));
    auto Writer=TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&R.Body);if(!FJsonSerializer::Serialize(O.ToSharedRef(),Writer)||!FRPGAuctionIntent{Scope,R}.IsValid())return false;
    Pending=MoveTemp(R);Attempt={};Recovery={};LastError.Reset();bUncertain=false;return true;
}
bool FRPGAuctionClient::PrepareRegister(FGuid L,FGuid Item,int64 Rev,int32 Q,int64 Price,FDateTime Expiry)
{ auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("itemId"),RPGAuctionInternal::Id(Item));O->SetStringField(TEXT("expectedItemRevision"),LexToString(Rev));O->SetNumberField(TEXT("quantity"),Q);O->SetStringField(TEXT("totalPrice"),LexToString(Price));O->SetStringField(TEXT("expiresAt"),RPGAuctionInternal::Iso(Expiry));return Prepare(ERPGAuctionAction::Register,L,O); }
bool FRPGAuctionClient::PreparePurchase(const FRPGAuctionListing& L)
{ if(L.Status!=ERPGAuctionStatus::Active||L.bOwn)return false;auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("expectedRevision"),LexToString(L.Revision));O->SetStringField(TEXT("expectedCurrencyCode"),L.CurrencyCode);O->SetStringField(TEXT("totalPrice"),LexToString(L.TotalPrice));return Prepare(ERPGAuctionAction::Purchase,L.ListingId,O); }
bool FRPGAuctionClient::PrepareCancel(const FRPGAuctionListing& L)
{ if(!L.bOwn||L.SellerCharacter!=Scope.Character||L.Status!=ERPGAuctionStatus::Active)return false;auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("expectedRevision"),LexToString(L.Revision));return Prepare(ERPGAuctionAction::Cancel,L.ListingId,O); }
bool FRPGAuctionClient::PrepareClaim(const FRPGAuctionPayable& P,int64 Amount)
{ if(P.Initial<0||P.Remaining<0||P.Remaining>P.Initial)return false;auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("expectedRevision"),LexToString(P.Revision));O->SetStringField(TEXT("expectedCurrencyCode"),P.CurrencyCode);O->SetStringField(TEXT("requestedAmount"),LexToString(Amount));return Prepare(ERPGAuctionAction::Claim,P.ListingId,O); }
bool FRPGAuctionClient::ExportPending(FRPGAuctionIntent& Out) const
{ Out={};if(!Pending.IsSet())return false;FRPGAuctionIntent I{Scope,*Pending};if(!I.IsValid())return false;Out=MoveTemp(I);return true; }
bool FRPGAuctionClient::RestorePending(const FRPGAuctionIntent& I)
{ if(Pending.IsSet()||Receipt.IsSet()||!(Scope==I.Scope)||!I.IsValid())return false;Generation=FGuid::NewGuid();Sequence=0;Attempt={};Recovery={};Pending=I.Request;Pending->Delivery=ERPGTradeDelivery::Unknown;bUncertain=true;LastError.Reset();return true; }
bool FRPGAuctionClient::BeginAttempt(FRPGTradeTicket& Out)
{ Out={};if(!Pending.IsSet()||Pending->Delivery==ERPGTradeDelivery::InFlight||Recovery.Sequence!=0||Sequence==MAX_uint64)return false;Attempt={Generation,++Sequence};Pending->Delivery=ERPGTradeDelivery::InFlight;Out=Attempt;return true; }
bool FRPGAuctionClient::BeginRecovery(FRPGTradeTicket& Out,FString& Path)
{ Out={};Path.Reset();if(!Pending.IsSet()||Pending->Delivery==ERPGTradeDelivery::InFlight||Recovery.Sequence!=0||Sequence==MAX_uint64)return false;Recovery={Generation,++Sequence};Out=Recovery;Path=RPGAuctionInternal::Root(Scope)+TEXT("requests/")+RPGAuctionInternal::Id(Pending->RequestId);return true; }
ERPGAuctionResponse FRPGAuctionClient::CompleteAttempt(const FRPGTradeTicket& T,int32 Status,const FString& Body)
{ if(!Pending.IsSet()||Pending->Delivery!=ERPGTradeDelivery::InFlight||T.Generation!=Generation||T.Sequence==0||!(T==Attempt))return ERPGAuctionResponse::Ignored;Attempt={};return Complete(Status,Body,false); }
ERPGAuctionResponse FRPGAuctionClient::CompleteRecovery(const FRPGTradeTicket& T,int32 Status,const FString& Body)
{ if(!Pending.IsSet()||T.Generation!=Generation||T.Sequence==0||!(T==Recovery))return ERPGAuctionResponse::Ignored;Recovery={};return Complete(Status,Body,true); }
ERPGAuctionResponse FRPGAuctionClient::Complete(int32 Status,const FString& Body,bool bRecovery)
{
    FRPGAuctionReceipt R;
    if(Status==200&&RPGAuctionInternal::Receipt({Scope,*Pending},Body,R)){Receipt=MoveTemp(R);bNeedsAssetRefresh=true;Pending.Reset();Attempt={};Recovery={};LastError.Reset();bUncertain=false;return ERPGAuctionResponse::Succeeded;}
    RPGTradeClient::FObject O;FString Error;
    if(!bRecovery&&!bUncertain&&(Status==400||Status==403||Status==404||Status==409)&&RPGTradeClient::ReadObject(Body,O,8192)&&O->Values.Num()==1
        &&RPGTradeClient::String(O,TEXT("error"),Error)&&RPGAuctionInternal::Definitive(Error)){LastError=Error;Pending.Reset();return ERPGAuctionResponse::Rejected;}
    Pending->Delivery=ERPGTradeDelivery::Unknown;bUncertain=true;return ERPGAuctionResponse::Unknown;
}
bool FRPGAuctionClient::AcknowledgeAssets(FGuid Id)
{ if(!Receipt.IsSet()||Receipt->RequestId!=Id)return false;bNeedsAssetRefresh=false;return true; }
