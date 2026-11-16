#include "Shop/RPGShopJournal.h"
#include "Trading/RPGTradeJson.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "String/BytesToHex.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace
{
FString Hash(const FString& Text)
{const FTCHARToUTF8 B(*Text);uint8 D[SHA256_DIGEST_LENGTH];if(!SHA256(reinterpret_cast<const uint8*>(B.Get()),B.Length(),D))return FString();return BytesToHex(D,SHA256_DIGEST_LENGTH).ToLower();}
FString Key(const FRPGTradeIntentScope& S)
{return S.IsValid()?Hash(TEXT("ShopIntentScope.v1\n")+S.Endpoint+TEXT("\n")+S.Account+TEXT("\n")+S.Character.ToString(EGuidFormats::DigitsWithHyphensLower)):FString();}
bool Plain(const FString& Path)
{
    auto& P=FPlatformFileManager::Get().GetPlatformFile();FString C=FPaths::ConvertRelativePathToFull(Path);
    while(!C.IsEmpty()){if(P.IsSymlink(*C)!=ESymlinkResult::NonSymlink)return false;const FString Parent=FPaths::GetPath(C);if(Parent==C)break;C=Parent;}return true;
}
struct FLock
{
    FString Path,ScopeKey;TUniquePtr<IFileHandle> Handle;
    FLock(const FString& Root,const FRPGTradeIntentScope& S)
    {
#if PLATFORM_WINDOWS
        ScopeKey=Key(S);if(Root.IsEmpty()||FPaths::IsRelative(Root)||ScopeKey.IsEmpty())return;
        FString Dir=FPaths::ConvertRelativePathToFull(Root);FPaths::NormalizeDirectoryName(Dir);auto& P=FPlatformFileManager::Get().GetPlatformFile();
        if(Dir.Len()<=3||!Plain(Dir)||!P.CreateDirectoryTree(*Dir)||!Plain(Dir))return;
        Path=Dir/(ScopeKey+TEXT(".json"));const FString Lock=Dir/(ScopeKey+TEXT(".lock"));
        if(Plain(Path)&&Plain(Lock))Handle.Reset(P.OpenWrite(*Lock,false,false));
#endif
    }
};
FString Fingerprint(const FString& K,const FRPGShopIntent& I)
{return I.IsValid()?Hash(TEXT("ShopIntent.v1\n")+K+TEXT("\n")+I.CurrencyCode+TEXT("\n")+I.Body):FString();}
ERPGShopJournal Read(const FLock& L,const FRPGTradeIntentScope& S,FRPGShopIntent& Out)
{
    Out={};auto& P=FPlatformFileManager::Get().GetPlatformFile();if(!L.Handle)return ERPGShopJournal::Unavailable;
    if(!Plain(L.Path)||P.DirectoryExists(*L.Path))return ERPGShopJournal::Invalid;if(!P.FileExists(*L.Path))return ERPGShopJournal::Missing;
    if(P.FileSize(*L.Path)<1||P.FileSize(*L.Path)>16384)return ERPGShopJournal::Invalid;
    FString Json,Version,SavedKey,Currency,Body,SavedHash;RPGTradeClient::FObject O;FRPGShopIntent I;
    if(!FFileHelper::LoadFileToString(Json,*L.Path)||!RPGTradeClient::ReadObject(Json,O,16384)||O->Values.Num()!=5
        ||!RPGTradeClient::String(O,TEXT("version"),Version)||Version!=TEXT("1")
        ||!RPGTradeClient::String(O,TEXT("scope"),SavedKey)||SavedKey!=L.ScopeKey
        ||!RPGTradeClient::String(O,TEXT("currency"),Currency)||!RPGTradeClient::String(O,TEXT("body"),Body)
        ||!RPGTradeClient::String(O,TEXT("fingerprint"),SavedHash)||!RPGShopCodec::Intent(S,Currency,Body,I)
        ||Fingerprint(L.ScopeKey,I)!=SavedHash)return ERPGShopJournal::Invalid;
    Out=MoveTemp(I);return ERPGShopJournal::Ready;
}
}
ERPGShopJournal RPGShopJournal::Load(const FString& Root,const FRPGTradeIntentScope& S,FRPGShopIntent& Out)
{FLock L(Root,S);return Read(L,S,Out);}
bool RPGShopJournal::Create(const FString& Root,const FRPGShopIntent& I)
{
    if(!I.IsValid())return false;FLock L(Root,I.Scope);FRPGShopIntent Existing;if(Read(L,I.Scope,Existing)!=ERPGShopJournal::Missing)return false;
    auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("version"),TEXT("1"));O->SetStringField(TEXT("scope"),L.ScopeKey);
    O->SetStringField(TEXT("currency"),I.CurrencyCode);O->SetStringField(TEXT("body"),I.Body);O->SetStringField(TEXT("fingerprint"),Fingerprint(L.ScopeKey,I));
    FString Json;if(!FJsonSerializer::Serialize(O,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json)))return false;
    const FTCHARToUTF8 B(*Json);if(B.Length()>16384)return false;auto& P=FPlatformFileManager::Get().GetPlatformFile();
    const FString Temp=L.Path+TEXT(".tmp-")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if(!Plain(Temp)||P.FileExists(*Temp))return false;TUniquePtr<IFileHandle> File(P.OpenWrite(*Temp,false,false));
    const bool Written=File&&File->Write(reinterpret_cast<const uint8*>(B.Get()),B.Length())&&File->Flush(true);File.Reset();
    const bool Moved=Written&&Plain(L.Path)&&IFileManager::Get().Move(*L.Path,*Temp,false,false,false,true);
    if(!Moved&&Plain(Temp))P.DeleteFile(*Temp);return Moved;
}
bool RPGShopJournal::Clear(const FString& Root,const FRPGShopIntent& I)
{if(!I.IsValid())return false;FLock L(Root,I.Scope);FRPGShopIntent Existing;return Read(L,I.Scope,Existing)==ERPGShopJournal::Ready&&I.Same(Existing)&&Plain(L.Path)&&FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*L.Path);}
