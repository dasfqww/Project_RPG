#include "Shop/RPGShopTypes.h"
#include "Trading/RPGTradeJson.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"

namespace
{
using namespace RPGTradeClient;
bool Identity(const FString& S,int32 Max=128)
{ if(S.IsEmpty() || S.Len()>Max)return false;for(TCHAR C:S)if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='.'||C=='_'||C=='-'))return false;return true; }
bool Text(const FObject& O,const TCHAR* K,FText& Out)
{FString S;if(!String(O,K,S)||S.IsEmpty()||S.Len()>512)return false;for(TCHAR C:S)if(FChar::IsControl(C))return false;Out=FText::FromString(S);return true;}
bool Number(const FObject& O,const TCHAR* K,int32& Out,int32 Minimum=1,int32 Maximum=MAX_int32)
{double V=0;if(!O||!O->HasTypedField<EJson::Number>(K)||!O->TryGetNumberField(K,V)||!FMath::IsFinite(V)||V<Minimum||V>Maximum||FMath::FloorToDouble(V)!=V)return false;Out=static_cast<int32>(V);return true;}
FString Id(FGuid G){return G.ToString(EGuidFormats::DigitsWithHyphensLower);}
bool OptionalItem(const FObject& O,FGuid& Item)
{const auto V=O->TryGetField(TEXT("itemId"));if(!V||V->IsNull()){Item.Invalidate();return true;}return Guid(O,TEXT("itemId"),Item);}
}

