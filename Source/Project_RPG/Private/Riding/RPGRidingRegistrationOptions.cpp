#include "Riding/RPGRidingRegistrationOptions.h"
#include "Riding/RPGRidingIntentJson.h"

namespace
{
    bool Identifier(const FString& Text, int32 Maximum)
    {
        if (Text.IsEmpty() || Text.Len() > Maximum) return false;
        for (int32 I = 0; I < Text.Len(); ++I)
        {
            const TCHAR C = Text[I];
            const bool bAlphaNumeric = (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9');
            if (!bAlphaNumeric && (I == 0 || (C != '.' && C != '_' && C != '-'))) return false;
        }
        return true;
    }
    bool Boolean(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, bool& Out)
    { return O && O->HasTypedField<EJson::Boolean>(Key) && O->TryGetBoolField(Key, Out); }
}
const FRPGRidingRegistrationOption* FRPGRidingRegistrationOptions::Find(const FString& Type, const FString& Name, int32 Version) const
{
    return Options.FindByPredicate([&](const auto& O) { return O.DefinitionType.Equals(Type, ESearchCase::CaseSensitive)
        && O.DefinitionName.Equals(Name, ESearchCase::CaseSensitive) && O.DefinitionVersion == Version; });
}
bool FRPGRidingRegistrationOptions::MatchesCollection(const FRPGRidingCollection& C) const
{
    if (!CharacterId.IsValid() || !RosterId.IsValid() || CharacterId != C.CharacterId || RosterId != C.RosterId || RosterRevision != C.RosterRevision
        || bCatalogReady != C.bCatalogReady || bRegistrationEnabled != C.bRegistrationEnabled || CatalogVersion != C.CatalogVersion
        || !GameplayHash.Equals(C.GameplayHash, ESearchCase::CaseSensitive)) return false;
    for (const auto& O : Options)
    {
        const auto* Owned = C.Mounts.FindByPredicate([&](const auto& M) { return M.MountId == O.MountId; });
        if (O.bAlreadyOwned != (Owned != nullptr)) return false;
    }
    return true;
}
bool RPGRidingRegistrationOptions::Decode(const FString& Body, FGuid ExpectedCharacter, FRPGRidingRegistrationOptions& Out)
{
    using namespace RPGRidingIntentJson;
    Out = {};
    if (!ExpectedCharacter.IsValid() || Body.IsEmpty() || Body.Len() > MaximumBytes || FTCHARToUTF8(*Body).Length() > MaximumBytes || !UniqueJson(Body)) return false;
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Root) || !Root || Root->Values.Num() != 9) return false;
    FRPGRidingRegistrationOptions C; int32 Protocol = 0;
    if (!Number(Root, TEXT("protocolVersion"), Protocol) || Protocol != 1
        || !Guid(Root, TEXT("characterId"), C.CharacterId) || C.CharacterId != ExpectedCharacter || !Guid(Root, TEXT("rosterId"), C.RosterId)
        || !Integer(Root, TEXT("rosterRevision"), C.RosterRevision) || !Boolean(Root, TEXT("catalogReady"), C.bCatalogReady)
        || !Boolean(Root, TEXT("registrationEnabled"), C.bRegistrationEnabled) || C.bCatalogReady != C.bRegistrationEnabled) return false;
    if (C.bCatalogReady)
    {
        if (!Number(Root, TEXT("catalogVersion"), C.CatalogVersion) || !String(Root, TEXT("gameplayHash"), C.GameplayHash) || C.GameplayHash.Len() != 64) return false;
        for (TCHAR Ch : C.GameplayHash) if (!((Ch >= '0' && Ch <= '9') || (Ch >= 'a' && Ch <= 'f'))) return false;
    }
    else if (!Root->HasTypedField<EJson::Null>(TEXT("catalogVersion")) || !Root->HasTypedField<EJson::Null>(TEXT("gameplayHash"))) return false;
    const TArray<TSharedPtr<FJsonValue>>* Options = nullptr;
    if (!Root->TryGetArrayField(TEXT("options"), Options) || Options->Num() > MaximumOptions || (!C.bCatalogReady && !Options->IsEmpty())) return false;
    TMap<FString, TPair<int32, bool>> Mounts;
    for (const auto& Value : *Options)
    {
        if (!Value || Value->Type != EJson::Object) return false;
        const auto O = Value->AsObject(); FRPGRidingRegistrationOption Option;
        if (!O || O->Values.Num() != 8 || !String(O, TEXT("definitionType"), Option.DefinitionType) || !Identifier(Option.DefinitionType, 64)
            || !String(O, TEXT("definitionName"), Option.DefinitionName) || !Identifier(Option.DefinitionName, 128)
            || !Number(O, TEXT("definitionVersion"), Option.DefinitionVersion) || C.Find(Option.DefinitionType, Option.DefinitionName, Option.DefinitionVersion)
            || !String(O, TEXT("mountId"), Option.MountId) || !RPGRiding::IsValidMountId(Option.MountId)
            || !Number(O, TEXT("mountDefinitionVersion"), Option.MountDefinitionVersion) || !Number(O, TEXT("requiredQuantity"), Option.RequiredQuantity)
            || Option.RequiredQuantity != 1 || !Number(O, TEXT("maximumStackSize"), Option.MaximumStackSize) || !Boolean(O, TEXT("alreadyOwned"), Option.bAlreadyOwned)) return false;
        if (const auto* Mount = Mounts.Find(Option.MountId); Mount && (Mount->Key != Option.MountDefinitionVersion || Mount->Value != Option.bAlreadyOwned)) return false;
        Mounts.Add(Option.MountId, {Option.MountDefinitionVersion, Option.bAlreadyOwned}); C.Options.Add(MoveTemp(Option));
    }
    Out = MoveTemp(C); return true;
}
