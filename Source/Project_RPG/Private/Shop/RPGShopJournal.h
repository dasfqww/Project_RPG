#pragma once
#include "Shop/RPGShopTypes.h"
enum class ERPGShopJournal : uint8 { Missing, Ready, Invalid, Unavailable };
namespace RPGShopJournal
{
    ERPGShopJournal Load(const FString& Root,const FRPGTradeIntentScope& Scope,FRPGShopIntent& Out);
    bool Create(const FString& Root,const FRPGShopIntent& Intent);
    bool Clear(const FString& Root,const FRPGShopIntent& Expected);
}
