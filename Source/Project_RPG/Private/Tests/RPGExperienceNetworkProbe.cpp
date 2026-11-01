// Opt-in, real Iris/network and travel proof. No reflected test types.
#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "GameFeaturesSubsystem.h"
#include "GameState/RPGGameStateBase.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "LoadingScreenManager.h"
#include "Manager/Component/RPGExperienceManagerComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UI/Loading/RPGExperienceFailureNotice.h"
#include "UI/HUD/RPGHUD.h"
#include "UI/Option/RPGOptionMenu.h"
#include "UnrealClient.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace
{
constexpr const TCHAR* ProofMap = TEXT("/Game/System/Experiences/Development/Maps/L_ExperienceDevelopment");
constexpr const TCHAR* ProofDefinition = TEXT("/Game/System/Experiences/Development/DA_CommonClientDevelopment.DA_CommonClientDevelopment");

struct FExperienceProofWorld
{
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<URPGExperienceManagerComponent> Manager;
    int32 Number = 0;
    bool LoadingRecorded = false;
    bool SettledRecorded = false;
    FString DeferredSession;
    TWeakObjectPtr<UNetDriver> NetDriver;
    TWeakObjectPtr<UNetConnection> Connection;
    TWeakObjectPtr<APlayerController> LocalController;
    TWeakObjectPtr<AHUD> HUD;
    TWeakObjectPtr<ULocalPlayer> LocalPlayer;
    FString ControllerName, HUDName;
    TArray<TWeakObjectPtr<UUserWidget>> Widgets;
    bool Ended = false;
    bool EndCleanup = false;
    bool SawReady = false;
    bool ReadyRecorded = false;
    bool FailureRecorded = false;
    bool ServerFailureBit = false;
    double StableSince = 0.0;
    FString Reason;
    FString Path;
    FString PendingScreenshot;
};

struct FExperienceNetworkProof : TSharedFromThis<FExperienceNetworkProof>
{
    FString Output, Role, Scenario, Failure;
    TArray<TSharedRef<FExperienceProofWorld>> Worlds;
    TArray<TSharedPtr<FJsonValue>> Events;
    double Started = FPlatformTime::Seconds();
    double TimeoutSeconds = 240.0;
    bool Finished = false;
    bool Passed = false;
    bool InjectedServerFailure = false;
    bool Travel1Issued = false;
    bool Travel2Issued = false;

    TArray<TWeakObjectPtr<UNetConnection>> SeamlessConnections;
    bool IsServer() const { return Role == TEXT("Server"); }
    FString File(const FString& Who, const FString& Name) const
    {
        return FPaths::Combine(Output, Who, Name);
    }
    bool Exists(const FString& Who, const TCHAR* Name) const
    {
        return IFileManager::Get().FileSize(*File(Who, Name)) > 0;
    }
    static int32 Notices(const UWorld* World, bool VisibleOnly = false)
    {
        int32 Count = 0;
        for (TObjectIterator<URPGExperienceFailureNotice> It; It; ++It)
        {
            if (It->IsInViewport() && It->GetWorld() == World && (!VisibleOnly || It->IsVisible())) { ++Count; }
        }
        return Count;
    }
    static int32 Hints(const UWorld* World)
    {
        int32 Count = 0;
        for (TObjectIterator<UUserWidget> It; It; ++It)
        {
            if (It->IsInViewport() && It->GetWorld() == World
                && It->GetClass()->GetPathName() == TEXT("/Script/RPGOptionsUIRuntime.RPGOptionsShortcutHint")) { ++Count; }
        }
        return Count;
    }
    static int32 Options(const UWorld* World)
    {
        int32 Count = 0;
        for (TObjectIterator<URPGOptionMenu> It; It; ++It)
        {
            if (It->IsInViewport() && It->GetWorld() == World) { ++Count; }
        }
        return Count;
    }
    bool LocalControllerReady(const UWorld& World, const UNetDriver& Driver) const
    {
        if (IsServer()) { return true; }
        APlayerController* PC = World.GetGameInstance()->GetFirstLocalPlayerController(&World);
        ARPGHUD* HUD = PC ? Cast<ARPGHUD>(PC->GetHUD()) : nullptr;
        URPGOptionMenu* Menu = HUD ? HUD->GetOptionMenuWidget() : nullptr;
        return PC && PC->GetWorld() == &World && !PC->IsActorBeingDestroyed()
            && PC->IsLocalPlayerController() && PC->GetLocalPlayer()
            && PC->GetLocalPlayer() == Worlds[0]->LocalPlayer.Get()
            && PC->GetLocalPlayer()->GetPlayerController(&World) == PC
            && Driver.ServerConnection && Driver.ServerConnection->PlayerController == PC
            && HUD && HUD->GetOwningPlayerController() == PC && !HUD->IsActorBeingDestroyed()
            && Menu && Menu->GetWorld() == &World && Menu->GetOwningPlayer() == PC && Menu->IsInViewport();
    }
    static bool ServerBit(URPGExperienceManagerComponent& Manager)
    {
        const FBoolProperty* Bit = FindFProperty<FBoolProperty>(Manager.GetClass(), TEXT("bServerExperienceFailed"));
        return Bit && Bit->GetPropertyValue_InContainer(&Manager);
    }
    static bool HasOwnLoadingTask(URPGExperienceManagerComponent& Manager)
    {
        const FObjectPropertyBase* Task = FindFProperty<FObjectPropertyBase>(Manager.GetClass(), TEXT("LoadingScreenTask"));
        return !Task || Task->GetObjectPropertyValue_InContainer(&Manager) != nullptr;
    }
    bool IsTravelRace() const
    {
        return Scenario == TEXT("LoadingTravel") || Scenario == TEXT("SeamlessTravel") || Scenario == TEXT("LoadingSeamlessTravel");
    }
    bool IsLoadingTravel() const { return Scenario == TEXT("LoadingTravel") || Scenario == TEXT("LoadingSeamlessTravel"); }
    bool UsesSeamless() const { return Scenario == TEXT("SeamlessTravel") || Scenario == TEXT("LoadingSeamlessTravel"); }
    static bool AllFeaturesActive()
    {
        for (const TCHAR* Name : {TEXT("RPGClientPresentation"), TEXT("RPGInputMapping"), TEXT("RPGOptionsUI"), TEXT("RPGClientUI")})
        {
            if (!UGameFeaturesSubsystem::Get().IsGameFeaturePluginActiveByName(Name)) { return false; }
        }
        return true;
    }
    static bool HasOwnedFailureNotices(URPGExperienceManagerComponent& Manager)
    {
        const FArrayProperty* Property = FindFProperty<FArrayProperty>(Manager.GetClass(), TEXT("FailureNotices"));
        if (!Property) { return true; }
        const FScriptArrayHelper Values(Property, Property->ContainerPtrToValuePtr<void>(&Manager));
        return Values.Num() != 0;
    }
    bool ConnectionsPreserved(const UNetDriver& Driver) const
    {
        if (!IsServer()) { return Worlds.Num() > 0 && Worlds[0]->Connection.Get() == Driver.ServerConnection; }
        if (SeamlessConnections.Num() != 2 || Driver.ClientConnections.Num() != 2) { return false; }
        for (const auto& Connection : SeamlessConnections)
        {
            if (!Connection.IsValid() || !Driver.ClientConnections.Contains(Connection.Get())) { return false; }
        }
        return true;
    }
    bool SaveJson(const TSharedRef<FJsonObject>& Json, const FString& Path) const
    {
        FString Text;
        FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
        return FFileHelper::SaveStringToFile(Text, *Path);
    }
    bool SaveReport() const
    {
        const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetStringField(TEXT("Role"), Role);
        Report->SetStringField(TEXT("Scenario"), Scenario);
        Report->SetBoolField(TEXT("Finished"), Finished);
        Report->SetBoolField(TEXT("Passed"), Passed);
        Report->SetStringField(TEXT("Failure"), Failure);
        Report->SetBoolField(TEXT("InjectedServerFailure"), InjectedServerFailure);
        Report->SetBoolField(TEXT("Travel1Issued"), Travel1Issued);
        Report->SetBoolField(TEXT("Travel2Issued"), Travel2Issued);
        Report->SetArrayField(TEXT("Events"), Events);
        TArray<TSharedPtr<FJsonValue>> WorldValues;
        for (const auto& Observation : Worlds)
        {
            const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
            Value->SetNumberField(TEXT("WorldNumber"), Observation->Number);
            Value->SetBoolField(TEXT("SawReady"), Observation->SawReady);
            Value->SetBoolField(TEXT("ReadyRecorded"), Observation->ReadyRecorded);
            Value->SetBoolField(TEXT("FailureRecorded"), Observation->FailureRecorded);
            Value->SetBoolField(TEXT("ReceivedServerFailureBit"), Observation->ServerFailureBit);
            Value->SetBoolField(TEXT("LoadingRecorded"), Observation->LoadingRecorded);
            Value->SetBoolField(TEXT("SettledRecorded"), Observation->SettledRecorded);
            Value->SetStringField(TEXT("DeferredSession"), Observation->DeferredSession);
            Value->SetBoolField(TEXT("Ended"), Observation->Ended);
            Value->SetBoolField(TEXT("EndCleanup"), Observation->EndCleanup);
            Value->SetStringField(TEXT("LastError"), Observation->Reason);
            Value->SetStringField(TEXT("SelectedPath"), Observation->Path);
            WorldValues.Add(MakeShared<FJsonValueObject>(Value));
        }
        Report->SetArrayField(TEXT("Worlds"), WorldValues);
        return SaveJson(Report, File(Role, TEXT("Report.json")));
    }
    bool Finish(const FString& Reason = FString())
    {
        Failure = Reason;
        Finished = true;
        Passed = Reason.IsEmpty();
        if (!SaveReport()) { Passed = false; }
        UE_LOG(LogTemp, Display, TEXT("RPG_EXPERIENCE_NETWORK_PROOF FINISH Role=%s Passed=%d Reason=%s"),
            *Role, Passed, *Failure);
        FPlatformMisc::RequestExitWithStatus(false, Passed ? 0 : 1);
        return false;
    }
    TSharedRef<FExperienceProofWorld> Observe(UWorld& World, URPGExperienceManagerComponent& Manager)
    {
        for (const auto& Observation : Worlds)
        {
            if (Observation->Manager.Get() == &Manager) { return Observation; }
        }
        const TSharedRef<FExperienceProofWorld> Observation = MakeShared<FExperienceProofWorld>();
        Observation->World = &World;
        Observation->Manager = &Manager;
        Observation->NetDriver = World.GetNetDriver();
        Observation->Connection = World.GetNetDriver() ? World.GetNetDriver()->ServerConnection : nullptr;
        Observation->Number = Worlds.Num() + 1;
        Worlds.Add(Observation);
        const TSharedRef<FExperienceNetworkProof> Self = AsShared();
        Manager.OnExperienceEnded.AddLambda([Self, Observation]
        {
            Observation->Ended = true;
            Observation->EndCleanup = Notices(Observation->World.Get()) == 0
                && (!Observation->Manager.IsValid() || (!HasOwnLoadingTask(*Observation->Manager.Get())
                    && !HasOwnedFailureNotices(*Observation->Manager.Get())));
            if (!Observation->EndCleanup)
            {
                Self->Passed = false;
                Self->Failure = TEXT("Manager EndPlay retained a failure notice or its loading task");
            }
            Self->SaveReport();
            UE_LOG(LogTemp, Display, TEXT("RPG_EXPERIENCE_NETWORK_PROOF END Role=%s World=%d Clean=%d"),
                *Self->Role, Observation->Number, Observation->EndCleanup);
        });
        return Observation;
    }
    bool OldWorldsClean() const
    {
        for (int32 Index = 0; Index + 1 < Worlds.Num(); ++Index)
        {
            const auto& Old = Worlds[Index];
            // Seamless travel can move a widget owner into another world. Check the actual old widgets too.
            for (const auto& Widget : Old->Widgets) { if (Widget.IsValid() && Widget->IsInViewport()) { return false; } }
            if (!Old->Ended || !Old->EndCleanup || (Old->World.IsValid()
                && (Notices(Old->World.Get()) != 0 || Hints(Old->World.Get()) != 0))) { return false; }
        }
        return true;
    }
    bool Record(const TCHAR* State, const TSharedRef<FExperienceProofWorld>& Observation,
        const ARPGGameStateBase& GameState, const UNetDriver& Driver)
    {
        const UWorld* World = Observation->World.Get();
        const TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
        Event->SetStringField(TEXT("State"), State);
        const APlayerController* PC = World->GetGameInstance()->GetFirstLocalPlayerController(World);
        const bool DriverPreserved = Worlds[0]->NetDriver.Get() == &Driver;
        const bool ControllerPreserved = IsServer() || (PC && Worlds[0]->LocalController.Get() == PC);
        Event->SetBoolField(TEXT("NetDriverPreserved"), DriverPreserved);
        Event->SetBoolField(TEXT("ConnectionsPreserved"), ConnectionsPreserved(Driver));
        Event->SetBoolField(TEXT("LocalControllerPreserved"), ControllerPreserved);
        Event->SetBoolField(TEXT("LocalControllerReady"), LocalControllerReady(*World, Driver));
        Event->SetBoolField(TEXT("LocalControllerReplaced"), !IsServer() && Observation->Number > 1 && !ControllerPreserved);
        Event->SetBoolField(TEXT("HUDReplaced"), !IsServer() && PC && Observation->Number > 1 && PC->GetHUD() != Worlds[0]->HUD.Get());
        Event->SetStringField(TEXT("InitialController"), Worlds[0]->ControllerName);
        Event->SetStringField(TEXT("CurrentController"), GetNameSafe(PC));
        Event->SetStringField(TEXT("InitialHUD"), Worlds[0]->HUDName);
        Event->SetStringField(TEXT("CurrentHUD"), PC ? GetNameSafe(PC->GetHUD()) : TEXT("None"));
        Event->SetBoolField(TEXT("HUDPreserved"), IsServer() || (PC && PC->GetHUD() == Worlds[0]->HUD.Get()));
        Event->SetBoolField(TEXT("FeaturesActive"), AllFeaturesActive());
        Event->SetBoolField(TEXT("OwnLoadingTask"), HasOwnLoadingTask(*Observation->Manager.Get()));
        Event->SetStringField(TEXT("DeferredSession"), Observation->DeferredSession);
        for (TObjectIterator<UUserWidget> It; It; ++It)
        {
            if (It->GetWorld() == World && It->IsInViewport()
                && (It->IsA<URPGExperienceFailureNotice>() || It->IsA<URPGOptionMenu>()
                    || It->GetClass()->GetPathName() == TEXT("/Script/RPGOptionsUIRuntime.RPGOptionsShortcutHint")))
            {
                Observation->Widgets.AddUnique(*It);
            }
        }
        Event->SetNumberField(TEXT("WorldNumber"), Observation->Number);
        Event->SetNumberField(TEXT("NetMode"), static_cast<int32>(World->GetNetMode()));
        Event->SetBoolField(TEXT("Authority"), GameState.HasAuthority());
        Event->SetBoolField(TEXT("LocalIris"), Driver.IsUsingIrisReplication());
        Event->SetBoolField(TEXT("ReplicatedServerIris"), GameState.GetNetworkSessionSnapshot().bServerUsesIris);
        Event->SetNumberField(TEXT("ConnectedPlayers"), GameState.GetNetworkSessionSnapshot().ConnectedPlayerCount);
        Event->SetBoolField(TEXT("ServerFailureBit"), Observation->ServerFailureBit);
        Event->SetNumberField(TEXT("FailureNotices"), Notices(World));
        Event->SetNumberField(TEXT("ShortcutHints"), Hints(World));
        Event->SetNumberField(TEXT("OptionsWidgets"), Options(World));
        Event->SetBoolField(TEXT("OldWorldsClean"), OldWorldsClean());
        Event->SetStringField(TEXT("SelectedPath"), Observation->Path);
        Event->SetStringField(TEXT("LastError"), Observation->Reason);
        Event->SetStringField(TEXT("Screenshot"), Observation->PendingScreenshot);
        Events.Add(MakeShared<FJsonValueObject>(Event));
        const FString Name = FString::Printf(TEXT("%s-W%d.json"), State, Observation->Number);
        if (!SaveReport() || !SaveJson(Event, File(Role, Name))) { return Finish(TEXT("Could not save network observation")); }
        Observation->StableSince = 0.0;
        Observation->PendingScreenshot.Reset();
        UE_LOG(LogTemp, Display, TEXT("RPG_EXPERIENCE_NETWORK_PROOF OBSERVED Role=%s State=%s World=%d"),
            *Role, State, Observation->Number);
        return true;
    }
    bool StablePresentation(const TSharedRef<FExperienceProofWorld>& Observation, const TCHAR* State)
    {
        if (IsServer()) { return true; }
        const double Now = FPlatformTime::Seconds();
        if (Observation->StableSince == 0.0) { Observation->StableSince = Now; return false; }
        if (Now - Observation->StableSince < 1.0) { return false; }
        if (Observation->PendingScreenshot.IsEmpty())
        {
            Observation->PendingScreenshot = File(Role, FString::Printf(TEXT("%s-W%d.png"), State, Observation->Number));
            FScreenshotRequest::RequestScreenshot(Observation->PendingScreenshot, true, false, false, FIntRect(), true);
            return false;
        }
        return IFileManager::Get().FileSize(*Observation->PendingScreenshot) > 0;
    }
    bool Tick(float)
    {
        if (!Failure.IsEmpty()) { return Finish(Failure); }
        if (FPlatformTime::Seconds() - Started > TimeoutSeconds) { return Finish(TEXT("Network/travel observation timed out")); }
        if (Exists(TEXT("Control"), *(Role + TEXT("Stop.txt")))) { return Finish(); }
        UWorld* World = nullptr;
        if (GEngine)
        {
            for (const FWorldContext& Context : GEngine->GetWorldContexts())
            {
                UWorld* Candidate = Context.World();
                if (Candidate && Candidate->IsGameWorld() && !Candidate->bIsTearingDown
                    && Candidate->GetPackage()->GetName() == ProofMap) { World = Candidate; break; }
            }
        }
        if (!World || !World->GetGameInstance()) { return true; }
        ARPGGameStateBase* GameState = World->GetGameState<ARPGGameStateBase>();
        UNetDriver* Driver = World->GetNetDriver();
        if (!GameState || !Driver || !Driver->IsUsingIrisReplication()) { return true; }
        if (IsServer() ? World->GetNetMode() != NM_DedicatedServer : World->GetNetMode() != NM_Client)
        {
            return Finish(TEXT("Probe world has an unexpected network role"));
        }
        if (!IsServer() && (!Driver->ServerConnection || Driver->ServerConnection->GetConnectionState() != USOCK_Open
            || !GameState->GetNetworkSessionSnapshot().bServerUsesIris)) { return true; }
        URPGExperienceManagerComponent* Manager = GameState->GetExperienceManagerComponent();
        if (!Manager || !Manager->HasBegunPlay()) { return true; }
        const auto Observation = Observe(*World, *Manager);
        Observation->SawReady |= Manager->IsExperienceLoaded();
        Observation->ServerFailureBit = ServerBit(*Manager);
        Observation->Reason = Manager->GetLastError();
        Observation->Path = Manager->GetSelectedExperiencePath().ToString();
        if (Observation->Number == 1 && !IsServer())
        {
            APlayerController* PC = World->GetGameInstance()->GetFirstLocalPlayerController(World);
            if (!PC || !PC->GetHUD()) { return true; }
            if (!Observation->LocalController.IsValid()) { Observation->LocalController = PC; }
            if (!Observation->HUD.IsValid()) { Observation->HUD = PC->GetHUD(); }
            Observation->LocalPlayer = PC->GetLocalPlayer();
            Observation->ControllerName = PC->GetName();
            Observation->HUDName = PC->GetHUD()->GetName();
        }
        if (IsTravelRace() && Manager->GetLoadState() == ERPGExperienceLoadState::Failed)
        {
            return Finish(TEXT("Unexpected failure during travel proof: ") + Manager->GetLastError());
        }
        if (IsLoadingTravel() && Observation->Number == 1 && !Observation->LoadingRecorded)
        {
            if (Manager->IsExperienceLoaded()) { return Finish(TEXT("First world's feature completion was not deferred")); }
            if (Manager->GetLoadState() != ERPGExperienceLoadState::Loading || !Exists(Role, TEXT("DeferredStarted.txt"))) { return true; }
            if (Observation->ServerFailureBit || Observation->Path != ProofDefinition) { return Finish(TEXT("Held loading world has wrong selection/state")); }
            if (!AllFeaturesActive()) { return true; }
            FFileHelper::LoadFileToString(Observation->DeferredSession, *File(Role, TEXT("DeferredStarted.txt")));
            Observation->DeferredSession.TrimStartAndEndInline();
            if (!IsServer())
            {
                ULoadingScreenManager* Loading = World->GetGameInstance()->GetSubsystem<ULoadingScreenManager>();
                if (!HasOwnLoadingTask(*Manager) || !Loading || !Loading->GetLoadingScreenDisplayStatus()) { return true; }
            }
            if (!StablePresentation(Observation, TEXT("Loading"))) { return true; }
            Observation->LoadingRecorded = true;
            return Record(TEXT("Loading"), Observation, *GameState, *Driver);
        }
        if (IsLoadingTravel() && Observation->Number == 2 && Observation->ReadyRecorded && !Observation->SettledRecorded
            && Exists(Role, TEXT("DeferredCompleted.txt")))
        {
            if (!Manager->IsExperienceLoaded() || !OldWorldsClean() || !AllFeaturesActive()
                || Observation->ServerFailureBit || HasOwnLoadingTask(*Manager)) { return Finish(TEXT("Late completion damaged the new Experience")); }
            if (!IsServer() && (Notices(World) != 0 || Hints(World) != 1 || Options(World) != 1
                || !LocalControllerReady(*World, *Driver))) { return Finish(TEXT("Late completion changed the new client UI")); }
            if (!StablePresentation(Observation, TEXT("Settled"))) { return true; }
            Observation->SettledRecorded = true;
            return Record(TEXT("Settled"), Observation, *GameState, *Driver);
        }

        if (IsServer() && Scenario == TEXT("PostReadyFailure") && !InjectedServerFailure
            && Manager->IsExperienceLoaded() && Exists(TEXT("Client"), TEXT("Ready-W1.json")))
        {
            // Fault injection is server-only. Clients only observe real replicated properties.
            // Use the existing RepNotify -> Fail path; no new production failure API is needed.
            FBoolProperty* Bit = FindFProperty<FBoolProperty>(Manager->GetClass(), TEXT("bServerExperienceFailed"));
            UFunction* Notify = Manager->FindFunction(TEXT("OnRep_ServerExperienceFailed"));
            if (!Bit || !Notify) { return Finish(TEXT("Missing existing failure replication fields")); }
            InjectedServerFailure = true;
            Bit->SetPropertyValue_InContainer(Manager, true);
            Manager->ProcessEvent(Notify, nullptr);
            Observation->StableSince = 0.0;
            Observation->PendingScreenshot.Reset();
            return true;
        }
        const bool Failed = Manager->GetLoadState() == ERPGExperienceLoadState::Failed;
        if (Failed && !Observation->FailureRecorded)
        {
            if (!Observation->ServerFailureBit) { return Finish(TEXT("Failed world did not receive the server failure bit")); }
            if (Manager->IsExperienceLoaded() || HasOwnLoadingTask(*Manager)) { return Finish(TEXT("Failure retained ready/loading ownership")); }
            if (Scenario != TEXT("PostReadyFailure") && Manager->GetSelectedExperiencePath().IsValid())
            {
                return Finish(TEXT("Selection failure unexpectedly replicated a selected path"));
            }
            if (Scenario == TEXT("PostReadyFailure") && Observation->Path != ProofDefinition)
            {
                return Finish(TEXT("Post-ready failure lost the previously replicated path"));
            }
            if (!IsServer())
            {
                if (Observation->Reason != TEXT("The server could not prepare this game."))
                {
                    return Finish(TEXT("Client received raw server diagnostics or did not process server failure"));
                }
                if ((Role == TEXT("LateClient") || Scenario != TEXT("PostReadyFailure")) && Observation->SawReady)
                {
                    return Finish(TEXT("Client became Ready after an initial server failure"));
                }
                ULoadingScreenManager* Loading = World->GetGameInstance()->GetSubsystem<ULoadingScreenManager>();
                if (!Loading || Loading->GetLoadingScreenDisplayStatus()) { return true; }
                if (Notices(World) > 1 || Hints(World) != 0) { return Finish(TEXT("Failure duplicated notices or retained feature UI")); }
                if (Notices(World, true) != 1) { return true; }
            }
            else if (Notices(World) != 0 || Hints(World) != 0)
            {
                return Finish(TEXT("Dedicated server created client UI"));
            }
            if (!StablePresentation(Observation, TEXT("Failed"))) { return true; }
            Observation->FailureRecorded = true;
            return Record(TEXT("Failed"), Observation, *GameState, *Driver);
        }
        if (Manager->IsExperienceLoaded() && !Observation->ReadyRecorded)
        {
            if (Observation->Path != ProofDefinition || Observation->ServerFailureBit || HasOwnLoadingTask(*Manager))
            {
                return Finish(TEXT("Ready world has stale selection, failure or loading ownership"));
            }
            if (IsLoadingTravel() && Observation->Number == 1) { return Finish(TEXT("Deferred world became Ready before travel")); }
            if (UsesSeamless() && Observation->Number > 1)
            {
                if (Worlds[0]->NetDriver.Get() != Driver || !ConnectionsPreserved(*Driver))
                {
                    return Finish(FString::Printf(TEXT("Seamless travel replaced transport: Driver=%d Connections=%d"),
                        Worlds[0]->NetDriver.Get() == Driver, ConnectionsPreserved(*Driver)));
                }
                // UE 5.8 GameModeBase::HandleSeamlessTravelPlayer always creates a replacement PC.
                // Wait for its real connection/local-player/HUD binding; transport must remain identical.
                if (!LocalControllerReady(*World, *Driver)) { return true; }
            }
            if (!OldWorldsClean()) { return Finish(TEXT("Travel retained old-world UI or manager ownership")); }
            if (!IsServer())
            {
                ULoadingScreenManager* Loading = World->GetGameInstance()->GetSubsystem<ULoadingScreenManager>();
                if (!Loading || Loading->GetLoadingScreenDisplayStatus()) { return true; }
                if (Notices(World) != 0 || Hints(World) > 1 || Options(World) > 1) { return Finish(FString::Printf(TEXT("Travel UI differs: Notices=%d Hints=%d Options=%d"), Notices(World), Hints(World), Options(World))); }
                if (Hints(World) != 1 || Options(World) != 1 || !LocalControllerReady(*World, *Driver)) { return true; }
                if ((Scenario == TEXT("FailureToReadyTravel") || IsTravelRace()) && Observation->Number > 1
                    && GameState->GetNetworkSessionSnapshot().ConnectedPlayerCount != 2) { return true; }
            }
            if (!StablePresentation(Observation, TEXT("Ready"))) { return true; }
            Observation->ReadyRecorded = true;
            return Record(TEXT("Ready"), Observation, *GameState, *Driver);
        }
        if (IsServer() && IsTravelRace() && Observation->Number == 1 && !Travel1Issued
            && (IsLoadingTravel() ? Observation->LoadingRecorded : Observation->ReadyRecorded)
            && Exists(TEXT("Control"), TEXT("Travel1.txt")))
        {
            Travel1Issued = true;
            if (UsesSeamless())
            {
                for (UNetConnection* Connection : Driver->ClientConnections) { SeamlessConnections.Add(Connection); }
            }
            const FString URL = FString(ProofMap) + TEXT("?RPGExperience=") + ProofDefinition
                + TEXT("?RPGExperienceProofHold=0") + (UsesSeamless() ? TEXT("?SeamlessTravel") : TEXT("?NoSeamlessTravel"));
            if (!World->ServerTravel(URL, true)) { return Finish(TEXT("ServerTravel rejected advanced travel proof")); }
            SaveReport();
        }
        if (IsServer() && Scenario == TEXT("FailureToReadyTravel"))
        {
            const bool First = Observation->Number == 1 && Observation->FailureRecorded
                && !Travel1Issued && Exists(TEXT("Control"), TEXT("Travel1.txt"));
            const bool Second = Observation->Number == 2 && Observation->ReadyRecorded
                && !Travel2Issued && Exists(TEXT("Control"), TEXT("Travel2.txt"));
            if (First || Second)
            {
                if (First) { Travel1Issued = true; } else { Travel2Issued = true; }
                const FString URL = FString(ProofMap) + TEXT("?RPGExperience=") + ProofDefinition;
                if (!World->ServerTravel(URL, true)) { return Finish(TEXT("ServerTravel rejected development map reload")); }
                SaveReport();
            }
        }
        return true;
    }
};

FAutoConsoleCommand ExperienceNetworkProofCommand(TEXT("RPG.Experience.NetworkProof"),
    TEXT("Opt-in real network/travel proof; requires -RPGExperienceNetworkProof and Saved output."),
    FConsoleCommandDelegate::CreateLambda([]
    {
        if (!FParse::Param(FCommandLine::Get(), TEXT("RPGExperienceNetworkProof"))) { return; }
        const TSharedRef<FExperienceNetworkProof> Proof = MakeShared<FExperienceNetworkProof>();
        FParse::Value(FCommandLine::Get(), TEXT("RPGExperienceNetworkProofOutput="), Proof->Output);
        FParse::Value(FCommandLine::Get(), TEXT("RPGExperienceNetworkProofRole="), Proof->Role);
        FParse::Value(FCommandLine::Get(), TEXT("RPGExperienceNetworkProofCase="), Proof->Scenario);
        FParse::Value(FCommandLine::Get(), TEXT("RPGExperienceNetworkProofTimeout="), Proof->TimeoutSeconds);
        Proof->TimeoutSeconds = FMath::IsFinite(Proof->TimeoutSeconds) ? FMath::Clamp(Proof->TimeoutSeconds, 30.0, 900.0) : 240.0;
        if (!FPaths::IsUnderDirectory(Proof->Output, FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved")))
            || (Proof->Role != TEXT("Server") && Proof->Role != TEXT("Client") && Proof->Role != TEXT("LateClient"))
            || (Proof->Scenario != TEXT("EarlySelectionFailure") && Proof->Scenario != TEXT("PostReadyFailure")
                && Proof->Scenario != TEXT("FailureToReadyTravel") && !Proof->IsTravelRace()))
        {
            UE_LOG(LogTemp, Error, TEXT("RPG_EXPERIENCE_NETWORK_PROOF invalid arguments or output outside project Saved"));
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }
        IFileManager::Get().MakeDirectory(*Proof->File(Proof->Role, TEXT("")), true);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
            [Proof](float Delta) { return Proof->Tick(Delta); }));
    }));
}
#endif
