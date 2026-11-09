#include "Trading/RPGTradeAssets.h"
#include "RPGTradeJson.h"
#include "Economy/Backend/RPGEconomyBackendJsonCodec.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"
#include "Item/Projection/RPGInventoryProjectionMapper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
// Existing item/economy codecs preserve int64 precision, but numeric conversion may
// truncate fractional tokens. Validate integer wire fields before those conversions.
bool IntegerTokens(const TSharedPtr<FJsonValue>& Value,const FString& Key=FString())
{
    if(!Value)return false;
    if(Value->Type==EJson::Object)
    {for(const auto& Field:Value->AsObject()->Values)if(!IntegerTokens(Field.Value,FString(Field.Key.ToView())))return false;return true;}
    if(Value->Type==EJson::Array)
    {for(const auto& Row:Value->AsArray())if(!IntegerTokens(Row))return false;return true;}
    const bool Required=Key==TEXT("balance") || Key==TEXT("maxBalance") || Key==TEXT("revision") || Key==TEXT("quantity")
        || Key==TEXT("generationSeed") || Key==TEXT("definitionVersion") || Key==TEXT("slotIndex")
        || Key==TEXT("current") || Key==TEXT("maximum") || Key==TEXT("ruleVersion") || Key==TEXT("level");
    if(Required && Value->Type!=EJson::Number)return false;
    if(Value->Type!=EJson::Number || Key==TEXT("value"))return true; // Stat values are finite doubles, validated by the item codec.
    FString Raw;if(!Value->TryGetString(Raw) || Raw.IsEmpty())return false;
    const bool Negative=Raw[0]=='-';if(Negative)Raw.RightChopInline(1);
    auto Text=MakeShared<FJsonObject>();Text->SetStringField(TEXT("n"),Raw);int64 Number=0;
    if(!RPGTradeClient::Integer(Text,TEXT("n"),Number,false) || (Negative && Number==0))return false;
    const bool Wide=Key==TEXT("balance") || Key==TEXT("maxBalance") || Key==TEXT("revision");
    return Wide || Number<=int64(MAX_int32)+(Negative && Key==TEXT("generationSeed")?1:0);
}
}

bool FRPGTradeAssets::Decode(const FString& Json,const FRPGTradeSession& Expected,FGuid Actor,const FString& Account,FRPGTradeAssets& Out)
{
    using namespace RPGTradeClient;
    FObject Root;FGuid Character;bool Complete=false;FDateTime Observed;FRPGTradeSession Session;
    if(Expected.Status!=ERPGTradeStatus::Completed || !Actor.IsValid() || Account.IsEmpty()
        || !ReadObject(Json,Root,4*1024*1024) || !Guid(Root,TEXT("characterId"),Character) || Character!=Actor
        || !Boolean(Root,TEXT("isComplete"),Complete) || !Complete || !Date(Root,TEXT("observedAt"),Observed)
        || !Root->HasTypedField<EJson::Object>(TEXT("session"))
        || !ReadSession(Root->GetObjectField(TEXT("session")),Actor,Session) || !(Session==Expected)) return false;
    // Reparse with number lexemes preserved before serializing the nested wallet.
    FObject Precise;const FObject* Wallet=nullptr;FString WalletJson;FRPGTradeAssets Result;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Precise,FJsonSerializer::EFlags::StoreNumbersAsStrings)
        || !Precise->TryGetObjectField(TEXT("wallet"),Wallet) || !Wallet || !Wallet->IsValid()
        || !IntegerTokens(MakeShared<FJsonValueObject>(*Wallet)) || !IntegerTokens(Precise->TryGetField(TEXT("items")))
        || !FJsonSerializer::Serialize(Wallet->ToSharedRef(),TJsonWriterFactory<>::Create(&WalletJson))
        || !FRPGEconomyBackendJsonCodec::DeserializeWalletResponse(WalletJson,Result.Wallet)) return false;
    FGuid WalletCharacter,Roster;
    if(!FGuid::Parse(Result.Wallet.CharacterId,WalletCharacter) || WalletCharacter!=Actor || Result.Wallet.AccountId!=Account
        || !FGuid::Parse(Result.Wallet.RosterId,Roster) || !Roster.IsValid()) return false;
    for(const auto& Balance:Result.Wallet.Balances)
    {
        FGuid Owner;
        if(!Balance.IsValid() || Balance.Balance>Balance.MaxBalance) return false;
        if(Balance.Scope==ERPGCurrencyScope::Account) { if(Balance.OwnerId!=Account)return false; }
        else if(!FGuid::Parse(Balance.OwnerId,Owner) || Owner!=(Balance.Scope==ERPGCurrencyScope::Roster?Roster:Actor))return false;
    }
    TArray<FRPGItemRecord> Records;TSet<FGuid> Ids;
    if(!FRPGItemBackendJsonCodec::DeserializeLoadResponse(Json,Records) || Records.Num()>500) return false;
    for(const auto& Item:Records)
    {
        FGuid Owner;
        if(!Item.IsActive() || Item.GetOwner().Type!=ERPGItemOwnerType::Character || !FGuid::Parse(Item.GetOwner().OwnerId,Owner)
            || Owner!=Actor || Ids.Contains(Item.GetItemId())) return false;
        Ids.Add(Item.GetItemId());
    }
    const FRPGItemOwnerRef Owner{ERPGItemOwnerType::Character,Actor.ToString(EGuidFormats::DigitsWithHyphensLower)};
    if(!FRPGInventoryProjectionMapper::BuildInventorySnapshot(Owner,Records,Result.Items)
        || !FRPGInventoryProjectionMapper::BuildMailSnapshot(Owner,Records,Result.Mail)) return false;
    Out=MoveTemp(Result);return true;
}
