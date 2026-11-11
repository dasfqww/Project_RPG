#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Narrative/RPGNarrativeCoordinator.h"
#include "Narrative/RPGNarrativeTransport.h"
#include "Narrative/RPGQuestManager.h"
#include "Narrative/RPGScenarioManager.h"
#include "Narrative/RPGDialogueManager.h"
#include "Manager/HttpWebManager.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "UI/Narrative/RPGNarrativeDialogueWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "Actor/Narrative/RPGNarrativeNPC.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/DefaultPawn.h"
#include "Components/SphereComponent.h"
#include "Manager/UIManager.h"
#include "Component/UI/RPGSkillWindowComponent.h"
#include "UI/RPGWindowInputState.h"
#include "Slate/SGameLayerManager.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SVirtualWindow.h"

struct FRPGNarrativeTestAccess
{
    static void QueueDungeonRefresh(URPGNarrativeCoordinator* Owner) { Owner->ScheduleDungeonSync(true, true); }
    static void PumpDungeon(URPGNarrativeCoordinator* Owner)
    {
        FTSTicker::GetCoreTicker().RemoveTicker(Owner->DungeonSyncTick);
        Owner->TickDungeonSync(0.f);
    }
    static void Attach(URPGNarrativeCoordinator* Owner, TSharedPtr<IRPGNarrativeTransport> Transport)
    {
        Owner->Transport = MoveTemp(Transport); Owner->BindManagers();
    }
    static void Login(UHttpWebManager* Web, const FString& SteamId = TEXT("76561198000000001"))
    {
        Web->ApiUrl = TEXT("http://127.0.0.1:5000/api"); Web->BackendServiceToken.Reset();
        Web->PlayerAuth.Reset();
        const double Now = FPlatformTime::Seconds();
        Web->PlayerAuth.AcceptLogin(TEXT("narrative-test-token"), SteamId,
            TEXT("2200-01-01T00:10:00Z"), TEXT("2200-01-01T00:00:00Z"), Now, Now);
    }
    static void Logout(UHttpWebManager* Web) { Web->PlayerAuth.Reset(); }
    static void ConfigureNPC(ARPGNarrativeNPC* NPC, TArray<FString> Entries) { NPC->DialogueEntryIds = MoveTemp(Entries); }
    static void TickNPC(URPGNarrativeDialogueWidget* Panel) { Panel->NativeTick(FGeometry(), 0.f); }
    static bool BackNPC(URPGNarrativeDialogueWidget* Panel)
    { return Panel->NativeOnPreviewKeyDown(FGeometry(), FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0)).IsEventHandled(); }
    static void NPCAction(URPGNarrativeDialogueWidget* Panel, const FString& Node) { Panel->Act(Node, FString()); }
};

namespace RPGNarrativeTests
{
    const FGuid Character(1, 2, 3, 4);
    FString Snapshot(const int32 Revision, const FGuid InCharacter = Character)
    {
        const FString Prefix = FString::Printf(TEXT("{\"characterId\":\"%s\",\"catalogVersion\":1,\"schemaVersion\":1,\"revision\":\"%d\","),
            *InCharacter.ToString(EGuidFormats::DigitsWithHyphensLower), Revision);
        const FString Quest = Revision == 0 ? TEXT("[]") : FString::Printf(
            TEXT("[{\"questId\":\"DEV.Introduction\",\"definitionVersion\":1,\"status\":\"%s\",\"completedObjectives\":%s,")
            TEXT("\"title\":\"Test quest\",\"objectives\":[{\"id\":\"contact\",\"text\":\"Contact briefing\",\"completed\":%s},")
            TEXT("{\"id\":\"preparation\",\"text\":\"Preparation briefing\",\"completed\":%s}]}]"),
            Revision == 4 ? TEXT("Completed") : Revision == 3 ? TEXT("ReadyToReport") : TEXT("Active"),
            Revision >= 3 ? TEXT("[\"contact\",\"preparation\"]") : Revision == 2 ? TEXT("[\"contact\"]") : TEXT("[]"),
            Revision >= 2 ? TEXT("true") : TEXT("false"), Revision >= 3 ? TEXT("true") : TEXT("false"));
        auto Node = [](const TCHAR* Id, const TCHAR* Choice)
        {
            return FString::Printf(TEXT("{\"id\":\"DEV.%s\",\"speaker\":\"Contact\",\"text\":\"Test dialogue\",\"choices\":[{\"id\":\"%s\",\"text\":\"Confirm\"}]}"), Id, Choice);
        };
        FString Nodes;
        if (Revision == 0) Nodes = Node(TEXT("Contact.Offer"), TEXT("accept"));
        else if (Revision == 1) Nodes = Node(TEXT("Contact.Explain"), TEXT("confirm")) + TEXT(",") + Node(TEXT("Instructor.Explain"), TEXT("confirm"));
        else if (Revision == 2) Nodes = Node(TEXT("Instructor.Explain"), TEXT("confirm"));
        else if (Revision == 3) Nodes = Node(TEXT("Contact.Report"), TEXT("report"));
        return Prefix + TEXT("\"quests\":") + Quest + TEXT(",\"completedChapters\":")
            + (Revision == 4 ? TEXT("[\"DEV.Prologue\"]") : TEXT("[]"))
            + TEXT(",\"storyUnlocks\":") + (Revision == 4 ? TEXT("[\"DEV.Story.Next\"]") : TEXT("[]"))
            + TEXT(",\"availableDialogues\":[") + Nodes + TEXT("]}");
    }
    struct FTransport final : IRPGNarrativeTransport
    {
        FString Account = TEXT("76561198000000001");
        FString LastBody;
        int32 Sends = 0;
        bool bDungeonSend = false;
        TFunction<void(FRPGNarrativeHttpResult)> Pending;
        bool GetContext(FString& Endpoint, FString& SteamId) const override
        {
            Endpoint = TEXT("http://127.0.0.1:5000/api"); SteamId = Account; return !Account.IsEmpty();
        }
        bool Send(FGuid, const FString& Body, TFunction<void(FRPGNarrativeHttpResult)> Completion) override
        {
            bDungeonSend = false; ++Sends; LastBody = Body; Pending = MoveTemp(Completion); return true;
        }
        bool SendDungeon(FGuid CharacterId, const FString& Body, TFunction<void(FRPGNarrativeHttpResult)> Completion) override
        { Send(CharacterId, Body, MoveTemp(Completion)); bDungeonSend = true; return true; }
        void Cancel() override { Pending = nullptr; }
        bool Complete(const FString& Body, const int32 Status = 200)
        {
            auto Callback = MoveTemp(Pending);
            if (!Callback) return false;
            FRPGNarrativeHttpResult Result; Result.bCurrentPlayerContext = true;
            Result.bTransportSuccessful = Status != 0; Result.StatusCode = Status; Result.Body = Body;
            Callback(MoveTemp(Result)); return true;
        }
    };
    struct FGameFixture
    {
        TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>(GEngine)};
        UGameViewportClient* Viewport = nullptr;
        ULocalPlayer* Player = nullptr;
        UWorld* World = nullptr;
        TSharedPtr<SVirtualWindow> VirtualWindow;
        bool Init(bool bViewport = false)
        {
            Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/Narrative_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
            World = Instance->GetWorld();
            if (!World) return false;
            Viewport = NewObject<UGameViewportClient>(GEngine);
            Instance->GetWorldContext()->GameViewport = Viewport;
            Viewport->Init(*Instance->GetWorldContext(), Instance.Get(), false);
            if (bViewport)
            {
                VirtualWindow = SNew(SVirtualWindow).Size(FVector2D(1280, 720));
                auto Overlay = SNew(SOverlay); auto Layers = SNew(SGameLayerManager);
                Overlay->AddSlot()[Layers]; VirtualWindow->SetContent(Overlay);
                FSlateApplication::Get().RegisterVirtualWindow(VirtualWindow.ToSharedRef());
PRAGMA_DISABLE_DEPRECATION_WARNINGS
                Viewport->SetViewportOverlayWidget(VirtualWindow, Overlay);
                Viewport->SetGameLayerManager(Layers);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
            }
            FString Error; Player = Instance->CreateLocalPlayer(0, Error, false);
            auto* Web = Instance->GetSubsystem<UHttpWebManager>();
            if (!Player || !Web) return false;
            FRPGNarrativeTestAccess::Login(Web);
            auto* Dungeon = Instance->GetSubsystem<URPGDungeonSessionSubsystem>();
            if (!Dungeon || !Dungeon->SelectCharacter(Character.ToString(EGuidFormats::DigitsWithHyphensLower))) return false;
            return Player->GetSubsystem<URPGNarrativeCoordinator>() && Player->GetSubsystem<URPGDialogueManager>()
                && Instance->GetSubsystem<URPGQuestManager>() && Instance->GetSubsystem<URPGScenarioManager>();
        }
        ~FGameFixture()
        {
            if (!World) return;
            if (Viewport) Viewport->RemoveAllViewportWidgets();
            if (VirtualWindow && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterVirtualWindow(VirtualWindow.ToSharedRef());
            while (Instance->GetLocalPlayers().Num()) Instance->RemoveLocalPlayer(Instance->GetLocalPlayers()[0]);
            Instance->Shutdown();
            if (Viewport) Viewport->DetachViewportClient();
            GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
        }
    };
}

