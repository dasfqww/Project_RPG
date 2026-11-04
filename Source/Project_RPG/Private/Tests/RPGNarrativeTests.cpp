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

struct FRPGNarrativeTestAccess
{
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
        TFunction<void(FRPGNarrativeHttpResult)> Pending;
        bool GetContext(FString& Endpoint, FString& SteamId) const override
        {
            Endpoint = TEXT("http://127.0.0.1:5000/api"); SteamId = Account; return !Account.IsEmpty();
        }
        bool Send(FGuid, const FString& Body, TFunction<void(FRPGNarrativeHttpResult)> Completion) override
        {
            ++Sends; LastBody = Body; Pending = MoveTemp(Completion); return true;
        }
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
        bool Init()
        {
            Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/Narrative_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
            World = Instance->GetWorld();
            if (!World) return false;
            Viewport = NewObject<UGameViewportClient>(GEngine);
            Instance->GetWorldContext()->GameViewport = Viewport;
            Viewport->Init(*Instance->GetWorldContext(), Instance.Get(), false);
            FString Error; Player = Instance->CreateLocalPlayer(0, Error, false);
            auto* Web = Instance->GetSubsystem<UHttpWebManager>();
            if (!Player || !Web) return false;
            FRPGNarrativeTestAccess::Login(Web);
            return Player->GetSubsystem<URPGNarrativeCoordinator>() && Player->GetSubsystem<URPGDialogueManager>()
                && Instance->GetSubsystem<URPGQuestManager>() && Instance->GetSubsystem<URPGScenarioManager>();
        }
        ~FGameFixture()
        {
            if (!World) return;
            while (Instance->GetLocalPlayers().Num()) Instance->RemoveLocalPlayer(Instance->GetLocalPlayers()[0]);
            Instance->Shutdown();
            if (Viewport) Viewport->DetachViewportClient();
            GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
        }
    };
}

namespace RPGNarrativeTests
{
    FString InformationSnapshot(bool bBrokenLink = false)
    {
        TSharedPtr<FJsonObject> Root, Information;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Snapshot(0)), Root);
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
    TestTrue(TEXT("Open exact character"), Panel->OpenForCharacter(Character));
    const TSharedRef<SWidget> PanelSlate = Panel->TakeWidget();
    Transport->Complete(Snapshot(0));
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
    Panel->CloseScreen(); Panel->OpenForCharacter(Character);
    auto* Retry = Cast<UButton>(Panel->GetWidgetFromName(TEXT("NarrativeRetry")));
    if (!TestNotNull(TEXT("Uncertain result has retry control"), Retry)) return false;
    Retry->OnClicked.Broadcast();
    TestEqual(TEXT("Widget retry retains the original command"), Transport->LastBody, Intent);
    if (!TestTrue(TEXT("Retry POST callback exists"), Transport->Complete(Snapshot(1)))) return false;
    TestEqual(TEXT("Server approval populates progress"), Flow->GetQuestManager()->GetQuests(Character).Num(), 1);
    auto* Journal = Cast<UTextBlock>(Panel->GetWidgetFromName(TEXT("NarrativeQuestJournal")));
    if (!TestNotNull(TEXT("Quest journal control"), Journal)) return false;
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
namespace RPGNarrativeTests
{
    // Opt-in real HTTP test. The runner supplies only nonsecret IDs and a private
    // loopback configuration; no test transport or state injection is used here.
    class FHttpDialogueCommand final : public IAutomationLatentCommand
    {
    public:
        FHttpDialogueCommand(FAutomationTestBase* InTest, FGuid InCharacter, FString InSteam,
            FString InWorld, bool bInRestore)
            : Test(InTest), Character(InCharacter), Steam(MoveTemp(InSteam)), WorldName(MoveTemp(InWorld)), bRestore(bInRestore) {}
        ~FHttpDialogueCommand()
        {
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
                Lobby.Reset(CreateWidget<URPGOnlineLobbyWidget>(Controller));
                if (!Lobby.IsValid() || Lobby->GetOwningLocalPlayer() != Player)
                    return Fail(FString::Printf(TEXT("Created lobby context mismatch: exists=%d initialized=%d valid=%d"),
                        Lobby.IsValid(), Lobby.IsValid() && Lobby->GetPlayerContext().IsInitialized(), Lobby.IsValid() && Lobby->GetPlayerContext().IsValid()));
                if (!Lobby.IsValid() || !Lobby->AddToPlayerScreen()) return Fail(TEXT("Lobby viewport unavailable"));
                Stage = 2; return false;
            }
            if (Stage == 2)
            {
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
            auto* Panel = Lobby->GetNarrativeDialogueWidget();
            if (!Panel || !Panel->IsInViewport()) return Fail(TEXT("Story panel closed unexpectedly"));
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
                Test->AddInfo(FString(TEXT("Narrative HTTP persistence verified: ")) + (bRestore ? TEXT("restore") : TEXT("complete")));
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
        FString Steam, WorldName;
        bool bRestore = false, bCheckedClose = false, bInformationChecked = false;
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
        || (Phase != TEXT("complete") && Phase != TEXT("restore"))
        || !FParse::Value(FCommandLine::Get(), TEXT("RPGNarrativeCharacter="), Id) || !FGuid::Parse(Id, Character)
        || !FParse::Value(FCommandLine::Get(), TEXT("RPGNarrativeSteam="), Steam)
        || !FParse::Param(FCommandLine::Get(), TEXT("game"))
        || !FParse::Value(FCommandLine::Get(), TEXT("RPGNarrativeWorld="), World) || World != TEXT("/Engine/Maps/Entry"))
    { AddError(TEXT("Invalid isolated narrative context")); return false; }
    ADD_LATENT_AUTOMATION_COMMAND(RPGNarrativeTests::FHttpDialogueCommand(this, Character, Steam, World, Phase == TEXT("restore")));
    return true;
}
#endif
