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
            TEXT("[{\"questId\":\"DEV.Introduction\",\"definitionVersion\":1,\"status\":\"%s\",\"completedObjectives\":%s}]"),
            Revision == 4 ? TEXT("Completed") : Revision == 3 ? TEXT("ReadyToReport") : TEXT("Active"),
            Revision >= 3 ? TEXT("[\"contact\",\"preparation\"]") : Revision == 2 ? TEXT("[\"contact\"]") : TEXT("[]"));
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
        TFunction<void(FRPGNarrativeHttpResult)> Pending;
        bool GetContext(FString& Endpoint, FString& SteamId) const override
        {
            Endpoint = TEXT("http://127.0.0.1:5000/api"); SteamId = Account; return !Account.IsEmpty();
        }
        bool Send(FGuid, const FString& Body, TFunction<void(FRPGNarrativeHttpResult)> Completion) override
        {
            LastBody = Body; Pending = MoveTemp(Completion); return true;
        }
        void Cancel() override { Pending = nullptr; }
        void Complete(const FString& Body, const int32 Status = 200)
        {
            auto Callback = MoveTemp(Pending);
            FRPGNarrativeHttpResult Result; Result.bCurrentPlayerContext = true;
            Result.bTransportSuccessful = Status != 0; Result.StatusCode = Status; Result.Body = Body;
            Callback(MoveTemp(Result));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeCodecTest, "ProjectRPG.Narrative.Codec",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGNarrativeCodecTest::RunTest(const FString&)
{
    using namespace RPGNarrativeTests;
    FRPGNarrativeSnapshot Parsed;
    TestTrue(TEXT("Backend contract decodes"), FRPGNarrativeCodec::Decode(Snapshot(4), Character, Parsed));
    TestEqual(TEXT("Completed status"), Parsed.Quests[0].Status, ERPGQuestStatus::Completed);
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
#endif