bool RPGShopCodec::Snapshot(const FString& Json,FRPGShopSnapshot& Out)
{
    using namespace RPGTradeClient;
    FObject O;FRPGShopSnapshot S;const TArray<TSharedPtr<FJsonValue>>* Offers=nullptr;const TArray<TSharedPtr<FJsonValue>>* Sales=nullptr;const TArray<TSharedPtr<FJsonValue>>* Slots=nullptr;
    if(!ReadObject(Json,O,256*1024)||!String(O,TEXT("shopId"),S.ShopId)||!Identity(S.ShopId)
        ||!String(O,TEXT("policyTag"),S.PolicyTag)||!Identity(S.PolicyTag)
        ||!String(O,TEXT("currencyCode"),S.CurrencyCode)||!Identity(S.CurrencyCode,64)||!Text(O,TEXT("displayName"),S.DisplayName)
        ||!Integer(O,TEXT("balance"),S.Balance,false)||!Integer(O,TEXT("availableBalance"),S.AvailableBalance,false)||S.AvailableBalance>S.Balance
        ||!O->TryGetArrayField(TEXT("inventoryFreeSlots"),Slots)||Slots->Num()!=3
        ||!O->TryGetArrayField(TEXT("offers"),Offers)||Offers->Num()>128||!O->TryGetArrayField(TEXT("saleItems"),Sales)||Sales->Num()>500)return false;
    for(const auto& V:*Slots){if(!V||V->Type!=EJson::Number)return false;double N=V->AsNumber();if(N<0||N>80||!FMath::IsFinite(N)||FMath::FloorToDouble(N)!=N)return false;S.InventoryFreeSlots.Add(static_cast<int32>(N));}
    TSet<FString> OfferIds;TSet<FGuid> ItemIds;
    for(const auto& V:*Offers)
    {
        if(!V||V->Type!=EJson::Object)return false;const FObject R=V->AsObject();FRPGShopOffer Row;
        if(!String(R,TEXT("offerId"),Row.OfferId)||!Identity(Row.OfferId)||OfferIds.Contains(Row.OfferId)||!Text(R,TEXT("displayName"),Row.DisplayName)
            ||!String(R,TEXT("definitionType"),Row.DefinitionType)||!Identity(Row.DefinitionType)||!String(R,TEXT("definitionName"),Row.DefinitionName)||!Identity(Row.DefinitionName)
            ||!Number(R,TEXT("definitionVersion"),Row.DefinitionVersion)||!Integer(R,TEXT("unitPrice"),Row.UnitPrice,true)
            ||!Number(R,TEXT("maximumQuantity"),Row.MaximumQuantity)||!Number(R,TEXT("maximumStackSize"),Row.MaximumStackSize)
            ||!Number(R,TEXT("maximumPurchasable"),Row.MaximumPurchasable,0,Row.MaximumQuantity)
            ||Row.UnitPrice>MAX_int64/Row.MaximumQuantity)return false;
        OfferIds.Add(Row.OfferId);S.Offers.Add(MoveTemp(Row));
    }
    for(const auto& V:*Sales)
    {
        if(!V||V->Type!=EJson::Object)return false;const FObject R=V->AsObject();FRPGShopSaleItem Row;FString Reason;
        if(!Guid(R,TEXT("itemId"),Row.ItemId)||ItemIds.Contains(Row.ItemId)||!Integer(R,TEXT("revision"),Row.Revision,true)
            ||!String(R,TEXT("ruleId"),Row.RuleId)||(!Row.RuleId.IsEmpty()&&!Identity(Row.RuleId))||!Text(R,TEXT("displayName"),Row.DisplayName)
            ||!Number(R,TEXT("quantity"),Row.Quantity)||!Number(R,TEXT("maximumQuantity"),Row.MaximumQuantity,0)
            ||!Integer(R,TEXT("unitPrice"),Row.UnitPrice,false)||!Boolean(R,TEXT("canSell"),Row.bCanSell)
            ||!String(R,TEXT("reason"),Reason)||Reason.Len()>256)return false;
        if(Row.bCanSell && (Row.RuleId.IsEmpty()||Row.UnitPrice<=0||Row.MaximumQuantity<=0||Row.MaximumQuantity>Row.Quantity||Row.UnitPrice>MAX_int64/Row.MaximumQuantity))return false;
        Row.Reason=Reason.IsEmpty()?FText::GetEmpty():Error(Reason);ItemIds.Add(Row.ItemId);S.SaleItems.Add(MoveTemp(Row));
    }
    Out=MoveTemp(S);return true;
}
FString RPGShopCodec::Body(const FRPGShopIntent& I)
{
    auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("requestId"),Id(I.RequestId));O->SetStringField(TEXT("shopId"),I.ShopId);
    O->SetStringField(TEXT("policyTag"),I.PolicyTag);O->SetStringField(TEXT("action"),I.Action);O->SetStringField(TEXT("offerId"),I.OfferId);
    O->SetNumberField(TEXT("quantity"),I.Quantity);O->SetStringField(TEXT("expectedUnitPrice"),LexToString(I.UnitPrice));
    if(I.Action==TEXT("sell")){O->SetStringField(TEXT("itemId"),Id(I.ItemId));O->SetStringField(TEXT("expectedItemRevision"),LexToString(I.ItemRevision));}
    FString Result;FJsonSerializer::Serialize(O,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Result));return Result;
}
bool RPGShopCodec::Intent(const FRPGTradeIntentScope& Scope,const FString& Currency,const FString& Json,FRPGShopIntent& Out)
{
    using namespace RPGTradeClient;FObject O;FRPGShopIntent I;I.Scope=Scope;I.CurrencyCode=Currency;I.Body=Json;
    if(!Scope.IsValid()||!Identity(Currency,64)||!ReadObject(Json,O,8192)||!Guid(O,TEXT("requestId"),I.RequestId)
        ||!String(O,TEXT("shopId"),I.ShopId)||!Identity(I.ShopId)||!String(O,TEXT("policyTag"),I.PolicyTag)||!Identity(I.PolicyTag)
        ||!String(O,TEXT("action"),I.Action)||(I.Action!=TEXT("buy")&&I.Action!=TEXT("sell"))||!String(O,TEXT("offerId"),I.OfferId)||!Identity(I.OfferId)
        ||!Number(O,TEXT("quantity"),I.Quantity)||!Integer(O,TEXT("expectedUnitPrice"),I.UnitPrice,true)||I.UnitPrice>MAX_int64/I.Quantity)return false;
    if(I.Action==TEXT("sell")){if(O->Values.Num()!=9||!Guid(O,TEXT("itemId"),I.ItemId)||!Integer(O,TEXT("expectedItemRevision"),I.ItemRevision,true))return false;}
    else if(O->Values.Num()!=7)return false;
    if(Body(I)!=Json)return false;Out=MoveTemp(I);return true;
}
bool FRPGShopIntent::IsValid() const
{
    FRPGShopIntent I;return RPGShopCodec::Intent(Scope,CurrencyCode,Body,I)&&I.Same(*this);
}
bool FRPGShopIntent::Same(const FRPGShopIntent& I) const
{return Scope==I.Scope&&RequestId==I.RequestId&&ItemId==I.ItemId&&ShopId==I.ShopId&&PolicyTag==I.PolicyTag&&Action==I.Action&&OfferId==I.OfferId&&CurrencyCode==I.CurrencyCode&&Body==I.Body&&Quantity==I.Quantity&&UnitPrice==I.UnitPrice&&ItemRevision==I.ItemRevision;}
bool RPGShopCodec::Receipt(const FString& Json,const FRPGShopIntent& I,FRPGShopReceipt& Out)
{
    using namespace RPGTradeClient;FObject O;FRPGShopReceipt R;FGuid Character,Item;FString Shop,Policy,Action,Offer,Currency;int32 Quantity=0;int64 Unit=0,Total=0;
    if(!I.IsValid()||!ReadObject(Json,O,32768)||!Guid(O,TEXT("requestId"),R.RequestId)||R.RequestId!=I.RequestId
        ||!Guid(O,TEXT("characterId"),Character)||Character!=I.Scope.Character||!String(O,TEXT("shopId"),Shop)||Shop!=I.ShopId
        ||!String(O,TEXT("policyTag"),Policy)||Policy!=I.PolicyTag||!String(O,TEXT("action"),Action)||Action!=I.Action
        ||!String(O,TEXT("offerId"),Offer)||Offer!=I.OfferId||!OptionalItem(O,Item)||Item!=I.ItemId
        ||!Number(O,TEXT("quantity"),Quantity)||Quantity!=I.Quantity||!Integer(O,TEXT("unitPrice"),Unit,true)||Unit!=I.UnitPrice
        ||!Integer(O,TEXT("totalPrice"),Total,true)||Total!=I.UnitPrice*I.Quantity||!String(O,TEXT("currencyCode"),Currency)||Currency!=I.CurrencyCode
        ||!Integer(O,TEXT("balanceBefore"),R.BalanceBefore,false)||!Integer(O,TEXT("balanceAfter"),R.BalanceAfter,false)
        ||!Date(O,TEXT("committedAtUtc"),R.CommittedAt))return false;
    if(I.Action==TEXT("buy")?(R.BalanceBefore<Total||R.BalanceAfter!=R.BalanceBefore-Total):(R.BalanceBefore>MAX_int64-Total||R.BalanceAfter!=R.BalanceBefore+Total))return false;
    Out=R;return true;
}
bool RPGShopCodec::Rejection(const FString& Json,const FRPGShopIntent& I,FString& ErrorCode)
{
    using namespace RPGTradeClient;FObject O;FGuid Request,Character,Item;FString Shop,Policy,Action,Offer,Code;bool Rejected=false;int32 Quantity=0;int64 Unit=0,Revision=0;FDateTime At;
    if(!I.IsValid()||!ReadObject(Json,O,32768)||O->Values.Num()!=13||!Boolean(O,TEXT("rejected"),Rejected)||!Rejected
        ||!String(O,TEXT("error"),Code)||!Identity(Code)||!Guid(O,TEXT("requestId"),Request)||Request!=I.RequestId
        ||!Guid(O,TEXT("characterId"),Character)||Character!=I.Scope.Character||!String(O,TEXT("shopId"),Shop)||Shop!=I.ShopId
        ||!String(O,TEXT("policyTag"),Policy)||Policy!=I.PolicyTag||!String(O,TEXT("action"),Action)||Action!=I.Action
        ||!String(O,TEXT("offerId"),Offer)||Offer!=I.OfferId||!O->TryGetField(TEXT("itemId"))||!OptionalItem(O,Item)||Item!=I.ItemId
        ||!Number(O,TEXT("quantity"),Quantity)||Quantity!=I.Quantity||!Integer(O,TEXT("expectedUnitPrice"),Unit,true)||Unit!=I.UnitPrice
        ||!Date(O,TEXT("rejectedAtUtc"),At))return false;
    const auto R=O->TryGetField(TEXT("expectedItemRevision"));if(!R)return false;
    if(I.Action==TEXT("sell")){if(!Integer(O,TEXT("expectedItemRevision"),Revision,true)||Revision!=I.ItemRevision)return false;}
    else if(R->Type!=EJson::Null)return false;
    ErrorCode=MoveTemp(Code);return true;
}
FText RPGShopCodec::Error(const FString& Code)
{
    if(Code==TEXT("shop_insufficient_balance"))return NSLOCTEXT("RPGShop","NoMoney","사용 가능한 재화가 부족합니다.");
    if(Code==TEXT("shop_inventory_full")||Code==TEXT("shop_capacity"))return NSLOCTEXT("RPGShop","NoSpace","가방 공간이 부족합니다. 수량을 줄이거나 가방을 정리하세요.");
    if(Code==TEXT("shop_price_changed")||Code==TEXT("shop_policy_changed")||Code==TEXT("shop_policy_conflict"))return NSLOCTEXT("RPGShop","PriceChanged","상품 조건이 바뀌었습니다. 목록을 새로 확인하세요.");
    if(Code==TEXT("shop_item_reserved"))return NSLOCTEXT("RPGShop","Reserved","다른 거래에서 사용 중인 물품입니다.");
    if(Code==TEXT("shop_item_locked"))return NSLOCTEXT("RPGShop","Locked","잠긴 물품은 판매할 수 없습니다.");
    if(Code==TEXT("shop_item_expired"))return NSLOCTEXT("RPGShop","Expired","사용 기간이 지난 물품입니다.");
    if(Code==TEXT("shop_wallet_limit"))return NSLOCTEXT("RPGShop","WalletLimit","보유 재화 한도를 초과합니다.");
    if(Code==TEXT("shop_requires_lobby"))return NSLOCTEXT("RPGShop","LobbyOnly","로비에서 이용할 수 있습니다.");
    if(Code==TEXT("shop_disabled"))return NSLOCTEXT("RPGShop","Unavailable","현재 상점을 이용할 수 없습니다.");
    return NSLOCTEXT("RPGShop","UnavailableItem","현재 조건에서는 거래할 수 없는 물품입니다.");
}
bool RPGShopCodec::Definitive(const FString& Json,FString& Code)
{
    RPGTradeClient::FObject O;if(!RPGTradeClient::ReadObject(Json,O,4096)||O->Values.Num()!=1||!RPGTradeClient::String(O,TEXT("error"),Code))return false;
    static const TSet<FString> Known={TEXT("shop_insufficient_balance"),TEXT("shop_inventory_full"),TEXT("shop_capacity"),TEXT("shop_price_changed"),TEXT("shop_policy_changed"),TEXT("shop_policy_conflict"),TEXT("shop_item_reserved"),TEXT("shop_item_locked"),TEXT("shop_item_expired"),TEXT("shop_wallet_limit"),TEXT("shop_requires_lobby"),TEXT("shop_disabled"),TEXT("shop_item_changed"),TEXT("shop_item_revision_conflict"),TEXT("shop_item_not_sellable"),TEXT("shop_offer_not_found"),TEXT("shop_quantity_invalid"),TEXT("shop_unavailable"),TEXT("shop_quantity_limit"),TEXT("shop_item_identity_conflict"),TEXT("shop_item_revision_exhausted"),TEXT("shop_item_unavailable"),TEXT("shop_sale_not_allowed"),TEXT("shop_offer_unavailable")};
    return Known.Contains(Code);
}
