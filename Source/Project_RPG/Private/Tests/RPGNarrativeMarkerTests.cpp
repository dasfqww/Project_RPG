#if WITH_DEV_AUTOMATION_TESTS
#include "Narrative/RPGNarrativeNPCMarker.h"
#include "UI/Narrative/RPGNarrativeNPCMarkerWidget.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformMisc.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace RPGNarrativeMarkerTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
const FGuid WireCharacter(1,2,3,4);
FString Wire(const FString& Metadata, const TCHAR* Status=TEXT("ReadyToReport"))
{
    const bool Active=FCString::Strcmp(Status,TEXT("Active"))==0;
    return FString::Printf(TEXT(R"({"characterId":"%s","schemaVersion":1,"catalogVersion":1,"revision":"9007199254740993","completedChapters":[],"storyUnlocks":[],"quests":[{"questId":"Q","definitionVersion":1,"status":"%s","completedObjectives":%s,"objectives":[{"id":"talk","text":"Talk","completed":%s}]}],"availableDialogues":[{"id":"Report","speaker":"Guide","text":"Hidden spoiler","choices":[{"id":"go","text":"Go"}]%s}]})"),
        *WireCharacter.ToString(),Status,Active?TEXT("[]"):TEXT("[\"talk\"]"),Active?TEXT("false"):TEXT("true"),*Metadata);
}
FRPGDialogueNode Node(const TCHAR* Id, ERPGNarrativeQuestAction Action)
{
    FRPGDialogueNode N;N.Id=Id;N.QuestId=TEXT("Q");N.QuestAction=Action;N.Choices.AddDefaulted();return N;
}
}
using namespace RPGNarrativeMarkerTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMarkerCodecTest,"ProjectRPG.Narrative.Marker.Codec",Flags)
bool FRPGMarkerCodecTest::RunTest(const FString&)
{
    FRPGNarrativeSnapshot View;
    TestTrue(TEXT("Report metadata"),FRPGNarrativeCodec::Decode(Wire(TEXT(",\"questId\":\"Q\",\"questAction\":\"Report\"")),WireCharacter,View));
    TestTrue(TEXT("Report retained"),View.AvailableDialogues[0].QuestAction==ERPGNarrativeQuestAction::Report);
    TestTrue(TEXT("Progress metadata"),FRPGNarrativeCodec::Decode(Wire(TEXT(",\"questId\":\"Q\",\"questAction\":\"Progress\""),TEXT("Active")),WireCharacter,View));
    for(const FString Meta : {FString(),FString(TEXT(",\"questId\":null,\"questAction\":null"))})
    {
        TestTrue(TEXT("Legacy metadata absence/null"),FRPGNarrativeCodec::Decode(Wire(Meta),WireCharacter,View));
        TestTrue(TEXT("Legacy never infers Report from node name"),View.AvailableDialogues[0].QuestAction==ERPGNarrativeQuestAction::None);
    }
    for(const TCHAR* Meta : {TEXT(",\"questId\":\"Q\""),TEXT(",\"questAction\":\"Report\""),TEXT(",\"questId\":null,\"questAction\":\"Report\""),
        TEXT(",\"questId\":1,\"questAction\":\"Report\""),TEXT(",\"questId\":\"Q\",\"questAction\":1"),TEXT(",\"questId\":\"Q\",\"questAction\":\"report\""),
        TEXT(",\"questId\":\"Q\",\"questAction\":\"Unknown\""),TEXT(",\"questId\":\"Q\",\"questAction\":\"Accept\""),
        TEXT(",\"questId\":\"Q\",\"questAction\":\"Progress\""),TEXT(",\"questId\":\"Other\",\"questAction\":\"Report\""),
        TEXT(",\"questId\":\"Q\",\"questAction\":\"Report\",\"informational\":true")})
    {
        TestFalse(FString(TEXT("Invalid metadata rejected: "))+Meta,FRPGNarrativeCodec::Decode(Wire(Meta),WireCharacter,View));
        TestEqual(TEXT("Invalid response preserves snapshot revision"),View.Revision,9007199254740993LL);
        TestTrue(TEXT("Invalid response preserves prior node"),View.AvailableDialogues[0].QuestAction==ERPGNarrativeQuestAction::None);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMarkerPriorityTest,"ProjectRPG.Narrative.Marker.Priority",Flags)
bool FRPGMarkerPriorityTest::RunTest(const FString&)
{
    TArray<FRPGDialogueNode> Nodes{Node(TEXT("Report"),ERPGNarrativeQuestAction::Report),Node(TEXT("Offer"),ERPGNarrativeQuestAction::Accept),Node(TEXT("Talk"),ERPGNarrativeQuestAction::Progress),Node(TEXT("Info"),ERPGNarrativeQuestAction::None)};
    TestTrue(TEXT("Report first"),FRPGNarrativeNPCMarker::Resolve({TEXT("Info"),TEXT("Talk"),TEXT("Offer"),TEXT("Report")},Nodes)==ERPGNarrativeNPCMarker::Report);
    TestTrue(TEXT("Offer before progress"),FRPGNarrativeNPCMarker::Resolve({TEXT("Talk"),TEXT("Offer")},Nodes)==ERPGNarrativeNPCMarker::Accept);
    TestTrue(TEXT("Progress before info"),FRPGNarrativeNPCMarker::Resolve({TEXT("Info"),TEXT("Talk")},Nodes)==ERPGNarrativeNPCMarker::Progress);
    TestTrue(TEXT("Legacy is generic dialogue"),FRPGNarrativeNPCMarker::Resolve({TEXT("Info")},Nodes)==ERPGNarrativeNPCMarker::Dialogue);
    TestTrue(TEXT("NPC mapping exact"),FRPGNarrativeNPCMarker::Resolve({TEXT("report")},Nodes)==ERPGNarrativeNPCMarker::None);
    Nodes[0].bEntryPoint=false;Nodes[1].Choices.Reset();
    TestTrue(TEXT("Hidden branches/no choices excluded"),FRPGNarrativeNPCMarker::Resolve({TEXT("Report"),TEXT("Offer")},Nodes)==ERPGNarrativeNPCMarker::None);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMarkerWireTest,"ProjectRPG.Narrative.Marker.HttpWire",Flags)
bool FRPGMarkerWireTest::RunTest(const FString&)
{
    FString Body;const auto Path=FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_NARRATIVE_MARKER_WIRE"));
    if(!TestTrue(TEXT("Actual HTTP fixture supplied"),!Path.IsEmpty()&&FFileHelper::LoadFileToString(Body,*Path)))return false;
    TSharedPtr<FJsonObject> Root;FString Id;FGuid OwnerId;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),Root)||!Root||!Root->TryGetStringField(TEXT("characterId"),Id)||!FGuid::Parse(Id,OwnerId))return false;
    FRPGNarrativeSnapshot View;if(!TestTrue(TEXT("Actual HTTP decodes"),FRPGNarrativeCodec::Decode(Body,OwnerId,View)))return false;
    TestTrue(TEXT("Actual report NPC"),FRPGNarrativeNPCMarker::Resolve({TEXT("Report")},View.AvailableDialogues)==ERPGNarrativeNPCMarker::Report);
    TestTrue(TEXT("Other NPC has no marker"),FRPGNarrativeNPCMarker::Resolve({TEXT("Offer")},View.AvailableDialogues)==ERPGNarrativeNPCMarker::None);
    TestTrue(TEXT("Actual informational NPC"),FRPGNarrativeNPCMarker::Resolve({TEXT("Info")},View.AvailableDialogues)==ERPGNarrativeNPCMarker::Dialogue);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMarkerWidgetTest,"ProjectRPG.Narrative.Marker.Widget",Flags)
