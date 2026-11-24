#include "UI/Riding/RPGMountQuickSlots.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ConfigCacheIni.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Player/RPGPlayerState.h"
#include "Riding/RPGRidingSessionComponent.h"

namespace
{
    FString SlotKey(const APlayerController* Controller, int32 Index)
    {
        if (!Controller || !Controller->IsLocalController() || !Controller->GetLocalPlayer() ||
            !RPGMountQuickSlots::IsMountSlot(Index)) return {};
        const auto* State = Controller->GetPlayerState<ARPGPlayerState>();
        const auto* GI = Controller->GetGameInstance();
        const auto* Flow = GI ? GI->GetSubsystem<URPGDungeonSessionSubsystem>() : nullptr;
        FGuid Character;
        if ((State && FGuid::Parse(State->GetBackendCharacterId(), Character) && Character.IsValid()) ||
            (Flow && FGuid::Parse(Flow->GetSelectedCharacterId(), Character) && Character.IsValid()))
            return Character.ToString(EGuidFormats::Digits) + TEXT(".") + LexToString(Index);
        return Controller->GetWorld() && Controller->GetWorld()->GetNetMode() == NM_Standalone
            ? FString(TEXT("Standalone.")) + LexToString(Index) : FString();
    }
    constexpr TCHAR Section[] = TEXT("ProjectRPG.MountQuickSlots");
}

bool RPGMountQuickSlots::IsMountSlot(int32 Index)
{
    return Index >= 0 && Index < UQuickSlotComponent::PotionSlotIndex;
}

const URPGRidingSessionComponent* RPGMountQuickSlots::FindSession(const APlayerController* Controller)
{
    const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
    const auto* State = Pawn ? Pawn->GetPlayerState() : nullptr;
    return State ? State->FindComponentByClass<URPGRidingSessionComponent>() : nullptr;
}

FString RPGMountQuickSlots::Read(const APlayerController* Controller, int32 Index)
{
    const FString Key = SlotKey(Controller, Index);
    FString MountId;
    if (GConfig && !Key.IsEmpty()) GConfig->GetString(Section, *Key, MountId, GGameUserSettingsIni);
    return RPGRiding::IsValidMountId(MountId) ? MountId : FString();
}

bool RPGMountQuickSlots::Assign(const APlayerController* Controller, int32 Index, const FString& MountId)
{
    const FString Key = SlotKey(Controller, Index);
    if (!GConfig || Key.IsEmpty()) return false;
    if (MountId.IsEmpty())
    {
        if (!GConfig->RemoveKey(Section, *Key, GGameUserSettingsIni)) return true;
    }
    else
    {
        const auto* Session = FindSession(Controller);
        if (!Session || !Session->GetOwnerState().bReady || !Session->GetOwnedMountIds().Contains(MountId)) return false;
        GConfig->SetString(Section, *Key, *MountId, GGameUserSettingsIni);
    }
    GConfig->Flush(false, GGameUserSettingsIni);
    return true;
}
