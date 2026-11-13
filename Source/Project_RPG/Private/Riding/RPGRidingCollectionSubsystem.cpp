#include "Riding/RPGRidingCollectionSubsystem.h"
#include "Riding/RPGMountDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Manager/HttpWebManager.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"

void URPGRidingCollectionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection); bEnding = false;
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickCollection), .25f);
}
void URPGRidingCollectionSubsystem::Deinitialize()
{
    bEnding = true; FTSTicker::RemoveTicker(TickHandle); TickHandle.Reset();
    ReleaseReader(); Super::Deinitialize();
}
FRPGRidingCollectionContext URPGRidingCollectionSubsystem::ReadContext() const
{
    FRPGRidingCollectionContext C;
    auto* Player = GetLocalPlayer(); auto* GI = Player ? Player->GetGameInstance() : nullptr;
    auto* Web = GI ? GI->GetSubsystem<UHttpWebManager>() : nullptr;
    if (bEnding || !GI || GI->GetFirstGamePlayer() != Player || !Web || IsRunningDedicatedServer() || Web->IsConfiguredForDungeonServer()) return C;
    if (!Web->GetPlayerEquipmentContext(C.Endpoint, C.Account)) return C;
    auto* Flow = GI->GetSubsystem<URPGDungeonSessionSubsystem>();
    if (Flow) FGuid::Parse(Flow->GetSelectedCharacterId(), C.Character);
    auto* Lobby = GI->GetSubsystem<URPGOnlineLobbySubsystem>();
    auto* PC = GI->GetWorld() ? Player->GetPlayerController(GI->GetWorld()) : nullptr;
    auto* Widget = Lobby ? Lobby->GetLobbyWidget() : nullptr;
    C.bEligible = Web->HasAccessToken() && Lobby && Lobby->IsLobbyController(PC) && Widget
        && Widget->GetOwningLocalPlayer() == Player && Widget->CanUseRidingCollection();
    return C;
}
TSharedPtr<FRPGRidingCollectionReader> URPGRidingCollectionSubsystem::GetReader()
{
    if (bEnding || !ReadContext().bEligible) return nullptr;
    if (!Reader)
    {
        auto* GI = GetLocalPlayer()->GetGameInstance(); auto* Web = GI->GetSubsystem<UHttpWebManager>();
        const TWeakObjectPtr<ThisClass> Weak(this);
        Reader = MakeShared<FRPGRidingCollectionReader>(Web->CreatePlayerRidingOwnerTransport(), [Weak]
            { const auto* Self = Weak.Get(); return Self ? Self->ReadContext() : FRPGRidingCollectionContext{}; });
    }
    Reader->RefreshContext(); return Reader;
}
void URPGRidingCollectionSubsystem::ReleaseReader()
{
    if (Reader) Reader->Shutdown(); Reader.Reset();
    if (PresentationLoad) PresentationLoad->CancelHandle(); PresentationLoad.Reset(); PresentationKey.Reset();
}
bool URPGRidingCollectionSubsystem::TickCollection(float)
{
    if (bEnding) return false;
    if (Reader) { Reader->RefreshContext(); RefreshPresentationAssets(); }
    return true; // No HTTP dispatch from this ticker.
}
void URPGRidingCollectionSubsystem::RefreshPresentationAssets()
{
    const auto Context = Reader ? Reader->GetContext() : FRPGRidingCollectionContext{};
    const auto* C = Reader ? Reader->GetCollection() : nullptr;
    FString Key = C ? Context.Endpoint + TEXT("|") + Context.Account + TEXT("|") + C->CharacterId.ToString() + TEXT("/") + LexToString(C->RosterRevision) : FString();
    if (C) for (const auto& Mount : C->Mounts) Key += TEXT("/") + Mount.MountId;
    if (Key == PresentationKey) return;
    PresentationKey = Key;
    if (PresentationLoad) PresentationLoad->CancelHandle(); PresentationLoad.Reset();
    if (!C || !UAssetManager::IsInitialized()) return;
    auto& Assets = UAssetManager::Get(); TArray<FSoftObjectPath> Paths;
    for (const auto& Mount : C->Mounts)
    {
        const auto Path = Assets.GetPrimaryAssetPath(FPrimaryAssetId(URPGMountDefinition::PrimaryAssetType, FName(*Mount.MountId)));
        if (Path.IsValid()) Paths.AddUnique(Path);
    }
    // Load metadata only; mesh, animation and icon fields are soft references. Release only this handle.
    if (!Paths.IsEmpty()) PresentationLoad = Assets.GetStreamableManager().RequestAsyncLoad(Paths);
}
FText URPGRidingCollectionSubsystem::DescribeMount(const FString& Id) const
{
    if (UAssetManager::IsInitialized())
    {
        const auto* Definition = Cast<URPGMountDefinition>(UAssetManager::Get().GetPrimaryAssetObject(
            FPrimaryAssetId(URPGMountDefinition::PrimaryAssetType, FName(*Id))));
        if (Definition && Definition->MountId == Id) return Definition->DisplayName;
    }
    return FText::GetEmpty();
}