bool FRPGMarkerWidgetTest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> Game(NewObject<UGameInstance>(GEngine));Game->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/Marker_%s"),*FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    UWorld* World=Game->GetWorld();if(!TestNotNull(TEXT("Private world"),World))return false;
    {
        bool VisibleA=true,VisibleB=true;TArray<FRPGDialogueNode> A{Node(TEXT("NPC"),ERPGNarrativeQuestAction::Report)},B{Node(TEXT("NPC"),ERPGNarrativeQuestAction::Accept)};
        TStrongObjectPtr<URPGNarrativeNPCMarkerWidget> First(CreateWidget<URPGNarrativeNPCMarkerWidget>(Game.Get())),Second(CreateWidget<URPGNarrativeNPCMarkerWidget>(Game.Get()));
        First->Configure([&](auto& Entries,auto& Nodes){Entries={TEXT("NPC")};Nodes=A;return VisibleA;});const auto FirstSlate=First->TakeWidget();
        Second->Configure([&](auto& Entries,auto& Nodes){Entries={TEXT("NPC")};Nodes=B;return VisibleB;});const auto SecondSlate=Second->TakeWidget();
        TestTrue(TEXT("First player's report"),First->GetDisplayedMarker().ToString().Contains(TEXT("보고 가능")));
        TestTrue(TEXT("Second player's own offer"),Second->GetDisplayedMarker().ToString().Contains(TEXT("새 의뢰")));
        VisibleA=false;TestTrue(TEXT("Owner/auth/readability failure hides first"),First->GetDisplayedMarker().IsEmpty());
        TestTrue(TEXT("Other local view unaffected"),Second->GetDisplayedMarker().ToString().Contains(TEXT("새 의뢰")));
        VisibleA=true;A[0].QuestAction=ERPGNarrativeQuestAction::Progress;
        TestTrue(TEXT("New validated snapshot refresh"),First->GetDisplayedMarker().ToString().Contains(TEXT("의뢰 진행")));
        A.Reset();TestTrue(TEXT("No eligible node/skip hides marker"),First->GetDisplayedMarker().IsEmpty());
        TestTrue(TEXT("Marker does not intercept input"),Second->GetVisibility()==ESlateVisibility::HitTestInvisible);
    }
    Game->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}
#endif