namespace RPGNarrativeTests
{
    FString InformationSnapshot(bool bBrokenLink = false, int32 Revision = 0)
    {
        TSharedPtr<FJsonObject> Root, Information;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Snapshot(Revision)), Root);
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(TEXT(R"json({"nodes":[
          {"id":"INFO.Start","speaker":"Guide","text":"Choose a topic.","informational":true,"choices":[{"id":"ask","text":"Ask","nextDialogueId":"INFO.Topic"},{"id":"close","text":"Close"}]},
          {"id":"INFO.Topic","speaker":"Guide","text":"Character progress.","informational":true,"entryPoint":false,"choices":[{"id":"more","text":"More","nextDialogueId":"INFO.Detail"},{"id":"back","text":"Back","nextDialogueId":"INFO.Start"}]},
          {"id":"INFO.Detail","speaker":"Guide","text":"Reading does not complete objectives.","informational":true,"entryPoint":false,"choices":[{"id":"back","text":"Back","nextDialogueId":"INFO.Topic"},{"id":"close","text":"Close"}]}
        ]})json"))), Information);
        auto Nodes = Root->GetArrayField(TEXT("availableDialogues"));
        auto Extra = Information->GetArrayField(TEXT("nodes"));
        if (bBrokenLink) Extra[0]->AsObject()->GetArrayField(TEXT("choices"))[0]->AsObject()->SetStringField(TEXT("nextDialogueId"), TEXT("missing"));
        Nodes.Append(Extra); Root->SetArrayField(TEXT("availableDialogues"), Nodes);
        FString Body; FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Body)); return Body;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeInformationTest, "ProjectRPG.Narrative.InformationDialogue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGNarrativeInformationTest::RunTest(const FString&)
{
    using namespace RPGNarrativeTests;
    FRPGNarrativeSnapshot Parsed;
    TestFalse(TEXT("Dangling information links rejected"), FRPGNarrativeCodec::Decode(InformationSnapshot(true), Character, Parsed));
    TestTrue(TEXT("Information graph decodes"), FRPGNarrativeCodec::Decode(InformationSnapshot(), Character, Parsed));
    FGameFixture Fixture;
    if (!TestTrue(TEXT("Information fixture"), Fixture.Init())) return false;
#if UE_SUPPORT_FOR_ACTOR_TICK_DISABLE
    Fixture.World->EnableActorTickAndUserCallbacks(true);
#endif
    Fixture.World->InitializeActorsForPlay(FURL());
    auto* Controller = Fixture.World->SpawnActor<APlayerController>();
    if (!Controller) return false;
    Controller->SetPlayer(Fixture.Player); Fixture.World->AddController(Controller);
    auto* Flow = Fixture.Player->GetSubsystem<URPGNarrativeCoordinator>();
    auto Transport = MakeShared<FTransport>(); FRPGNarrativeTestAccess::Attach(Flow, Transport); Flow->SelectCharacter(Character);
    TStrongObjectPtr<URPGNarrativeDialogueWidget> Panel(CreateWidget<URPGNarrativeDialogueWidget>(Controller));
    Panel->SetOwningLocalPlayer(Fixture.Player); Panel->OpenForCharacter(Character);
    const auto Slate = Panel->TakeWidget(); Transport->Complete(InformationSnapshot());
    auto Click = [&](const FString& Node, const FString& Choice)
    {
        auto* List = Cast<UVerticalBox>(Panel->GetWidgetFromName(TEXT("NarrativeActions")));
        for (auto* Child : List->GetAllChildren())
            if (auto* Row = Cast<URPGNarrativeActionWidget>(Child))
                if (Row->GetDialogueId() == Node && Row->GetChoiceId() == Choice)
                    if (auto* Button = Cast<UButton>(Row->GetWidgetFromName(TEXT("NarrativeActionButton"))))
                    { Button->OnClicked.Broadcast(); return true; }
        return false;
    };
    TestEqual(TEXT("Only entry nodes shown"), Cast<UVerticalBox>(Panel->GetWidgetFromName(TEXT("NarrativeActions")))->GetChildrenCount(), 2);
    TestFalse(TEXT("Hidden continuation is not a public entry"), Flow->BeginDialogue(TEXT("INFO.Topic")));
    TestTrue(TEXT("Information entry button"), Click(TEXT("INFO.Start"), FString()));
    TestTrue(TEXT("Question button"), Click(TEXT("INFO.Start"), TEXT("ask")));
    TestTrue(TEXT("More button"), Click(TEXT("INFO.Topic"), TEXT("more")));
    TestTrue(TEXT("Back to question"), Click(TEXT("INFO.Detail"), TEXT("back")));
    TestTrue(TEXT("Back to topics"), Click(TEXT("INFO.Topic"), TEXT("back")));
    TestTrue(TEXT("Close conversation"), Click(TEXT("INFO.Start"), TEXT("close")));
    FRPGDialogueNode Active;
    TestFalse(TEXT("Close choice clears local conversation"), Flow->GetDialogueManager()->GetActiveDialogue(Active));
    Click(TEXT("INFO.Start"), FString()); Click(TEXT("INFO.Start"), TEXT("ask")); Panel->CloseScreen();
    TestFalse(TEXT("Closing after navigation clears the owned continuation"), Flow->GetDialogueManager()->GetActiveDialogue(Active));
    TestEqual(TEXT("Read-only navigation sent only the initial GET"), Transport->Sends, 1);
    TestFalse(TEXT("No pending command from information"), Flow->HasPendingChoice());
    TestTrue(TEXT("No quest progress invented"), Flow->GetQuestManager()->GetQuests(Character).IsEmpty());
    Panel->OpenForCharacter(Character); Transport->Complete(InformationSnapshot());
    Click(TEXT("DEV.Contact.Offer"), FString()); Click(TEXT("DEV.Contact.Offer"), TEXT("accept"));
    TestTrue(TEXT("Progression choice still sends a command"), Flow->HasPendingChoice() && Flow->IsBusy());
    Transport->Complete(Snapshot(1)); Panel->CloseScreen(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeCodecTest, "ProjectRPG.Narrative.Codec",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGNarrativeCodecTest::RunTest(const FString&)
{
    using namespace RPGNarrativeTests;
    FRPGNarrativeSnapshot Parsed;
    TestTrue(TEXT("Backend contract decodes"), FRPGNarrativeCodec::Decode(Snapshot(4), Character, Parsed));
    TestEqual(TEXT("Completed status"), Parsed.Quests[0].Status, ERPGQuestStatus::Completed);
    TestEqual(TEXT("Authored quest title"), Parsed.Quests[0].Title.ToString(), FString(TEXT("Test quest")));
    TestTrue(TEXT("Authored goals carry confirmed completion"), Parsed.Quests[0].Objectives.Num() == 2 && Parsed.Quests[0].Objectives[0].bCompleted);
    TestFalse(TEXT("Inconsistent journal flags rejected"), FRPGNarrativeCodec::Decode(
        Snapshot(4).Replace(TEXT("\"completed\":true"), TEXT("\"completed\":false")), Character, Parsed));
    TSharedPtr<FJsonObject> LegacyObject;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Snapshot(4)), LegacyObject);
    auto LegacyQuest = LegacyObject->GetArrayField(TEXT("quests"))[0]->AsObject();
    LegacyQuest->RemoveField(TEXT("title")); LegacyQuest->RemoveField(TEXT("objectives"));
    FString LegacyBody; FJsonSerializer::Serialize(LegacyObject.ToSharedRef(), TJsonWriterFactory<>::Create(&LegacyBody));
    TestTrue(TEXT("Older snapshot without presentation still decodes"), FRPGNarrativeCodec::Decode(LegacyBody, Character, Parsed));
    TestTrue(TEXT("Legacy metadata has no invented ID label"), Parsed.Quests[0].Title.IsEmpty() && Parsed.Quests[0].Objectives.IsEmpty());
    TestFalse(TEXT("Foreign character rejected"), FRPGNarrativeCodec::Decode(Snapshot(4), FGuid::NewGuid(), Parsed));
    TestFalse(TEXT("Fractional version rejected"), FRPGNarrativeCodec::Decode(Snapshot(4).Replace(TEXT("\"catalogVersion\":1"), TEXT("\"catalogVersion\":1.5")), Character, Parsed));
    TestFalse(TEXT("Revision overflow rejected"), FRPGNarrativeCodec::Decode(Snapshot(4).Replace(TEXT("\"revision\":\"4\""), TEXT("\"revision\":\"9223372036854775808\"")), Character, Parsed));
    TestFalse(TEXT("Unknown quest status rejected"), FRPGNarrativeCodec::Decode(Snapshot(4).Replace(TEXT("Completed\""), TEXT("Skipped\"")), Character, Parsed));
    TestFalse(TEXT("Duplicate objective rejected"), FRPGNarrativeCodec::Decode(Snapshot(4).Replace(TEXT("\"contact\",\"preparation\""), TEXT("\"contact\",\"contact\"")), Character, Parsed));
    TestFalse(TEXT("Oversize response rejected"), FRPGNarrativeCodec::Decode(FString::ChrN(1024 * 1024 + 1, 'x'), Character, Parsed));
    FString Body;
    FRPGNarrativeCommand Command{FGuid::NewGuid(), MAX_int64 - 1, 1, TEXT("DEV.Contact.Offer"), TEXT("accept")};
    TestTrue(TEXT("Int64 command encodes"), FRPGNarrativeCodec::Encode(Command, Body));
    TestTrue(TEXT("Revision is an exact string"), Body.Contains(TEXT("\"9223372036854775806\"")));
    Command.ChoiceId = TEXT("../forged");
    TestFalse(TEXT("Invalid command identifier rejected"), FRPGNarrativeCodec::Encode(Command, Body));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeFlowTest, "ProjectRPG.Narrative.Coordinator",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGNarrativeFlowTest::RunTest(const FString&)
{
    using namespace RPGNarrativeTests;
    FGameFixture Fixture;
    if (!TestTrue(TEXT("Engine creates all narrative subsystems"), Fixture.Init())) return false;
    auto* Owner = Fixture.Player->GetSubsystem<URPGNarrativeCoordinator>();
    auto Transport = MakeShared<FTransport>();
    FRPGNarrativeTestAccess::Attach(Owner, Transport);
    TestTrue(TEXT("Select exact character"), Owner->SelectCharacter(Character));
    TestTrue(TEXT("Refresh starts"), Owner->Refresh()); Transport->Complete(Snapshot(0));
    TestTrue(TEXT("Open authored dialogue"), Owner->BeginDialogue(TEXT("DEV.Contact.Offer")));
    Owner->CloseDialogue();
    TestFalse(TEXT("Closing never accepts quest"), Owner->Choose(TEXT("accept")));
    Owner->BeginDialogue(TEXT("DEV.Contact.Offer"));
    TestFalse(TEXT("Arbitrary choice rejected locally"), Owner->Choose(TEXT("forged")));
    TestTrue(TEXT("Accept dispatches"), Owner->Choose(TEXT("accept")));
    const FString OriginalBody = Transport->LastBody;
    TestFalse(TEXT("Concurrent UI action blocked"), Owner->Choose(TEXT("accept")));
    TestTrue(TEXT("No optimistic quest completion"), Owner->GetQuestManager()->GetQuests(Character).IsEmpty());
    Transport->Complete(FString(), 0);
    TestTrue(TEXT("Unknown outcome retained"), Owner->HasPendingChoice());
    TestFalse(TEXT("Unknown choice blocks character switch"), Owner->SelectCharacter(FGuid::NewGuid()));
    TestTrue(TEXT("Explicit retry available"), Owner->RetryPendingChoice());
    TestEqual(TEXT("Retry preserves request ID, revision and input"), Transport->LastBody, OriginalBody);
    Transport->Complete(Snapshot(1));
    TestFalse(TEXT("Acknowledged choice cleared"), Owner->HasPendingChoice());
    TestEqual(TEXT("Quest manager receives approved state"), Owner->GetQuestManager()->GetQuests(Character).Num(), 1);
    int32 Revision = 1;
    for (const FString Node : {FString(TEXT("DEV.Contact.Explain")), FString(TEXT("DEV.Instructor.Explain")), FString(TEXT("DEV.Contact.Report"))})
    {
        TestTrue(TEXT("Open next authored node"), Owner->BeginDialogue(Node));
        TestTrue(TEXT("Choose next step"), Owner->Choose(Revision == 3 ? TEXT("report") : TEXT("confirm")));
        Transport->Complete(Snapshot(++Revision));
    }
    TestTrue(TEXT("Quest completion visible"), Owner->GetQuestManager()->IsQuestCompleted(Character, TEXT("DEV.Introduction")));
    TestTrue(TEXT("Chapter completion visible"), Owner->GetScenarioManager()->IsChapterCompleted(Character, TEXT("DEV.Prologue")));
    TestTrue(TEXT("Story unlock visible"), Owner->GetScenarioManager()->IsStoryUnlocked(Character, TEXT("DEV.Story.Next")));
    Owner->Refresh(); Transport->Complete(Snapshot(1));
    TestTrue(TEXT("Older response cannot undo completion"), Owner->GetQuestManager()->IsQuestCompleted(Character, TEXT("DEV.Introduction")));
    Transport->Account.Reset();
    FRPGNarrativeTestAccess::Logout(Fixture.Instance->GetSubsystem<UHttpWebManager>());
    TestTrue(TEXT("Logout hides cached quest state"), Owner->GetQuestManager()->GetQuests(Character).IsEmpty());
    TestFalse(TEXT("Logout hides cached story unlocks"), Owner->GetScenarioManager()->IsStoryUnlocked(Character, TEXT("DEV.Story.Next")));
    Transport->Account = TEXT("76561198000000002");
    FRPGNarrativeTestAccess::Login(Fixture.Instance->GetSubsystem<UHttpWebManager>(), Transport->Account);
    TestTrue(TEXT("Another account selects its own context"), Owner->SelectCharacter(FGuid::NewGuid()));
    TestFalse(TEXT("Account switch clears previous view"), Owner->HasCurrentSnapshot());
    Owner->Refresh(); auto Late = MoveTemp(Transport->Pending);
    Fixture.Instance->RemoveLocalPlayer(Fixture.Player);
    FRPGNarrativeHttpResult Result; Result.bCurrentPlayerContext = Result.bTransportSuccessful = true;
    Result.StatusCode = 200; Result.Body = Snapshot(4); Late(MoveTemp(Result));
    TestFalse(TEXT("Late callback after shutdown ignored"), Owner->HasCurrentSnapshot());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeDungeonTest, "ProjectRPG.Narrative.DungeonObjectives",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGNarrativeDungeonTest::RunTest(const FString&)
{
    using namespace RPGNarrativeTests;
    FGameFixture Fixture; if (!TestTrue(TEXT("Subsystem fixture"), Fixture.Init())) return false;
    auto* Flow = Fixture.Player->GetSubsystem<URPGNarrativeCoordinator>();
    auto Transport = MakeShared<FTransport>(); FRPGNarrativeTestAccess::Attach(Flow, Transport);
    TestTrue(TEXT("Select context"), Flow->SelectCharacter(Character));
    FString Candidate = Snapshot(1); Candidate.LeftChopInline(1);
    Candidate += TEXT(",\"dungeonObjectiveCandidates\":[{\"questId\":\"DEV.Introduction\",\"objectiveId\":\"contact\",\"dungeonSessionId\":\"00000001-0000-0002-0000-000300000004\"}]}");
    FRPGNarrativeSnapshot Parsed;
    TestTrue(TEXT("Decode server-proven candidate"), FRPGNarrativeCodec::Decode(Candidate, Character, Parsed));
    TestEqual(TEXT("One candidate"), Parsed.DungeonObjectiveCandidates.Num(), 1);
    FString Invalid = Candidate.Replace(TEXT("\"objectiveId\":\"contact\""), TEXT("\"objectiveId\":\"absent\""));
    TestFalse(TEXT("Unknown candidate objective rejected"), FRPGNarrativeCodec::Decode(Invalid, Character, Parsed));
    Flow->Refresh();
    const int32 InFlightSends = Transport->Sends;
    FRPGNarrativeTestAccess::QueueDungeonRefresh(Flow); FRPGNarrativeTestAccess::PumpDungeon(Flow);
    TestEqual(TEXT("Result event does not overlap an in-flight read"), Transport->Sends, InFlightSends);
    Transport->Complete(Snapshot(1)); FRPGNarrativeTestAccess::PumpDungeon(Flow);
    TestEqual(TEXT("Result event survives the older read response"), Transport->Sends, InFlightSends + 1);
    TestTrue(TEXT("Deferred result reads fresh evidence"), Transport->LastBody.IsEmpty());
    Transport->Complete(Candidate);
    TestFalse(TEXT("No optimistic goal completion"), Flow->GetQuestManager()->GetQuests(Character)[0].CompletedObjectives.Contains(TEXT("contact")));
    FRPGNarrativeTestAccess::PumpDungeon(Flow);
    TestTrue(TEXT("Automatic dedicated objective POST"), Transport->bDungeonSend);
    FString Original = Transport->LastBody;
    TSharedPtr<FJsonObject> EncodedGoal; FString EncodedRevision;
    TestTrue(TEXT("Evidence reference encoded"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Original), EncodedGoal)
        && EncodedGoal && EncodedGoal->HasField(TEXT("dungeonSessionId"))
        && EncodedGoal->TryGetStringField(TEXT("expectedRevision"), EncodedRevision) && EncodedRevision == TEXT("1"));
    TestFalse(TEXT("No authored choice in evidence body"), Original.Contains(TEXT("dialogueId")));
    Transport->Complete(FString(), 0);
    TestTrue(TEXT("Unknown goal result retained"), Flow->HasPendingChoice());
    TestFalse(TEXT("Uncertain goal blocks character switch"), Flow->SelectCharacter(FGuid::NewGuid()));
    TestTrue(TEXT("Shared retry retries the goal"), Flow->RetryPendingChoice());
    TestTrue(TEXT("Retry uses goal route"), Transport->bDungeonSend);
    TestEqual(TEXT("Retry preserves exact body"), Transport->LastBody, Original);
    Transport->Complete(TEXT("{\"error\":\"narrative_revision_conflict\"}"), 409);
    TestFalse(TEXT("Definite conflict clears pending goal"), Flow->HasPendingChoice());
    FRPGNarrativeTestAccess::PumpDungeon(Flow);
    TestTrue(TEXT("Conflict reads fresh state"), Transport->LastBody.IsEmpty() && !Transport->bDungeonSend);
    Transport->Complete(Snapshot(2));
    const int32 Sends = Transport->Sends;
    FRPGNarrativeTestAccess::PumpDungeon(Flow);
    TestEqual(TEXT("No candidates means no further writes"), Transport->Sends, Sends);
    TestTrue(TEXT("Server completion restored"), Flow->GetQuestManager()->GetQuests(Character)[0].CompletedObjectives.Contains(TEXT("contact")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeSubsystemTest, "ProjectRPG.Narrative.SubsystemOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGNarrativeSubsystemTest::RunTest(const FString&)
{
    using namespace RPGNarrativeTests;
    FGameFixture First, Second;
    if (!TestTrue(TEXT("Independent game instances initialize"), First.Init() && Second.Init())) return false;
    auto* Flow = First.Player->GetSubsystem<URPGNarrativeCoordinator>();
    auto* QuestService = First.Instance->GetSubsystem<URPGQuestManager>();
    auto* ScenarioService = First.Instance->GetSubsystem<URPGScenarioManager>();
    TestEqual(TEXT("Quest service is engine-owned by GI"), QuestService->GetOuter(), static_cast<UObject*>(First.Instance.Get()));
    TestEqual(TEXT("Coordinator borrows GI quest service"), Flow->GetQuestManager(), QuestService);
    TestEqual(TEXT("Coordinator borrows GI scenario service"), Flow->GetScenarioManager(), ScenarioService);
    TestEqual(TEXT("Dialogue service is engine-owned by local player"), Flow->GetDialogueManager()->GetOuter(), static_cast<UObject*>(First.Player));
    auto Transport = MakeShared<FTransport>(); FRPGNarrativeTestAccess::Attach(Flow, Transport);
    Flow->SelectCharacter(Character); Flow->Refresh(); Transport->Complete(Snapshot(4));
    const FGuid Alt = FGuid::NewGuid();
    Flow->SelectCharacter(Alt); Flow->Refresh(); Transport->Complete(Snapshot(0, Alt));
    TestTrue(TEXT("Character switch preserves first character cache"), QuestService->IsQuestCompleted(Character, TEXT("DEV.Introduction")));
    TestFalse(TEXT("Alt does not inherit first character completion"), QuestService->IsQuestCompleted(Alt, TEXT("DEV.Introduction")));
    TestFalse(TEXT("Another GI has no shared cache"), Second.Instance->GetSubsystem<URPGQuestManager>()->IsQuestCompleted(Character, TEXT("DEV.Introduction")));
    FString Error; auto* OtherPlayer = First.Instance->CreateLocalPlayer(1, Error, false);
    if (!TestNotNull(TEXT("Second local player"), OtherPlayer)) return false;
    auto* Other = OtherPlayer->GetSubsystem<URPGNarrativeCoordinator>();
    TestEqual(TEXT("Local players share GI quest service"), Other->GetQuestManager(), QuestService);
    TestTrue(TEXT("Dialogue services are independent"), Other->GetDialogueManager() != Flow->GetDialogueManager());
    auto OtherTransport = MakeShared<FTransport>(); FRPGNarrativeTestAccess::Attach(Other, OtherTransport);
    Other->SelectCharacter(Alt); Other->Refresh(); OtherTransport->Complete(Snapshot(0, Alt));
    TestTrue(TEXT("Only first local player opens dialogue"), Flow->BeginDialogue(TEXT("DEV.Contact.Offer")));
    FRPGDialogueNode Node;
    TestFalse(TEXT("Second local player has no first-player active dialogue"), Other->GetDialogueManager()->GetActiveDialogue(Node));
    First.Instance->RemoveLocalPlayer(First.Player);
    TestTrue(TEXT("Removing coordinator preserves GI quest cache"), QuestService->IsQuestCompleted(Character, TEXT("DEV.Introduction")));
    TestTrue(TEXT("Removing coordinator preserves GI scenario cache"), ScenarioService->IsChapterCompleted(Character, TEXT("DEV.Prologue")));
    TestTrue(TEXT("Remaining local player remains usable"), Other->HasCurrentSnapshot());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeHttpTest, "ProjectRPG.Narrative.PlayerTransport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGNarrativeHttpTest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
    TStrongObjectPtr<UHttpWebManager> Web(NewObject<UHttpWebManager>(Instance.Get()));
    FRPGNarrativeTestAccess::Login(Web.Get());
    bool bCalled = false, bCurrent = true;
    auto Request = Web->CreatePlayerNarrativeRequest(RPGNarrativeTests::Character, FString(),
        [&](FRPGNarrativeHttpResult Result) { bCalled = true; bCurrent = Result.bCurrentPlayerContext; });
    if (!TestTrue(TEXT("Owned player request built"), Request.IsValid())) return false;
    TestEqual(TEXT("Read verb"), Request->GetVerb(), FString(TEXT("GET")));
    TestTrue(TEXT("Exact character endpoint"), Request->GetURL().EndsWith(
        RPGNarrativeTests::Character.ToString(EGuidFormats::DigitsWithHyphensLower) + TEXT("/narrative")));
    TestTrue(TEXT("Player authorization attached"), Request->GetHeader(TEXT("Authorization")).StartsWith(TEXT("Bearer ")));
    FRPGNarrativeTestAccess::Logout(Web.Get());
    Request->OnProcessRequestComplete().ExecuteIfBound(Request, nullptr, false);
    TestTrue(TEXT("Callback still completes"), bCalled); TestFalse(TEXT("Old credential callback is stale"), bCurrent);
    TestFalse(TEXT("No request after logout"), Web->CreatePlayerNarrativeRequest(RPGNarrativeTests::Character,
        FString(), [](FRPGNarrativeHttpResult) {}).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeDialogueWidgetTest, "ProjectRPG.Narrative.DialogueWidget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGNarrativeDialogueWidgetTest::RunTest(const FString&)
{
    using namespace RPGNarrativeTests;
    FGameFixture Fixture;
    if (!TestTrue(TEXT("UI fixture"), Fixture.Init())) return false;
#if UE_SUPPORT_FOR_ACTOR_TICK_DISABLE
    Fixture.World->EnableActorTickAndUserCallbacks(true);
#endif
    Fixture.World->InitializeActorsForPlay(FURL());
    auto* Controller = Fixture.World->SpawnActor<APlayerController>();
    if (!TestNotNull(TEXT("Widget player controller"), Controller)) return false;
    Controller->SetPlayer(Fixture.Player); Fixture.World->AddController(Controller);
    if (!TestEqual(TEXT("Controller registered for this world"), Fixture.Player->GetPlayerController(Fixture.World), Controller)) return false;
    auto* Flow = Fixture.Player->GetSubsystem<URPGNarrativeCoordinator>();
    auto Transport = MakeShared<FTransport>(); FRPGNarrativeTestAccess::Attach(Flow, Transport);
    Flow->SelectCharacter(Character);
    TStrongObjectPtr<URPGNarrativeDialogueWidget> Panel(CreateWidget<URPGNarrativeDialogueWidget>(Controller));
    if (!TestNotNull(TEXT("Native story widget"), Panel.Get())) return false;
    Panel->SetOwningLocalPlayer(Fixture.Player);
    auto* Dungeon = Fixture.Instance->GetSubsystem<URPGDungeonSessionSubsystem>();
    const FGuid Alt(9, 8, 7, 6);
    auto Select = [&](const FGuid Id) { return Dungeon->SelectCharacter(Id.ToString(EGuidFormats::DigitsWithHyphensLower).ToUpper()); };
    TestTrue(TEXT("Select another character globally"), Select(Alt));
    TestFalse(TEXT("Stale character cannot open a story window"), Panel->OpenForCharacter(Character));
    TestEqual(TEXT("Rejected open sends no request"), Transport->Sends, 0);
    TestTrue(TEXT("Restore global character"), Select(Character));
    TestTrue(TEXT("Open exact character"), Panel->OpenForCharacter(Character));
    const TSharedRef<SWidget> PanelSlate = Panel->TakeWidget();
    TestTrue(TEXT("Global selection changes while the old GET is in flight"), Select(Alt));
    Transport->Complete(Snapshot(0));
    auto* Actions = Cast<UVerticalBox>(Panel->GetWidgetFromName(TEXT("NarrativeActions")));
    auto* Refresh = Cast<UButton>(Panel->GetWidgetFromName(TEXT("NarrativeRefresh")));
    auto* Journal = Cast<UTextBlock>(Panel->GetWidgetFromName(TEXT("NarrativeQuestJournal")));
    if (!TestNotNull(TEXT("Actions control"), Actions) || !TestNotNull(TEXT("Refresh control"), Refresh)
        || !TestNotNull(TEXT("Journal control"), Journal)) return false;
    TestEqual(TEXT("Late old-character GET cannot expose its actions"), Actions->GetChildrenCount(), 0);
    TestTrue(TEXT("Late old-character GET cannot expose its journal"), Journal->GetText().IsEmpty());
    const int32 BeforeRefresh = Transport->Sends;
    Refresh->OnClicked.Broadcast();
    TestEqual(TEXT("Stale refresh callback sends no request"), Transport->Sends, BeforeRefresh);
    TestTrue(TEXT("Select original character for its cached view"), Select(Character));
    FRPGNarrativeTestAccess::TickNPC(Panel.Get());
    auto Click = [&](const FString& Node, const FString& Choice) -> bool
    {
        auto* List = Cast<UVerticalBox>(Panel->GetWidgetFromName(TEXT("NarrativeActions")));
        if (!List) return false;
        for (auto* Child : List->GetAllChildren())
            if (auto* Row = Cast<URPGNarrativeActionWidget>(Child))
                if (Row->GetDialogueId() == Node && Row->GetChoiceId() == Choice)
                {
                    Row->TakeWidget();
                    if (auto* Button = Cast<UButton>(Row->GetWidgetFromName(TEXT("NarrativeActionButton"))))
                    { Button->OnClicked.Broadcast(); return true; }
                }
        return false;
    };
    TestTrue(TEXT("Open from rendered server node"), Click(TEXT("DEV.Contact.Offer"), FString()));
    TestFalse(TEXT("Opening a conversation does not accept"), Flow->HasPendingChoice());
    TestTrue(TEXT("Switch before the old choice's click handler runs"), Select(Alt));
    const int32 BeforeChoice = Transport->Sends;
    TestTrue(TEXT("Previously rendered accept button still exists before tick"), Click(TEXT("DEV.Contact.Offer"), TEXT("accept")));
    TestEqual(TEXT("Stale choice callback sends no command"), Transport->Sends, BeforeChoice);
    TestFalse(TEXT("Stale choice creates no uncertain command"), Flow->HasPendingChoice());
    FRPGNarrativeTestAccess::TickNPC(Panel.Get());
    TestEqual(TEXT("Tick hides old-character actions"), Actions->GetChildrenCount(), 0);
    TestTrue(TEXT("Tick hides old-character journal"), Journal->GetText().IsEmpty());
    TestTrue(TEXT("Restore selection after stale click"), Select(Character));
    FRPGNarrativeTestAccess::TickNPC(Panel.Get());
    Panel->CloseScreen();
    FRPGDialogueNode Active;
    TestFalse(TEXT("Closing clears only local conversation"), Flow->GetDialogueManager()->GetActiveDialogue(Active));
    TestTrue(TEXT("Closing does not create quest progress"), Flow->GetQuestManager()->GetQuests(Character).IsEmpty());
    if (!TestTrue(TEXT("Reopen dispatches GET"), Panel->OpenForCharacter(Character))
        || !TestTrue(TEXT("Reopen GET callback exists"), Transport->Complete(Snapshot(0)))) return false;
    Click(TEXT("DEV.Contact.Offer"), FString());
    TestTrue(TEXT("Authored accept button dispatches"), Click(TEXT("DEV.Contact.Offer"), TEXT("accept")));
    const FString Intent = Transport->LastBody;
    TestTrue(TEXT("No local progress before server reply"), Flow->GetQuestManager()->GetQuests(Character).IsEmpty());
    if (!TestTrue(TEXT("Accept POST callback exists"), Transport->Complete(FString(), 0))) return false;
    auto* Retry = Cast<UButton>(Panel->GetWidgetFromName(TEXT("NarrativeRetry")));
    if (!TestNotNull(TEXT("Uncertain result has retry control"), Retry)) return false;
    TestTrue(TEXT("Switch globally while a choice outcome is uncertain"), Select(Alt));
    const int32 BeforeRetry = Transport->Sends;
    Retry->OnClicked.Broadcast(); Refresh->OnClicked.Broadcast();
    TestEqual(TEXT("Stale retry and refresh callbacks send nothing"), Transport->Sends, BeforeRetry);
    TestTrue(TEXT("Switching preserves the original uncertain command"), Flow->HasPendingChoice());
    Panel->CloseScreen();
    TestFalse(TEXT("Cannot reopen a pending command for a different global selection"), Panel->OpenForCharacter(Character));
    TestTrue(TEXT("Restore original selection for recovery"), Select(Character));
    TestTrue(TEXT("Reopen preserves uncertain command without replacing it"), Panel->OpenForCharacter(Character));
    Retry->OnClicked.Broadcast();
    TestEqual(TEXT("Widget retry retains the original command"), Transport->LastBody, Intent);
    if (!TestTrue(TEXT("Retry POST callback exists"), Transport->Complete(Snapshot(1)))) return false;
    TestEqual(TEXT("Server approval populates progress"), Flow->GetQuestManager()->GetQuests(Character).Num(), 1);
    TestTrue(TEXT("Journal displays authored title and unfinished goal"), Journal->GetText().ToString().Contains(TEXT("Test quest"))
        && Journal->GetText().ToString().Contains(TEXT("Contact briefing")));
    int32 Revision = 1;
    for (const FString Node : {FString(TEXT("DEV.Contact.Explain")), FString(TEXT("DEV.Instructor.Explain")), FString(TEXT("DEV.Contact.Report"))})
    {
        TestTrue(TEXT("Server node appears in story list"), Click(Node, FString()));
        TestTrue(TEXT("Server choice appears in conversation"), Click(Node, Revision == 3 ? TEXT("report") : TEXT("confirm")));
        if (!TestTrue(TEXT("Dialogue choice POST callback exists"), Transport->Complete(Snapshot(++Revision)))) return false;
    }
    TestTrue(TEXT("Report button commits through coordinator"), Flow->GetScenarioManager()->IsChapterCompleted(Character, TEXT("DEV.Prologue")));
    TestTrue(TEXT("Completed quest retains its title"), Journal->GetText().ToString().Contains(TEXT("Test quest")));
    TestEqual(TEXT("Completed dialogue list is empty"), Cast<UVerticalBox>(Panel->GetWidgetFromName(TEXT("NarrativeActions")))->GetChildrenCount(), 0);
    Panel->CloseScreen();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeNPCTest, "ProjectRPG.Narrative.NPCInteraction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGNarrativeNPCTest::RunTest(const FString&)
{
    using namespace RPGNarrativeTests;
    FGameFixture Fixture;
    if (!TestTrue(TEXT("NPC fixture"), Fixture.Init(true))) return false;
#if UE_SUPPORT_FOR_ACTOR_TICK_DISABLE
    Fixture.World->EnableActorTickAndUserCallbacks(true);
#endif
    Fixture.World->InitializeActorsForPlay(FURL());
    auto* Controller = Fixture.World->SpawnActor<APlayerController>();
    auto* Pawn = Fixture.World->SpawnActor<ADefaultPawn>();
    auto* Contact = Fixture.World->SpawnActor<ARPGNarrativeNPC>();
    auto* Instructor = Fixture.World->SpawnActor<ARPGNarrativeNPC>();
    if (!Controller || !Pawn || !Contact || !Instructor) return false;
    Controller->SetPlayer(Fixture.Player); Fixture.World->AddController(Controller); Controller->Possess(Pawn);
    auto* Skills = NewObject<URPGSkillWindowComponent>(Controller);
    Controller->AddInstanceComponent(Skills); Skills->RegisterComponent();
    auto SharedInput = MakeShared<FRPGWindowInputState>(); Skills->SetWindowInputState(SharedInput);
    Controller->SetIgnoreMoveInput(true); // An unrelated counted lock must survive NPC close.
    FRPGNarrativeTestAccess::ConfigureNPC(Contact, {TEXT("DEV.Contact.Offer"), TEXT("DEV.Contact.Explain"), TEXT("DEV.Contact.Report")});
    FRPGNarrativeTestAccess::ConfigureNPC(Instructor, {TEXT("DEV.Instructor.Explain")});
    auto* Dungeon = Fixture.Instance->GetSubsystem<URPGDungeonSessionSubsystem>();
    if (!TestTrue(TEXT("Selected character for NPC"), Dungeon->SelectCharacter(Character.ToString(EGuidFormats::DigitsWithHyphensLower)))) return false;
    auto* Flow = Fixture.Player->GetSubsystem<URPGNarrativeCoordinator>();
    auto Transport = MakeShared<FTransport>(); FRPGNarrativeTestAccess::Attach(Flow, Transport);
    TestTrue(TEXT("Native NPC implements existing interaction interface"), Contact->GetClass()->ImplementsInterface(UInteractionInterface::StaticClass()));
    auto* Bounds = Cast<USphereComponent>(Contact->GetRootComponent());
    TestTrue(TEXT("Existing controller WorldDynamic query can discover NPC"), Bounds && Bounds->GetCollisionObjectType() == ECC_WorldDynamic
        && Bounds->GetCollisionEnabled() == ECollisionEnabled::QueryOnly);
    static_cast<IInteractionInterface*>(Contact)->Interact(Controller);
    auto* Panel = Contact->GetOpenDialogue(Controller);
    if (!TestNotNull(TEXT("Interface opens LocalPlayer viewport panel"), Panel)) return false;
    TestTrue(TEXT("NPC uses the controller's existing window input state"), SharedInput->ContainsWindow(*Panel));
    TestTrue(TEXT("NPC exposes mouse cursor"), Controller->bShowMouseCursor);
    if (!TestTrue(TEXT("NPC refresh callback"), Transport->Complete(Snapshot(0)))) return false;
    const int32 Sends = Transport->Sends;
    TestTrue(TEXT("Repeated interaction reuses panel"), Contact->OpenDialogue(Controller));
    TestEqual(TEXT("No duplicate query on repeated interaction"), Transport->Sends, Sends);
    auto Click = [&](const FString& Node, const FString& Choice) -> bool
    {
        auto* List = Cast<UVerticalBox>(Panel->GetWidgetFromName(TEXT("NarrativeActions")));
        if (!List) return false;
        for (auto* Child : List->GetAllChildren())
            if (auto* Row = Cast<URPGNarrativeActionWidget>(Child))
                if (Row->GetDialogueId() == Node && Row->GetChoiceId() == Choice && Row->GetIsEnabled())
                    if (auto* Button = Cast<UButton>(Row->GetWidgetFromName(TEXT("NarrativeActionButton"))))
                    { Button->OnClicked.Broadcast(); return true; }
        return false;
    };
    TestTrue(TEXT("NPC offer opens"), Click(TEXT("DEV.Contact.Offer"), FString()));
    TestTrue(TEXT("NPC accept dispatches"), Click(TEXT("DEV.Contact.Offer"), TEXT("accept")));
    if (!TestTrue(TEXT("Server accepts NPC quest"), Transport->Complete(Snapshot(1)))) return false;
    TestFalse(TEXT("Other NPC entry is absent"), Click(TEXT("DEV.Instructor.Explain"), FString()));
    FRPGNarrativeTestAccess::NPCAction(Panel, TEXT("DEV.Instructor.Explain"));
    FRPGDialogueNode Active;
    TestFalse(TEXT("Direct widget action cannot select another NPC's entry"), Flow->GetDialogueManager()->GetActiveDialogue(Active));
    TestTrue(TEXT("Owned objective entry opens"), Click(TEXT("DEV.Contact.Explain"), FString()));
    TestTrue(TEXT("Owned objective dispatches"), Click(TEXT("DEV.Contact.Explain"), TEXT("confirm")));
    const FString Intent = Transport->LastBody;
    Transport->Complete(FString(), 0);
    Pawn->SetActorLocation(FVector(5000, 0, 0)); FRPGNarrativeTestAccess::TickNPC(Panel);
    TestFalse(TEXT("Distance exit closes NPC viewport"), Panel->IsInViewport());
    TestFalse(TEXT("NPC releases its shared window entry"), SharedInput->HasOpenWindows());
    TestTrue(TEXT("Unrelated counted movement lock retained"), Controller->IsMoveInputIgnored());
    Controller->SetIgnoreMoveInput(false); TestFalse(TEXT("NPC added no stale movement lock"), Controller->IsMoveInputIgnored());
    TestFalse(TEXT("Distance exit clears owned local conversation"), Flow->GetDialogueManager()->GetActiveDialogue(Active));
    TestTrue(TEXT("Distance exit preserves uncertain command"), Flow->HasPendingChoice());
    TestFalse(TEXT("Out-of-range interaction denied"), Contact->OpenDialogue(Controller));
    Pawn->SetActorLocation(FVector::ZeroVector);
    TestFalse(TEXT("Different NPC cannot recover another entry's command"), Instructor->OpenDialogue(Controller));
    if (!TestTrue(TEXT("Same NPC can reopen pending result"), Contact->OpenDialogue(Controller))) return false;
    Panel = Contact->GetOpenDialogue(Controller);
    auto* Retry = Panel ? Cast<UButton>(Panel->GetWidgetFromName(TEXT("NarrativeRetry"))) : nullptr;
    if (!TestNotNull(TEXT("NPC retry control"), Retry)) return false;
    Retry->OnClicked.Broadcast(); TestEqual(TEXT("NPC retry keeps exact command"), Transport->LastBody, Intent);
    if (!TestTrue(TEXT("NPC retry confirms"), Transport->Complete(Snapshot(2)))) return false;
    Dungeon->SelectCharacter(FGuid(9, 8, 7, 6).ToString(EGuidFormats::DigitsWithHyphensLower));
    FRPGNarrativeTestAccess::TickNPC(Panel); TestFalse(TEXT("Character switch closes NPC panel"), Panel->IsInViewport());
    Dungeon->SelectCharacter(Character.ToString(EGuidFormats::DigitsWithHyphensLower));
    if (!TestTrue(TEXT("NPC reopens after selection restored"), Contact->OpenDialogue(Controller))) return false;
    Panel = Contact->GetOpenDialogue(Controller); Transport->Complete(Snapshot(2));
    Controller->UnPossess(); FRPGNarrativeTestAccess::TickNPC(Panel);
    TestFalse(TEXT("Unpossession closes NPC panel"), Panel->IsInViewport()); Controller->Possess(Pawn);
    if (!TestTrue(TEXT("NPC reopens after possession restored"), Contact->OpenDialogue(Controller))) return false;
    Panel = Contact->GetOpenDialogue(Controller); Transport->Complete(Snapshot(2));
    Contact->Destroy(); FRPGNarrativeTestAccess::TickNPC(Panel);
    TestFalse(TEXT("NPC destruction closes viewport"), Panel->IsInViewport());
    TestFalse(TEXT("NPC destruction leaves no owned conversation"), Flow->GetDialogueManager()->GetActiveDialogue(Active));
    TestFalse(TEXT("No new progress from lifetime changes"), Flow->HasPendingChoice());
    TestTrue(TEXT("Only confirmed objective persisted"), Flow->GetQuestManager()->HasRevision(Character, 2));
    TestTrue(TEXT("LocalPlayer UI stack released"), !UUIManager::Get(Fixture.Player)->HasUIOpen());
    Skills->DestroyComponent(); // Exercise the generic-controller fallback after the shared-controller path.
    auto* Guide = Fixture.World->SpawnActor<ARPGNarrativeNPC>();
    FRPGNarrativeTestAccess::ConfigureNPC(Guide, {TEXT("INFO.Start")});
    if (!TestTrue(TEXT("Information NPC opens"), Guide->OpenDialogue(Controller))) return false;
    Panel = Guide->GetOpenDialogue(Controller); Transport->Complete(InformationSnapshot(false, 2));
    const int32 BeforeInformation = Transport->Sends;
    TestTrue(TEXT("NPC information root"), Click(TEXT("INFO.Start"), FString()));
    TestTrue(TEXT("NPC linked hidden topic"), Click(TEXT("INFO.Start"), TEXT("ask")));
    TestTrue(TEXT("NPC information back"), Click(TEXT("INFO.Topic"), TEXT("back")));
    TestTrue(TEXT("NPC information close"), Click(TEXT("INFO.Start"), TEXT("close")));
    TestEqual(TEXT("NPC information navigation sends no command"), Transport->Sends, BeforeInformation);
    TestTrue(TEXT("Fallback NPC widget can rebind without duplicating input locks"), Panel->OpenForNPC(Character, Guide));
    Transport->Complete(InformationSnapshot(false, 2)); Panel->FocusNPCInput();
    TestTrue(TEXT("Escape is handled by NPC window"), FRPGNarrativeTestAccess::BackNPC(Panel));
    TestFalse(TEXT("Escape closes viewport"), Panel->IsInViewport());
    TestFalse(TEXT("Rebinding/close releases fallback movement lock"), Controller->IsMoveInputIgnored());
    FString Candidate = Snapshot(2); Candidate.LeftChopInline(1);
    Candidate += TEXT(",\"dungeonObjectiveCandidates\":[{\"questId\":\"DEV.Introduction\",\"objectiveId\":\"preparation\",\"dungeonSessionId\":\"00000001-0000-0002-0000-000300000004\"}]}");
    Flow->Refresh(); Transport->Complete(Candidate); FRPGNarrativeTestAccess::PumpDungeon(Flow);
    const FString GoalBody = Transport->LastBody; Transport->Complete(FString(), 0);
    TestTrue(TEXT("Unknown dungeon goal retained"), Flow->HasPendingDungeonObjective());
    if (!TestTrue(TEXT("Information NPC can reopen a player-owned pending goal"), Guide->OpenDialogue(Controller))) return false;
    Panel = Guide->GetOpenDialogue(Controller);
    auto* RetryGoal = Cast<UButton>(Panel->GetWidgetFromName(TEXT("NarrativeRetry")));
    if (!TestTrue(TEXT("NPC goal retry is enabled"), RetryGoal && RetryGoal->GetIsEnabled())) return false;
    const int32 BeforeGoalRetry = Transport->Sends;
    RetryGoal->OnClicked.Broadcast();
    TestEqual(TEXT("NPC retry actually dispatches"), Transport->Sends, BeforeGoalRetry + 1);
    TestTrue(TEXT("NPC retries the goal route"), Transport->bDungeonSend);
    TestEqual(TEXT("NPC preserves the pending evidence request"), Transport->LastBody, GoalBody);
    Transport->Complete(Snapshot(3)); Panel->CloseScreen();
    if (!TestTrue(TEXT("NPC reopens after Escape"), Guide->OpenDialogue(Controller))) return false;
    Panel = Guide->GetOpenDialogue(Controller); Transport->Complete(InformationSnapshot(false, 3));
    FRPGNarrativeTestAccess::Logout(Fixture.Instance->GetSubsystem<UHttpWebManager>());
    FRPGNarrativeTestAccess::TickNPC(Panel); TestFalse(TEXT("Logout closes NPC panel"), Panel->IsInViewport());
    TestFalse(TEXT("Logout releases fallback input"), Controller->IsMoveInputIgnored());
    Guide->Destroy();
    Instructor->Destroy(); Controller->Destroy(); Pawn->Destroy();
    return true;
}
namespace RPGNarrativeTests
{
    // Opt-in real HTTP test. The runner supplies only nonsecret IDs and a private
    // loopback configuration; no test transport or state injection is used here.
    class FHttpDialogueCommand final : public IAutomationLatentCommand
    {
    public:
        FHttpDialogueCommand(FAutomationTestBase* InTest, FGuid InCharacter, FString InSteam,
            FString InWorld, bool bInRestore, FString InDungeonPhase = FString())
            : Test(InTest), Character(InCharacter), Steam(MoveTemp(InSteam)), WorldName(MoveTemp(InWorld)),
                DungeonPhase(MoveTemp(InDungeonPhase)), bRestore(bInRestore) {}
        ~FHttpDialogueCommand()
        {
            if (ContactNPC.IsValid()) ContactNPC->Destroy();
            if (InstructorNPC.IsValid()) InstructorNPC->Destroy();
            if (SpawnedPawn.IsValid()) SpawnedPawn->Destroy();
            if (Lobby.IsValid()) Lobby->RemoveFromParent();
        }
        bool Update() override
        {
            if (FPlatformTime::Seconds() > Deadline) return Fail(FString::Printf(TEXT("HTTP dialogue timed out at stage %d, choice %d"), Stage, Choice));
            if (Stage == 0)
            {
                if (!FSlateApplication::IsInitialized()) return Fail(TEXT("Slate unavailable"));
                for (const auto& Context : GEngine->GetWorldContexts())
                    if (Context.WorldType == EWorldType::Game && Context.World()
                        && Context.World()->GetOutermost()->GetName() == WorldName)
                    { World = Context.World(); break; }
                if (!World || !World->HasBegunPlay()) return false;
                Instance.Reset(World->GetGameInstance());
                Player = Instance.IsValid() ? Instance->GetFirstGamePlayer() : nullptr;
                Controller = Player ? Player->GetPlayerController(World) : nullptr;
                if (!Controller || Controller->GetLocalPlayer() != Player) return false;
                Web = Instance->GetSubsystem<UHttpWebManager>();
                Flow = Instance->GetSubsystem<URPGDungeonSessionSubsystem>();
                Narrative = Player->GetSubsystem<URPGNarrativeCoordinator>();
                if (!Web || !Flow || !Narrative)
                    return Fail(FString::Printf(TEXT("Isolated services missing: web=%d flow=%d narrative=%d"), Web != nullptr, Flow != nullptr, Narrative != nullptr));
                if (!Web->GetApiUrl().StartsWith(TEXT("http://127.0.0.1:")))
                    return Fail(TEXT("Isolated loopback endpoint not applied: ") + Web->GetApiUrl());
                Web->AuthenticateWithSteamTicket(TEXT("dev:") + Steam); Stage = 1; return false;
            }
            if (Stage == 1)
            {
                if (!Web->HasAccessToken()) return false;
                if (Web->GetAuthenticatedSteamId() != Steam) return Fail(TEXT("Wrong authenticated owner"));
                if (Player->GetPlayerController(World) != Controller || Controller->GetLocalPlayer() != Player)
                    return Fail(FString::Printf(TEXT("Controller changed awaiting auth: registered=%d local=%d destroyed=%d worldTearingDown=%d"),
                        Player->GetPlayerController(World) == Controller, Controller->GetLocalPlayer() == Player,
                        Controller->IsActorBeingDestroyed(), World->bIsTearingDown));
                if (!Flow->SelectCharacter(Character.ToString(EGuidFormats::DigitsWithHyphensLower))) return Fail(TEXT("Character selection failed"));
                if (!DungeonPhase.IsEmpty())
                {
                    if (!Flow->ResumeDungeonSession()) return Fail(TEXT("Dungeon recovery query failed"));
                    Stage = 20; return false;
                }
                if (bNPC)
                {
                    if (!Controller->GetPawn())
                    {
                        SpawnedPawn = World->SpawnActor<ADefaultPawn>();
                        if (!SpawnedPawn.IsValid()) return Fail(TEXT("NPC interaction pawn missing"));
                        Controller->Possess(SpawnedPawn.Get());
                    }
                    ContactNPC = World->SpawnActor<ARPGNarrativeNPC>(); InstructorNPC = World->SpawnActor<ARPGNarrativeNPC>();
                    if (!ContactNPC.IsValid() || !InstructorNPC.IsValid()) return Fail(TEXT("World NPC spawn failed"));
                    const FVector Position = Controller->GetPawn()->GetActorLocation();
                    ContactNPC->SetActorLocation(Position); InstructorNPC->SetActorLocation(Position + FVector(60, 0, 0));
                    FRPGNarrativeTestAccess::ConfigureNPC(ContactNPC.Get(), {TEXT("DEV.Contact.Offer"), TEXT("DEV.Contact.Explain"), TEXT("DEV.Contact.Report")});
                    FRPGNarrativeTestAccess::ConfigureNPC(InstructorNPC.Get(), {TEXT("DEV.Instructor.Explain")});
                    Stage = 2; return false;
                }
                Lobby.Reset(CreateWidget<URPGOnlineLobbyWidget>(Controller));
                if (!Lobby.IsValid() || Lobby->GetOwningLocalPlayer() != Player)
                    return Fail(FString::Printf(TEXT("Created lobby context mismatch: exists=%d initialized=%d valid=%d"),
                        Lobby.IsValid(), Lobby.IsValid() && Lobby->GetPlayerContext().IsInitialized(), Lobby.IsValid() && Lobby->GetPlayerContext().IsValid()));
                if (!Lobby.IsValid() || !Lobby->AddToPlayerScreen()) return Fail(TEXT("Lobby viewport unavailable"));
                Stage = 2; return false;
            }
            if (Stage == 20)
            {
                if (Narrative->IsBusy() || !Narrative->HasCurrentSnapshot()) return false;
                if (Narrative->HasPendingChoice()) return Fail(TEXT("Automatic dungeon goal has an unresolved HTTP result"));
                const auto Quests = Narrative->GetQuestManager()->GetQuests(Character);
                const auto* Goal = Quests.FindByPredicate([](const auto& Q) { return Q.QuestId == TEXT("DEV.DungeonProof"); });
                if (!Goal || Goal->Status == ERPGQuestStatus::Active) return false;
                if (Goal->Status != ERPGQuestStatus::ReadyToReport || Goal->CompletedObjectives.Num() != 1
                    || !Goal->CompletedObjectives.Contains(TEXT("clear")) || !Narrative->GetQuestManager()->HasRevision(Character, 6))
                    return Fail(TEXT("Automatic dungeon goal bypassed reporting or duplicated progress"));
                Test->AddInfo(TEXT("Dungeon goal HTTP automatic sync verified: ") + DungeonPhase); return true;
            }
            if (Stage == 2)
            {
                if (bNPC)
                {
                    if (!OpenNPC()) return Fail(TEXT("Actual NPC interface failed to open viewport"));
                    Stage = 3; return false;
                }
                if (Lobby->GetCharacterOption(Character.ToString(EGuidFormats::DigitsWithHyphensLower)).IsEmpty()
                    || Lobby->IsFrontendRequestInFlight()) return false;
                auto* Button = Cast<UButton>(Lobby->GetWidgetFromName(TEXT("OpenStoryMenuButton")));
                if (!Button || !Button->GetIsEnabled() || !Button->IsVisible()) return false;
                Button->OnClicked.Broadcast();
                if (!Lobby->GetNarrativeDialogueWidget() || !Lobby->GetNarrativeDialogueWidget()->IsInViewport())
                    return Fail(TEXT("Story menu did not open the actual viewport panel"));
                Stage = 3; return false;
            }
            if (Narrative->IsBusy()) return false;
            if (Narrative->HasPendingChoice() || !Narrative->HasCurrentSnapshot()) return Fail(TEXT("HTTP response not confirmed"));
            if (bNPC && CurrentNPC.Get() != DesiredNPC())
            {
                if (!OpenNPC()) return Fail(TEXT("NPC switch failed"));
                return false;
            }
            auto* Panel = bNPC ? CurrentNPC->GetOpenDialogue(Controller) : Lobby->GetNarrativeDialogueWidget();
            if (!Panel || !Panel->IsInViewport()) return Fail(TEXT("Story panel closed unexpectedly"));
            if (bNPC && (!Controller->bShowMouseCursor || !Controller->IsMoveInputIgnored()))
                return Fail(TEXT("World NPC cursor/movement input lifetime missing"));
            if (!bInformationChecked && FParse::Param(FCommandLine::Get(), TEXT("RPGNarrativeInformation")))
            {
                if (!Click(Panel, TEXT("INFO.Start"), FString()) || !Click(Panel, TEXT("INFO.Start"), TEXT("ask"))
                    || !Click(Panel, TEXT("INFO.Topic"), TEXT("more")) || !Click(Panel, TEXT("INFO.Detail"), TEXT("back"))
                    || !Click(Panel, TEXT("INFO.Topic"), TEXT("back")) || !Click(Panel, TEXT("INFO.Start"), TEXT("close")))
                    return Fail(TEXT("Real server information graph did not navigate through viewport buttons"));
                FRPGDialogueNode Conversation;
                if (Narrative->IsBusy() || Narrative->HasPendingChoice()
                    || !Narrative->GetQuestManager()->HasRevision(Character, bRestore ? 4 : 0)
                    || Narrative->GetDialogueManager()->GetActiveDialogue(Conversation))
                    return Fail(TEXT("Information navigation created a write or left an active conversation"));
                bInformationChecked = true;
                Test->AddInfo(TEXT("Real HTTP information graph navigated without a progress command."));
            }
            if (Choice > 0 || bRestore)
            {
                const auto Quests = Narrative->GetQuestManager()->GetQuests(Character);
                auto* Journal = Cast<UTextBlock>(Panel->GetWidgetFromName(TEXT("NarrativeQuestJournal")));
                if (Quests.Num() != 1 || Quests[0].Title.IsEmpty() || Quests[0].Objectives.Num() != 2
                    || !Journal || !Journal->GetText().ToString().Contains(Quests[0].Title.ToString()))
                    return Fail(TEXT("Real HTTP journal metadata did not reach the viewport"));
                if (!bRestore && Choice < 4 && !Journal->GetText().ToString().Contains(Quests[0].Objectives[0].Text.ToString()))
                    return Fail(TEXT("Real HTTP goal description missing from journal"));
            }
            if (bRestore || Choice == 4)
            {
                if (!Narrative->GetQuestManager()->HasRevision(Character, 4)
                    || !Narrative->GetQuestManager()->IsQuestCompleted(Character, TEXT("DEV.Introduction"))
                    || !Narrative->GetScenarioManager()->IsChapterCompleted(Character, TEXT("DEV.Prologue"))
                    || !Narrative->GetScenarioManager()->IsStoryUnlocked(Character, TEXT("DEV.Story.Next"))
                    || Narrative->GetDialogueManager()->GetAvailableDialogues().ContainsByPredicate([](const auto& D) { return !D.bInformational; }))
                    return Fail(TEXT("Completion or fresh-process restoration mismatch"));
                Test->AddInfo(FString(bNPC ? TEXT("World NPC HTTP persistence verified: ") : TEXT("Narrative HTTP persistence verified: ")) + (bRestore ? TEXT("restore") : TEXT("complete")));
                Panel->CloseScreen(); return true;
            }
            if (!Narrative->GetQuestManager()->HasRevision(Character, Choice)) return Fail(TEXT("Unexpected server revision"));
            if (!bCheckedClose)
            {
                if (!Click(Panel, TEXT("DEV.Contact.Offer"), FString())) return Fail(TEXT("Offer row missing"));
                Panel->CloseScreen();
                if (Narrative->HasPendingChoice() || !Narrative->GetQuestManager()->GetQuests(Character).IsEmpty())
                    return Fail(TEXT("Open/close changed quest state"));
                bCheckedClose = true; Stage = 2; return false;
            }
            const TCHAR* Nodes[] = {TEXT("DEV.Contact.Offer"), TEXT("DEV.Contact.Explain"), TEXT("DEV.Instructor.Explain"), TEXT("DEV.Contact.Report")};
            const TCHAR* Choices[] = {TEXT("accept"), TEXT("confirm"), TEXT("confirm"), TEXT("report")};
            if (!Click(Panel, Nodes[Choice], FString()) || !Click(Panel, Nodes[Choice], Choices[Choice]))
                return Fail(TEXT("Rendered dialogue/choice button missing or disabled"));
            if (!Narrative->IsBusy()) return Fail(TEXT("Choice button did not dispatch"));
            if (!Narrative->GetQuestManager()->HasRevision(Character, Choice)) return Fail(TEXT("Optimistic progress detected"));
            ++Choice; return false;
        }
    private:
        ARPGNarrativeNPC* DesiredNPC() const { return Choice == 2 ? InstructorNPC.Get() : ContactNPC.Get(); }
        bool OpenNPC()
        {
            if (CurrentNPC.IsValid()) if (auto* Panel = CurrentNPC->GetOpenDialogue(Controller)) Panel->CloseScreen();
            CurrentNPC = DesiredNPC();
            if (!CurrentNPC.IsValid()) return false;
            static_cast<IInteractionInterface*>(CurrentNPC.Get())->Interact(Controller);
            return CurrentNPC->GetOpenDialogue(Controller) != nullptr;
        }
        bool Fail(const FString& Error) { Test->AddError(Error); return true; }
        static bool Click(URPGNarrativeDialogueWidget* Panel, const FString& Node, const FString& Option)
        {
            auto* List = Cast<UVerticalBox>(Panel->GetWidgetFromName(TEXT("NarrativeActions")));
            if (!List) return false;
            for (auto* Child : List->GetAllChildren())
                if (auto* Row = Cast<URPGNarrativeActionWidget>(Child))
                    if (Row->GetDialogueId() == Node && Row->GetChoiceId() == Option && Row->GetIsEnabled())
                        if (auto* Button = Cast<UButton>(Row->GetWidgetFromName(TEXT("NarrativeActionButton"))))
                        { Button->OnClicked.Broadcast(); return true; }
            return false;
        }
        FAutomationTestBase* Test;
        FGuid Character;
        FString Steam, WorldName, DungeonPhase;
        bool bRestore = false, bCheckedClose = false, bInformationChecked = false;
        const bool bNPC = FParse::Param(FCommandLine::Get(), TEXT("RPGNarrativeNPC"));
        TWeakObjectPtr<ARPGNarrativeNPC> ContactNPC, InstructorNPC, CurrentNPC;
        TWeakObjectPtr<APawn> SpawnedPawn;
        double Deadline = FPlatformTime::Seconds() + 90;
        int32 Stage = 0, Choice = 0;
        TStrongObjectPtr<UGameInstance> Instance;
        TStrongObjectPtr<URPGOnlineLobbyWidget> Lobby;
        UWorld* World = nullptr;
        ULocalPlayer* Player = nullptr;
        APlayerController* Controller = nullptr;
        UHttpWebManager* Web = nullptr;
        URPGDungeonSessionSubsystem* Flow = nullptr;
        URPGNarrativeCoordinator* Narrative = nullptr;
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeHttpDialoguePersistenceTest, "ProjectRPG.Narrative.HttpDialoguePersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FRPGNarrativeHttpDialoguePersistenceTest::RunTest(const FString&)
{
    FString Phase, Id, Steam, World;
    if (!FParse::Value(FCommandLine::Get(), TEXT("RPGNarrativePhase="), Phase))
    { AddInfo(TEXT("Skipped: use Run-NarrativeDialogueE2E.ps1 with disposable storage.")); return true; }
    FGuid Character;
    if (!FParse::Param(FCommandLine::Get(), TEXT("nosteam")) || !URPGOnlineLobbyWidget::IsDevelopmentUIEnabled()
        || (Phase != TEXT("complete") && Phase != TEXT("restore") && Phase != TEXT("dungeon") && Phase != TEXT("dungeonrestore"))
        || !FParse::Value(FCommandLine::Get(), TEXT("RPGNarrativeCharacter="), Id) || !FGuid::Parse(Id, Character)
        || !FParse::Value(FCommandLine::Get(), TEXT("RPGNarrativeSteam="), Steam)
        || !FParse::Param(FCommandLine::Get(), TEXT("game"))
        || !FParse::Value(FCommandLine::Get(), TEXT("RPGNarrativeWorld="), World) || World != TEXT("/Engine/Maps/Entry"))
    { AddError(TEXT("Invalid isolated narrative context")); return false; }
    ADD_LATENT_AUTOMATION_COMMAND(RPGNarrativeTests::FHttpDialogueCommand(this, Character, Steam, World,
        Phase == TEXT("restore"), Phase.StartsWith(TEXT("dungeon")) ? Phase : FString()));
    return true;
}
#endif
