#include "Riding/RPGRidingCollectionSubsystem.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
struct FRPGRidingLobbyCommandTestAccess
{
    static void Bind(URPGRidingCollectionSubsystem& S, TSharedPtr<FRPGRidingCommandCoordinator> C) { S.Commands = MoveTemp(C); }
    static void Tick(URPGRidingCollectionSubsystem& S) { S.TickCollection(0); }
    static bool HasReader(const URPGRidingCollectionSubsystem& S) { return S.Reader.IsValid(); }
    static void LateTicket(URPGDungeonSessionSubsystem& F, FGuid Character)
    {
        F.SelectedCharacterId = Character.ToString(EGuidFormats::DigitsWithHyphensLower);
        F.CurrentDungeonSession.DungeonSessionId = TEXT("private-ticket-session");
        F.PendingOperation = URPGDungeonSessionSubsystem::EPendingOperation::Connect;
        F.HandleJoinTicketIssued(F.CurrentDungeonSession.DungeonSessionId, F.SelectedCharacterId, TEXT("unused-private-ticket"), TEXT("2100-01-01T00:00:00Z"), true);
    }
};
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    struct FFake : IRPGRidingOwnerTransport
    {
        FRPGRidingCommandContext* C; FCompletion Done; int32 Writes = 0;
        explicit FFake(FRPGRidingCommandContext& In) : C(&In) {}
        virtual bool GetContext(FString& E, FString& A) const override { E = C->Scope.Endpoint; A = C->Scope.Account; return true; }
        virtual bool HasCredential() const override { return C->bEligible; }
        virtual bool CanSend() const override { return C->bEligible && !Done; }
        virtual bool ReadCollection(FGuid, FCompletion) override { return false; }
        virtual bool SendIntent(const FRPGRidingPendingIntent&, FCompletion F) override { ++Writes; Done = MoveTemp(F); return true; }
        virtual void Cancel() override { Done = {}; }
        void Reply(int32 Code, FString Body) { auto F = MoveTemp(Done); if (F) F({true, true, Code, MoveTemp(Body), 0}); }
    };
    struct FFixture
    {
        TStrongObjectPtr<UGameInstance> GI{NewObject<UGameInstance>(GEngine)};
        UWorld* World = nullptr; UGameViewportClient* Viewport = nullptr; ULocalPlayer* Player = nullptr;
        APlayerController* PC = nullptr; URPGRidingCollectionSubsystem* Riding = nullptr; URPGDungeonSessionSubsystem* Flow = nullptr;
        FRPGRidingCommandContext C; TSharedPtr<FFake> T; TSharedPtr<FRPGRidingCommandCoordinator> Commands;
        FRPGRidingPendingIntent Intent; FString Root;
        bool Init()
        {
            FString Base; if (!FParse::Value(FCommandLine::Get(), TEXT("RidingLobbyRoot="), Base) || Base.IsEmpty()) return false;
            Root = Base / FGuid::NewGuid().ToString(EGuidFormats::Digits);
            GI->InitializeForMinimalNetRPC(FName(*(TEXT("/Temp/RidingLobby_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
            World = GI->GetWorld(); if (!World) return false;
            Viewport = NewObject<UGameViewportClient>(GEngine); GI->GetWorldContext()->GameViewport = Viewport;
            Viewport->Init(*GI->GetWorldContext(), GI.Get(), false);
            FString Error; Player = GI->CreateLocalPlayer(0, Error, false); if (!Player) return false;
            PC = World->SpawnActor<APlayerController>(); if (!PC) return false; PC->SetPlayer(Player); World->AddController(PC);
            Riding = Player->GetSubsystem<URPGRidingCollectionSubsystem>(); Flow = GI->GetSubsystem<URPGDungeonSessionSubsystem>();
            if (!Riding || !Flow) return false;
            if (!FRPGRidingIntentScope::Create(TEXT("http://127.0.0.1:1/api"), TEXT("76561198000000001"), FGuid::NewGuid(), C.Scope)) return false;
            C.bEligible = true; T = MakeShared<FFake>(C);
            if (!RPGRidingCommand::Preferences(C.Scope, FGuid::NewGuid(), {}, Intent)) return false;
            Rebind(); return true;
        }
        void Rebind()
        {
            if (Commands) Commands->Shutdown();
            Commands = MakeShared<FRPGRidingCommandCoordinator>(T.ToSharedRef(), [this] { return C; }, Root);
            FRPGRidingLobbyCommandTestAccess::Bind(*Riding, Commands);
        }
        ~FFixture()
        {
            GI->Shutdown(); if (Viewport) Viewport->DetachViewportClient();
            if (World) { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
        }
    };
    FString Proof(const FRPGRidingPendingIntent& I)
    {
        TSharedPtr<FJsonObject> Command; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(I.Body), Command);
        auto P = MakeShared<FJsonObject>();
        for (const auto* K : {TEXT("selectedMountId"), TEXT("favoriteMountIds"), TEXT("shortcut")}) P->SetField(K, Command->TryGetField(K));
        auto R = MakeShared<FJsonObject>(); R->SetStringField(TEXT("requestId"), I.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
        R->SetStringField(TEXT("characterId"), I.Scope.Character.ToString(EGuidFormats::DigitsWithHyphensLower));
        R->SetStringField(TEXT("rosterId"), FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
        R->SetStringField(TEXT("operation"), TEXT("Riding.Preferences")); R->SetStringField(TEXT("outcome"), TEXT("PreferencesSaved"));
        R->SetStringField(TEXT("preferencesRevision"), TEXT("1")); R->SetStringField(TEXT("committedAtUtc"), TEXT("2026-11-14T00:00:00Z")); R->SetObjectField(TEXT("preferences"), P);
        auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("outcome"), TEXT("PreferencesSaved")); E->SetObjectField(TEXT("receipt"), R);
        FString S; FJsonSerializer::Serialize(E, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&S)); return S;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingLobbyScreenLifetime, "ProjectRPG.Riding.LobbyCommand.ScreenLifetime", Flags)
bool FRidingLobbyScreenLifetime::RunTest(const FString&)
{
    FFixture F; if (!TestTrue(TEXT("Actual LocalPlayer fixture"), F.Init())) return false;
    TestTrue(TEXT("Submit pending"), F.Riding->GetCommands()->Submit(F.Intent)); F.Riding->ReleaseReader();
    TestTrue(TEXT("Closing read screen preserves command owner"), F.Riding->GetCommands() == F.Commands && F.Riding->BlocksTransitions());
    FRPGRidingLobbyCommandTestAccess::Tick(*F.Riding); TestEqual(TEXT("Ticker never resends"), F.T->Writes, 1);
    F.C.bEligible = false; TestTrue(TEXT("Auth loss keeps transition barrier"), F.Riding->BlocksTransitions());
    TestNull(TEXT("Auth loss hides command"), F.Commands->GetPending());
    F.Riding->Deinitialize(); TestNull(TEXT("Owner cannot reopen after deinitialize"), F.Riding->GetCommands().Get());
    FRPGRidingPendingIntent Saved; TestTrue(TEXT("Deinitialize preserves original disk command"), FRPGRidingIntentJournal::Load(F.Root, F.C.Scope, Saved) == ERPGRidingJournalRead::Ready && Saved == F.Intent);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingLobbyRestore, "ProjectRPG.Riding.LobbyCommand.RestoreBeforeScreen", Flags)
bool FRidingLobbyRestore::RunTest(const FString&)
{
    FFixture F; if (!TestTrue(TEXT("Actual LocalPlayer fixture"), F.Init())) return false;
    TestTrue(TEXT("Existing original intent"), FRPGRidingIntentJournal::Create(F.Root, F.Intent)); F.Rebind();
    TestFalse(TEXT("Screen never opened"), FRPGRidingLobbyCommandTestAccess::HasReader(*F.Riding));
    TestTrue(TEXT("Guard restores before any screen"), F.Riding->BlocksTransitions());
    TestTrue(TEXT("Exact original restored"), F.Commands->GetPending() && *F.Commands->GetPending() == F.Intent);
    TestEqual(TEXT("Restore sends nothing"), F.T->Writes, 0); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingLobbyFlowGuards, "ProjectRPG.Riding.LobbyCommand.NativeFlowGuards", Flags)
bool FRidingLobbyFlowGuards::RunTest(const FString&)
{
    FFixture F; if (!TestTrue(TEXT("Actual LocalPlayer fixture"), F.Init())) return false;
    F.Riding->GetCommands()->Submit(F.Intent);
    auto Check = [&](bool Sent) { TestFalse(TEXT("Transition rejected"), Sent); TestEqual(TEXT("Riding guard reached before other validation"), F.Flow->GetLastErrorCode(), FString(TEXT("riding_command_unresolved"))); };
    Check(F.Flow->SelectCharacter(FGuid::NewGuid().ToString())); Check(F.Flow->CreateDungeonSession(TEXT("d"), TEXT("n")));
    Check(F.Flow->JoinDungeonSession(TEXT("s"))); Check(F.Flow->ResumeDungeonSession()); Check(F.Flow->SetDeparture(false)); Check(F.Flow->ConnectToDungeon(F.PC, TEXT("127.0.0.1:7777")));
    FRPGRidingLobbyCommandTestAccess::LateTicket(*F.Flow, F.C.Scope.Character);
    TestTrue(TEXT("Late ticket cannot travel"), F.Flow->GetFlowState() == ERPGDungeonClientFlowState::Error && F.Flow->GetLastErrorCode() == TEXT("riding_command_unresolved"));
    const auto Character = F.Flow->GetSelectedCharacterId(); F.Flow->ResetDungeonFlow(false);
    TestEqual(TEXT("Reset cannot clear pinned identity"), F.Flow->GetSelectedCharacterId(), Character);
    TStrongObjectPtr<URPGOnlineLobbyWidget> Widget(CreateWidget<URPGOnlineLobbyWidget>(F.PC, URPGOnlineLobbyWidget::StaticClass()));
    TestTrue(TEXT("Frontend/tutorial busy predicate includes riding"), Widget.IsValid() && Widget->IsFrontendRequestInFlight());
    TestEqual(TEXT("Guards never resend"), F.T->Writes, 1); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingLobbyFreshBarrier, "ProjectRPG.Riding.LobbyCommand.FreshReadBarrier", Flags)
bool FRidingLobbyFreshBarrier::RunTest(const FString&)
{
    FFixture F; if (!TestTrue(TEXT("Actual LocalPlayer fixture"), F.Init())) return false;
    F.Riding->GetCommands()->Submit(F.Intent); F.T->Reply(200, Proof(F.Intent));
    TestTrue(TEXT("Validated receipt still blocks until current reads"), F.Commands->NeedsFreshReads() && F.Riding->BlocksTransitions());
    TestFalse(TEXT("Wrong request cannot release"), F.Commands->AcknowledgeFreshReads(FGuid::NewGuid()));
    // The trusted consumer hook is tested here; actual fresh asset HTTP reads are a later integration slice.
    TestTrue(TEXT("Matching fresh-read acknowledgement"), F.Commands->AcknowledgeFreshReads(F.Intent.RequestId));
    TestFalse(TEXT("Barrier released"), F.Riding->BlocksTransitions());
    F.Flow->SelectCharacter(FGuid::NewGuid().ToString());
    TestTrue(TEXT("Native path proceeds beyond riding guard"), F.Flow->GetLastErrorCode() != TEXT("riding_command_unresolved")); return true;
}
#endif
