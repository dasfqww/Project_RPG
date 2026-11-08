#pragma once
#include "CoreMinimal.h"
#include "Trading/RPGTradeClient.h"
class FJsonObject;
namespace RPGTradeClient
{
using FObject = TSharedPtr<FJsonObject>;
bool ReadObject(const FString& Text, FObject& Out, int32 MaximumBytes = 32768);
bool String(const FObject& Obj, const TCHAR* Key, FString& Out);
bool Guid(const FObject& Obj, const TCHAR* Key, FGuid& Out);
bool Integer(const FObject& Obj, const TCHAR* Key, int64& Out, bool bPositive);
bool Date(const FObject& Obj, const TCHAR* Key, FDateTime& Out);
bool Boolean(const FObject& Obj, const TCHAR* Key, bool& Out);
bool ReadSession(const FObject& Obj, const FGuid& Character, FRPGTradeSession& Out);
}
