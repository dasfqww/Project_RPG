#include "Trading/RPGTradeIntentJournal.h"
#include "RPGTradeJson.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "String/BytesToHex.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace RPGTradeJournal
{
constexpr int64 MaximumBytes=65536;
FString Id(FGuid Value){return Value.ToString(EGuidFormats::DigitsWithHyphensLower);}
bool Hash(const FString& Text,FString& Out)
{
    Out.Reset();const FTCHARToUTF8 Bytes(*Text);uint8 Digest[SHA256_DIGEST_LENGTH];
    if(!SHA256(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length(),Digest))return false;
    Out=BytesToHex(Digest,SHA256_DIGEST_LENGTH).ToLower();return true;
}
bool Plain(const FString& Path)
{
    auto& Platform=FPlatformFileManager::Get().GetPlatformFile();
    FString Cursor=FPaths::ConvertRelativePathToFull(Path);
    while(!Cursor.IsEmpty())
    {
        if(Platform.IsSymlink(*Cursor)!=ESymlinkResult::NonSymlink)return false;
        const FString Parent=FPaths::GetPath(Cursor);if(Parent==Cursor)break;Cursor=Parent;
    }
    return true;
}
struct FLockedPath
{
    FString Path,Key;TUniquePtr<IFileHandle> Lock;
    ERPGTradeJournalRead Failure=ERPGTradeJournalRead::Unavailable;
    FLockedPath(const FString& Root,const FRPGTradeIntentScope& Scope)
    {
#if PLATFORM_WINDOWS
        if(Root.IsEmpty() || FPaths::IsRelative(Root) || !FRPGTradeIntentJournal::ScopeKey(Scope,Key))return;
        FString Directory=FPaths::ConvertRelativePathToFull(Root);FPaths::NormalizeDirectoryName(Directory);
        auto& Platform=FPlatformFileManager::Get().GetPlatformFile();
        if(Directory.Len()<=3 || !Plain(Directory) || !Platform.CreateDirectoryTree(*Directory) || !Plain(Directory))return;
        Path=Directory/(Key+TEXT(".json"));const FString LockPath=Directory/(Key+TEXT(".lock"));
        if(!Plain(Path) || !Plain(LockPath))return;
        Lock.Reset(Platform.OpenWrite(*LockPath,false,false));if(!Lock)Failure=ERPGTradeJournalRead::Busy;
#endif
    }
};
bool Fingerprint(const FString& Key,const FRPGTradePendingIntent& I,FString& Out)
{
    return I.IsValid() && Hash(TEXT("TradeIntent.v1\n")+Key+TEXT("\n")+Id(I.Request.RequestId)+TEXT("\n")
        +Id(I.Request.TradeId)+TEXT("\n")+I.Request.RelativePath+TEXT("\n")+I.Request.Body,Out);
}
bool Same(const FRPGTradePendingIntent& A,const FRPGTradePendingIntent& B)
{
    return A.Scope==B.Scope && A.Request.RequestId==B.Request.RequestId && A.Request.TradeId==B.Request.TradeId
        && A.Request.RelativePath==B.Request.RelativePath && A.Request.Body==B.Request.Body;
}
ERPGTradeJournalRead Read(const FString& Path,const FString& Key,const FRPGTradeIntentScope& Scope,FRPGTradePendingIntent& Out)
{
    Out={};auto& Platform=FPlatformFileManager::Get().GetPlatformFile();
    if(!Plain(Path) || Platform.DirectoryExists(*Path))return ERPGTradeJournalRead::Invalid;
    if(!Platform.FileExists(*Path))return ERPGTradeJournalRead::Missing;
    const int64 Size=Platform.FileSize(*Path);if(Size<1 || Size>MaximumBytes)return ERPGTradeJournalRead::Invalid;
    FString Text,Version,SavedKey,Character,SavedFingerprint;RPGTradeClient::FObject Obj;
    FRPGTradePendingIntent I;I.Scope=Scope;I.Request.Delivery=ERPGTradeDelivery::Unknown;
    if(!FFileHelper::LoadFileToString(Text,*Path) || !RPGTradeClient::ReadObject(Text,Obj,MaximumBytes) || Obj->Values.Num()!=8
        || !RPGTradeClient::String(Obj,TEXT("version"),Version) || Version!=TEXT("1")
        || !RPGTradeClient::String(Obj,TEXT("scope"),SavedKey) || SavedKey!=Key
        || !RPGTradeClient::String(Obj,TEXT("characterId"),Character) || Character!=Id(Scope.Character)
        || !RPGTradeClient::Guid(Obj,TEXT("requestId"),I.Request.RequestId) || !RPGTradeClient::Guid(Obj,TEXT("tradeId"),I.Request.TradeId)
        || !RPGTradeClient::String(Obj,TEXT("path"),I.Request.RelativePath) || !RPGTradeClient::String(Obj,TEXT("body"),I.Request.Body)
        || !RPGTradeClient::String(Obj,TEXT("fingerprint"),SavedFingerprint))return ERPGTradeJournalRead::Invalid;
    FString Expected;if(!Fingerprint(Key,I,Expected) || SavedFingerprint!=Expected)return ERPGTradeJournalRead::Invalid;
    Out=MoveTemp(I);return ERPGTradeJournalRead::Ready;
}
bool Write(const FString& Path,const FString& Key,const FRPGTradePendingIntent& I)
{
    FString FingerprintValue;if(!Fingerprint(Key,I,FingerprintValue))return false;
    auto Obj=MakeShared<FJsonObject>();Obj->SetStringField(TEXT("version"),TEXT("1"));Obj->SetStringField(TEXT("scope"),Key);
    Obj->SetStringField(TEXT("characterId"),Id(I.Scope.Character));Obj->SetStringField(TEXT("requestId"),Id(I.Request.RequestId));
    Obj->SetStringField(TEXT("tradeId"),Id(I.Request.TradeId));Obj->SetStringField(TEXT("path"),I.Request.RelativePath);
    Obj->SetStringField(TEXT("body"),I.Request.Body);Obj->SetStringField(TEXT("fingerprint"),FingerprintValue);
    FString Text;if(!FJsonSerializer::Serialize(Obj,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)))return false;
    const FTCHARToUTF8 Bytes(*Text);if(Bytes.Length()>MaximumBytes)return false;
    const FString Temporary=Path+TEXT(".tmp-")+Id(FGuid::NewGuid());auto& Platform=FPlatformFileManager::Get().GetPlatformFile();
    if(!Plain(Temporary) || Platform.FileExists(*Temporary))return false;
    TUniquePtr<IFileHandle> Output(Platform.OpenWrite(*Temporary,false,false));
    const bool Written=Output && Output->Write(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length()) && Output->Flush(true);Output.Reset();
    const bool Moved=Written && Plain(Path) && IFileManager::Get().Move(*Path,*Temporary,false,false,false,true);
    if(!Moved && Plain(Temporary))Platform.DeleteFile(*Temporary);return Moved;
}
}

