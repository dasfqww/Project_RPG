#include "Trading/RPGTradeTargets.h"
#include "RPGTradeJson.h"
#include "Dom/JsonObject.h"

bool RPGTradeTargets::NormalizeName(const FString& Input, FString& Out)
{
    if (Input.Len()>64) return false;
    const FString Name=Input.TrimStartAndEnd();
    if (Name.Len()<2 || Name.Len()>20) return false;
    for (TCHAR C:Name) if (FChar::IsControl(C)) return false;
    Out=Name; return true;
}
bool RPGTradeTargets::Decode(const FString& Json, FGuid Actor, const FString& Query, TArray<FRPGTradeTarget>& Out)
{
    RPGTradeClient::FObject Root;
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    FString Normalized;
    if (!Actor.IsValid() || !NormalizeName(Query,Normalized) || !RPGTradeClient::ReadObject(Json,Root,4096)
        || Root->Values.Num()!=1 || !Root->TryGetArrayField(TEXT("targets"),Rows) || !Rows || Rows->Num()>1) return false;
    TArray<FRPGTradeTarget> Result;
    for (const auto& Row:*Rows)
    {
        if (!Row || Row->Type!=EJson::Object) return false;
        const auto Object=Row->AsObject(); FRPGTradeTarget Target; FString Name;
        if (!Object || Object->Values.Num()!=2 || !RPGTradeClient::Guid(Object,TEXT("characterId"),Target.Character)
            || Target.Character==Actor || !RPGTradeClient::String(Object,TEXT("name"),Target.Name)
            || !NormalizeName(Target.Name,Name) || Name!=Target.Name || !Name.Equals(Normalized,ESearchCase::IgnoreCase)) return false;
        Result.Add(MoveTemp(Target));
    }
    Out=MoveTemp(Result); return true;
}
