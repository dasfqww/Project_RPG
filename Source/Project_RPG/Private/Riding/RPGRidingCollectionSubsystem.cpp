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
#include "Misc/Paths.h"

void URPGRidingCollectionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection); bEnding = false;
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickCollection), .25f);
}
void URPGRidingCollectionSubsystem::Deinitialize()
{
    bEnding = true; FTSTicker::RemoveTicker(TickHandle); TickHandle.Reset();
    ReleaseReader();
    if (FreshReads) FreshReads->Shutdown(); FreshReads.Reset();
    if (Commands) Commands->Shutdown(); Commands.Reset();
    Super::Deinitialize();
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
TSharedPtr<FRPGRidingRegistrationReader> URPGRidingCollectionSubsystem::GetRegistrationReader()
{
    if (bEnding || !ReadContext().bEligible) return nullptr;
    if (!RegistrationReader)
    {
        auto* Web = GetLocalPlayer()->GetGameInstance()->GetSubsystem<UHttpWebManager>();
        const TWeakObjectPtr<ThisClass> Weak(this);
        RegistrationReader = MakeShared<FRPGRidingRegistrationReader>(Web->CreatePlayerRidingOwnerTransport(), [Weak]
            { const auto* Self = Weak.Get(); return Self ? Self->ReadContext() : FRPGRidingCollectionContext{}; });
    }
    RegistrationReader->RefreshContext(); return RegistrationReader;
}
FRPGRidingCommandContext URPGRidingCollectionSubsystem::ReadCommandContext() const
{
    FRPGRidingCommandContext C;
    auto* Player = GetLocalPlayer(); auto* GI = Player ? Player->GetGameInstance() : nullptr;
    auto* Web = GI ? GI->GetSubsystem<UHttpWebManager>() : nullptr;
    if (bEnding || !GI || GI->GetFirstGamePlayer() != Player || !Web || IsRunningDedicatedServer() || Web->IsConfiguredForDungeonServer()) return C;
    const auto* Flow = GI->GetSubsystem<URPGDungeonSessionSubsystem>(); FGuid Character;
    if (Flow) FGuid::Parse(Flow->GetSelectedCharacterId(), Character);
    // Preserve identity during credential refresh/expiry; eligibility and transport still require current Player auth.
    FRPGRidingIntentScope::Create(Web->GetApiUrl(), Web->GetAuthenticatedSteamId(), Character, C.Scope);
    C.bEligible = ReadContext().bEligible;
    return C;
}
TSharedPtr<FRPGRidingCommandCoordinator> URPGRidingCollectionSubsystem::GetCommands()
{
    if (bEnding) return nullptr;
    if (!Commands)
    {
        auto* Player = GetLocalPlayer(); auto* GI = Player ? Player->GetGameInstance() : nullptr;
        auto* Web = GI ? GI->GetSubsystem<UHttpWebManager>() : nullptr;
        if (!GI || GI->GetFirstGamePlayer() != Player || !Web || IsRunningDedicatedServer() || Web->IsConfiguredForDungeonServer()) return nullptr;
        const TWeakObjectPtr<ThisClass> Weak(this);
        Commands = MakeShared<FRPGRidingCommandCoordinator>(Web->CreatePlayerRidingOwnerTransport(), [Weak]
            { const auto* Self = Weak.Get(); return Self ? Self->ReadCommandContext() : FRPGRidingCommandContext{}; },
            FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("RidingIntents/v1")));
    }
    Commands->RefreshContext(); return Commands;
}
bool URPGRidingCollectionSubsystem::BlocksTransitions()
{ const auto Owner = GetCommands(); return Owner && Owner->BlocksTransitions(); }
TSharedPtr<FRPGRidingFreshRead> URPGRidingCollectionSubsystem::GetFreshReads()
{
    const auto Owner = GetCommands(); if (!Owner) return nullptr;
    if (!FreshReads)
    {
        auto* GI = GetLocalPlayer()->GetGameInstance(); auto* Web = GI->GetSubsystem<UHttpWebManager>();
        const TWeakObjectPtr<ThisClass> Weak(this);
        const TWeakObjectPtr<URPGEquipmentMutationSubsystem> Equipment(GI->GetSubsystem<URPGEquipmentMutationSubsystem>());
        FRPGRidingFreshRead::FSink Sink;
        Sink.Begin = [Weak, Equipment](FGuid Character)
        { auto* S = Weak.Get(); return S && !S->bEnding && Equipment.IsValid() && Equipment->BeginExternalRead(Character, S->InventoryTicket); };
        Sink.IsCurrent = [Weak, Equipment]
        { const auto* S = Weak.Get(); return S && !S->bEnding && Equipment.IsValid() && Equipment->IsExternalReadCurrent(S->InventoryTicket); };
        Sink.Inventory = [Weak, Equipment](const TArray<FRPGInventoryProjectionEntry>& Items, const TArray<FRPGInventoryProjectionEntry>& Mail)
        { auto* S = Weak.Get(); return S && !S->bEnding && Equipment.IsValid() && Equipment->ApplyExternalInventoryRead(S->InventoryTicket, Items, Mail); };
        Sink.Collection = [Weak](const FRPGRidingCollection& C)
        {
            auto* S = Weak.Get(); if (!S || S->bEnding) return false;
            if (S->RegistrationReader) S->RegistrationReader->Invalidate();
            return !S->Reader || S->Reader->InstallFreshSnapshot(S->ReadContext(), C);
        };
        FreshReads = MakeShared<FRPGRidingFreshRead>(Owner.ToSharedRef(), Web->CreatePlayerRidingOwnerTransport(),
            [Weak] { const auto* S = Weak.Get(); return S ? S->ReadCommandContext() : FRPGRidingCommandContext{}; }, MoveTemp(Sink));
    }
    FreshReads->RefreshContext(); return FreshReads;
}
bool URPGRidingCollectionSubsystem::RefreshFreshAssets()
{ const auto Fresh = GetFreshReads(); return Fresh && Fresh->Refresh(); }
void URPGRidingCollectionSubsystem::ReleaseReader()
{
    if (Reader) Reader->Shutdown(); Reader.Reset();
    if (RegistrationReader) RegistrationReader->Shutdown(); RegistrationReader.Reset();
    if (PresentationLoad) PresentationLoad->CancelHandle(); PresentationLoad.Reset(); PresentationKey.Reset();
}
bool URPGRidingCollectionSubsystem::TickCollection(float)
{
    if (bEnding) return false;
    if (const auto Fresh = GetFreshReads(); Fresh && Fresh->IsUnattempted())
        Fresh->Refresh(); // One initial GET sequence after a verified receipt; failures require explicit refresh.
    if (Reader) { Reader->RefreshContext(); RefreshPresentationAssets(); }
    if (RegistrationReader) RegistrationReader->RefreshContext();
    return true; // Never sends a mutation or retries a failed read here.
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