bool FRPGTradeIntentJournal::ScopeKey(const FRPGTradeIntentScope& Scope,FString& OutKey)
{
    OutKey.Reset();return Scope.IsValid() && RPGTradeJournal::Hash(TEXT("TradeIntentScope.v1\n")+Scope.Endpoint+TEXT("\n")
        +Scope.Account+TEXT("\n")+RPGTradeJournal::Id(Scope.Character),OutKey);
}
ERPGTradeJournalRead FRPGTradeIntentJournal::Load(const FString& Root,const FRPGTradeIntentScope& Scope,FRPGTradePendingIntent& Out)
{
    Out={};RPGTradeJournal::FLockedPath Locked(Root,Scope);
    return Locked.Lock ? RPGTradeJournal::Read(Locked.Path,Locked.Key,Scope,Out) : Locked.Failure;
}
bool FRPGTradeIntentJournal::Create(const FString& Root,const FRPGTradePendingIntent& Intent)
{
    if(!Intent.IsValid())return false;
    RPGTradeJournal::FLockedPath Locked(Root,Intent.Scope);FRPGTradePendingIntent Existing;
    return Locked.Lock && RPGTradeJournal::Read(Locked.Path,Locked.Key,Intent.Scope,Existing)==ERPGTradeJournalRead::Missing
        && RPGTradeJournal::Write(Locked.Path,Locked.Key,Intent);
}
bool FRPGTradeIntentJournal::Clear(const FString& Root,const FRPGTradePendingIntent& Expected)
{
    if(!Expected.IsValid())return false;
    RPGTradeJournal::FLockedPath Locked(Root,Expected.Scope);FRPGTradePendingIntent Existing;
    return Locked.Lock && RPGTradeJournal::Read(Locked.Path,Locked.Key,Expected.Scope,Existing)==ERPGTradeJournalRead::Ready
        && RPGTradeJournal::Same(Existing,Expected) && RPGTradeJournal::Plain(Locked.Path)
        && FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Locked.Path);
}
