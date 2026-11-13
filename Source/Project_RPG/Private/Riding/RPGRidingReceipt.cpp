#include "Riding/RPGRidingReceipt.h"
#include "Riding/RPGRidingIntentJson.h"
#include "String/BytesToHex.h"
#include "Policies/CondensedJsonPrintPolicy.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace
{
    FGuid Child(FGuid Parent)
    {
        const auto Text = TEXT("Riding.Register/v1\n") + Parent.ToString(EGuidFormats::DigitsWithHyphensLower);
        FTCHARToUTF8 Bytes(*Text); uint8 Digest[SHA256_DIGEST_LENGTH]; FGuid Result;
        if (!SHA256(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length(), Digest)) return Result;
        Digest[6] = (Digest[6] & 0x0f) | 0x80; Digest[8] = (Digest[8] & 0x3f) | 0x80;
        FGuid::ParseExact(BytesToHex(Digest, 16), EGuidFormats::Digits, Result); return Result;
    }
}
bool RPGRidingReceipt::Decode(const FRPGRidingPendingIntent& I, const FString& Body, bool bRecovery, FRPGRidingReceipt& Out)
{
    using namespace RPGRidingIntentJson;
    Out = {}; if (!I.IsValid() || Body.IsEmpty() || Body.Len() > 256 * 1024 || !UniqueJson(Body)) return false;
    TSharedPtr<FJsonObject> Root; if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Root) || !Root) return false;
    TSharedPtr<FJsonObject> R = Root; FString Envelope;
    if (!bRecovery)
    {
        const TSharedPtr<FJsonObject>* Value = nullptr;
        if (Root->Values.Num() != 2 || !String(Root, TEXT("outcome"), Envelope) || !Root->TryGetObjectField(TEXT("receipt"), Value) || !*Value) return false;
        R = *Value;
    }
    FRPGRidingReceipt C; C.Operation = I.Operation; FString Operation;
    if (!Guid(R, TEXT("requestId"), C.RequestId) || C.RequestId != I.RequestId || !Guid(R, TEXT("characterId"), C.Character) || C.Character != I.Scope.Character
        || !Guid(R, TEXT("rosterId"), C.Roster) || !String(R, TEXT("operation"), Operation) || !String(R, TEXT("outcome"), C.Outcome)
        || !Date(R, TEXT("committedAtUtc"), C.CommittedAt)) return false;
    TSharedPtr<FJsonObject> Command; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(I.Body), Command);
    if (I.Operation == ERPGRidingPersistentOperation::Preferences)
    {
        const TSharedPtr<FJsonObject>* P = nullptr;
        int64 Expected = 0;
        if (Operation != TEXT("Riding.Preferences") || C.Outcome != TEXT("PreferencesSaved") || (!bRecovery && Envelope != TEXT("PreferencesSaved") && Envelope != TEXT("AlreadyApplied"))
            || !Integer(Command, TEXT("expectedRevision"), Expected) || !Integer(R, TEXT("preferencesRevision"), C.Revision) || C.Revision != Expected + 1
            || !R->TryGetObjectField(TEXT("preferences"), P) || !*P || (*P)->Values.Num() != 3) return false;
        auto Match = MakeShared<FJsonObject>(); Match->Values = Command->Values; Match->SetStringField(TEXT("expectedRevision"), LexToString(Expected));
        for (const TCHAR* Key : {TEXT("selectedMountId"), TEXT("favoriteMountIds"), TEXT("shortcut")})
        { const auto V = (*P)->TryGetField(Key); if (!V) return false; Match->SetField(Key, V); }
        FString Text; FJsonSerializer::Serialize(Match, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
        auto Copy = I; Copy.Body = Text; if (!Copy.IsValid() || Copy.Body != I.Body) return false;
    }
    else
    {
        if (Operation != TEXT("Riding.Register") || (C.Outcome != TEXT("Registered") && C.Outcome != TEXT("AlreadyOwned"))
            || (!bRecovery && Envelope != C.Outcome && Envelope != TEXT("AlreadyApplied")) || !Integer(R, TEXT("rosterRevision"), C.Revision)
            || !String(R, TEXT("mountId"), C.MountId) || !RPGRiding::IsValidMountId(C.MountId)) return false;
        if (C.Outcome == TEXT("AlreadyOwned")) { if (!R->HasTypedField<EJson::Null>(TEXT("itemReceipt"))) return false; }
        else
        {
            const TSharedPtr<FJsonObject>* Item = nullptr; const TArray<TSharedPtr<FJsonValue>>* Records = nullptr; FGuid ChildId, ItemId, OriginalId; int64 Revision = 0, Expected = 0; int32 Quantity = 0;
            if (C.Revision < 1 || !R->TryGetObjectField(TEXT("itemReceipt"), Item) || !*Item || !Guid(*Item, TEXT("requestId"), ChildId) || ChildId != Child(I.RequestId)
                || !Number(*Item, TEXT("affectedQuantity"), Quantity) || Quantity != 1 || !(*Item)->TryGetArrayField(TEXT("records"), Records) || Records->Num() != 1
                || !(*Records)[0] || (*Records)[0]->Type != EJson::Object || !Guid(Command, TEXT("itemId"), OriginalId) || !Integer(Command, TEXT("expectedItemRevision"), Expected)) return false;
            const auto Record = (*Records)[0]->AsObject(); const TSharedPtr<FJsonObject>* Owner = nullptr; FString Type, OwnerId;
            if (!Guid(Record, TEXT("itemId"), ItemId) || ItemId != OriginalId || !Integer(Record, TEXT("revision"), Revision) || Revision != Expected + 1
                || !Record->TryGetObjectField(TEXT("owner"), Owner) || !*Owner || !String(*Owner, TEXT("type"), Type) || Type != TEXT("Character")
                || !String(*Owner, TEXT("ownerId"), OwnerId) || OwnerId != I.Scope.Character.ToString(EGuidFormats::DigitsWithHyphensLower)) return false;
            C.bConsumedItem = true; // Remaining historical item fields are deliberately neither exposed nor applied.
        }
    }
    Out = MoveTemp(C); return true;
}
bool RPGRidingReceipt::DefinitiveFirstRejection(int32 Status, const FString& Body)
{
    using namespace RPGRidingIntentJson;
    if (Body.Len() > 8192 || !UniqueJson(Body)) return false; TSharedPtr<FJsonObject> O;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), O) || !O || O->Values.Num() != 1) return false;
    FString Error; if (!String(O, TEXT("error"), Error)) return false;
    if (Status == 400) return Error == TEXT("InvalidRequest") || Error == TEXT("DefinitionMismatch");
    return Status == 409 && (Error == TEXT("RevisionConflict") || Error == TEXT("ItemLocked") || Error == TEXT("ItemExpired")
        || Error == TEXT("AssetReserved") || Error == TEXT("RequiresLobby"));
}
