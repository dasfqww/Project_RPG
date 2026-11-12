#include "Auction/RPGAuctionIntentJournal.h"
#include "Trading/RPGTradeJson.h"
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

namespace RPGAuctionJournal
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
    ERPGAuctionJournalRead Failure=ERPGAuctionJournalRead::Unavailable;
    FLockedPath(const FString& Root,const FRPGTradeIntentScope& Scope)
    {
#if PLATFORM_WINDOWS
        if(Root.IsEmpty() || FPaths::IsRelative(Root) || !FRPGAuctionIntentJournal::ScopeKey(Scope,Key))return;
        FString Directory=FPaths::ConvertRelativePathToFull(Root);FPaths::NormalizeDirectoryName(Directory);
        auto& Platform=FPlatformFileManager::Get().GetPlatformFile();
        if(Directory.Len()<=3 || !Plain(Directory) || !Platform.CreateDirectoryTree(*Directory) || !Plain(Directory))return;
        Path=Directory/(Key+TEXT(".json"));const FString LockPath=Directory/(Key+TEXT(".lock"));
        if(!Plain(Path) || !Plain(LockPath))return;
        Lock.Reset(Platform.OpenWrite(*LockPath,false,false));if(!Lock)Failure=ERPGAuctionJournalRead::Busy;
#endif
    }
};
bool Fingerprint(const FString& Key,const FRPGAuctionIntent& I,FString& Out)
{
    return I.IsValid() && Hash(TEXT("AuctionIntent.v1\n")+Key+TEXT("\n")+Id(I.Request.RequestId)+TEXT("\n")
        +Id(I.Request.ListingId)+TEXT("\n")+LexToString(static_cast<uint8>(I.Request.Action))+TEXT("\n")+I.Request.RelativePath+TEXT("\n")+I.Request.Body,Out);
}
bool Same(const FRPGAuctionIntent& A,const FRPGAuctionIntent& B)
{
    return A.Scope==B.Scope && A.Request.RequestId==B.Request.RequestId && A.Request.ListingId==B.Request.ListingId && A.Request.Action==B.Request.Action
        && A.Request.RelativePath==B.Request.RelativePath && A.Request.Body==B.Request.Body;
}
ERPGAuctionJournalRead Read(const FString& Path,const FString& Key,const FRPGTradeIntentScope& Scope,FRPGAuctionIntent& Out)
{
    Out={};auto& Platform=FPlatformFileManager::Get().GetPlatformFile();
    if(!Plain(Path) || Platform.DirectoryExists(*Path))return ERPGAuctionJournalRead::Invalid;
    if(!Platform.FileExists(*Path))return ERPGAuctionJournalRead::Missing;
    const int64 Size=Platform.FileSize(*Path);if(Size<1 || Size>MaximumBytes)return ERPGAuctionJournalRead::Invalid;
    FString Text,Version,SavedKey,Character,SavedFingerprint,Action;RPGTradeClient::FObject Obj;
    FRPGAuctionIntent I;I.Scope=Scope;I.Request.Delivery=ERPGTradeDelivery::Unknown;
    if(!FFileHelper::LoadFileToString(Text,*Path) || !RPGTradeClient::ReadObject(Text,Obj,MaximumBytes) || Obj->Values.Num()!=9
        || !RPGTradeClient::String(Obj,TEXT("version"),Version) || Version!=TEXT("1")
        || !RPGTradeClient::String(Obj,TEXT("scope"),SavedKey) || SavedKey!=Key
        || !RPGTradeClient::String(Obj,TEXT("characterId"),Character) || Character!=Id(Scope.Character)
        || !RPGTradeClient::Guid(Obj,TEXT("requestId"),I.Request.RequestId) || !RPGTradeClient::Guid(Obj,TEXT("listingId"),I.Request.ListingId)
        || !RPGTradeClient::String(Obj,TEXT("path"),I.Request.RelativePath) || !RPGTradeClient::String(Obj,TEXT("body"),I.Request.Body)
        || !RPGTradeClient::String(Obj,TEXT("action"),Action) || !RPGTradeClient::String(Obj,TEXT("fingerprint"),SavedFingerprint))return ERPGAuctionJournalRead::Invalid;
    if(Action==TEXT("register"))I.Request.Action=ERPGAuctionAction::Register;else if(Action==TEXT("purchase"))I.Request.Action=ERPGAuctionAction::Purchase;else if(Action==TEXT("cancel"))I.Request.Action=ERPGAuctionAction::Cancel;else if(Action==TEXT("claim"))I.Request.Action=ERPGAuctionAction::Claim;else return ERPGAuctionJournalRead::Invalid;
    FString Expected;if(!Fingerprint(Key,I,Expected) || SavedFingerprint!=Expected)return ERPGAuctionJournalRead::Invalid;
    Out=MoveTemp(I);return ERPGAuctionJournalRead::Ready;
}
bool Write(const FString& Path,const FString& Key,const FRPGAuctionIntent& I)
{
    FString FingerprintValue;if(!Fingerprint(Key,I,FingerprintValue))return false;
    auto Obj=MakeShared<FJsonObject>();Obj->SetStringField(TEXT("version"),TEXT("1"));Obj->SetStringField(TEXT("scope"),Key);
    Obj->SetStringField(TEXT("characterId"),Id(I.Scope.Character));Obj->SetStringField(TEXT("requestId"),Id(I.Request.RequestId));
    Obj->SetStringField(TEXT("listingId"),Id(I.Request.ListingId));Obj->SetStringField(TEXT("path"),I.Request.RelativePath);
    Obj->SetStringField(TEXT("body"),I.Request.Body);
    const TCHAR* Action=I.Request.Action==ERPGAuctionAction::Register?TEXT("register"):I.Request.Action==ERPGAuctionAction::Purchase?TEXT("purchase"):I.Request.Action==ERPGAuctionAction::Cancel?TEXT("cancel"):TEXT("claim");Obj->SetStringField(TEXT("action"),Action);Obj->SetStringField(TEXT("fingerprint"),FingerprintValue);
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

bool FRPGAuctionIntentJournal::ScopeKey(const FRPGTradeIntentScope& Scope,FString& OutKey)
{
    OutKey.Reset();return Scope.IsValid() && RPGAuctionJournal::Hash(TEXT("AuctionIntentScope.v1\n")+Scope.Endpoint+TEXT("\n")
        +Scope.Account+TEXT("\n")+RPGAuctionJournal::Id(Scope.Character),OutKey);
}
ERPGAuctionJournalRead FRPGAuctionIntentJournal::Load(const FString& Root,const FRPGTradeIntentScope& Scope,FRPGAuctionIntent& Out)
{
    Out={};RPGAuctionJournal::FLockedPath Locked(Root,Scope);
    return Locked.Lock ? RPGAuctionJournal::Read(Locked.Path,Locked.Key,Scope,Out) : Locked.Failure;
}
bool FRPGAuctionIntentJournal::Create(const FString& Root,const FRPGAuctionIntent& Intent)
{
    if(!Intent.IsValid())return false;
    RPGAuctionJournal::FLockedPath Locked(Root,Intent.Scope);FRPGAuctionIntent Existing;
    return Locked.Lock && RPGAuctionJournal::Read(Locked.Path,Locked.Key,Intent.Scope,Existing)==ERPGAuctionJournalRead::Missing
        && RPGAuctionJournal::Write(Locked.Path,Locked.Key,Intent);
}
bool FRPGAuctionIntentJournal::Clear(const FString& Root,const FRPGAuctionIntent& Expected)
{
    if(!Expected.IsValid())return false;
    RPGAuctionJournal::FLockedPath Locked(Root,Expected.Scope);FRPGAuctionIntent Existing;
    return Locked.Lock && RPGAuctionJournal::Read(Locked.Path,Locked.Key,Expected.Scope,Existing)==ERPGAuctionJournalRead::Ready
        && RPGAuctionJournal::Same(Existing,Expected) && RPGAuctionJournal::Plain(Locked.Path)
        && FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Locked.Path);
}
