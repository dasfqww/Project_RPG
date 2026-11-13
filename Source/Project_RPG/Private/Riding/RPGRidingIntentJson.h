#pragma once
#include "Riding/RPGRidingCommand.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
namespace RPGRidingIntentJson
{
    inline bool UniqueJson(const FString& Text)
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
    inline bool Integer(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, int64& Out)
    {
        FString S; if (!O || !O->HasTypedField<EJson::String>(Key) || !O->TryGetStringField(Key, S) || S.IsEmpty() || S.Len() > 19 || (S.Len() > 1 && S[0] == '0')) return false;
        for (TCHAR C : S) if (C < '0' || C > '9') return false;
        if (S.Len() == 19 && S.Compare(TEXT("9223372036854775807"), ESearchCase::CaseSensitive) > 0) return false;
        return LexTryParseString(Out, *S);
    }
    inline bool Number(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, int32& Out)
    {
        double N = 0; if (!O || !O->HasTypedField<EJson::Number>(Key) || !O->TryGetNumberField(Key, N) || !FMath::IsFinite(N) || N < 1 || N > MAX_int32 || FMath::FloorToDouble(N) != N) return false;
        Out = static_cast<int32>(N); return true;
    }
    inline bool String(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FString& Out)
    { return O && O->HasTypedField<EJson::String>(Key) && O->TryGetStringField(Key, Out); }
    inline bool Guid(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FGuid& Out)
    { FString S; return String(O, Key, S) && FGuid::ParseExact(S, EGuidFormats::DigitsWithHyphens, Out) && Out.IsValid(); }
    inline bool Date(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FDateTime& Out)
    { FString S; return String(O, Key, S) && S.Len() <= 40 && FDateTime::ParseIso8601(*S, Out); }
    inline bool OptionalId(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FString& Out)
    {
        Out.Reset(); if (!O || !O->HasField(Key)) return false;
        if (O->HasTypedField<EJson::Null>(Key)) return true;
        return String(O, Key, Out) && RPGRiding::IsValidMountId(Out);
    }
}
