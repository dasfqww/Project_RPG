#if WITH_DEV_AUTOMATION_TESTS
#include "Narrative/RPGNarrativeTypes.h"
#include "Narrative/RPGNarrativePresentation.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformMisc.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"

namespace RPGNarrativeCategoryTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
const FGuid Character(0x11111111,0x22222222,0x33333333,0x44444444);
FString Wire(const FString& Category)
{
    return FString::Printf(TEXT(R"({"characterId":"%s","schemaVersion":1,"catalogVersion":1,"revision":"9007199254740993","completedChapters":[],"storyUnlocks":[],"quests":[{"questId":"Q","definitionVersion":1,"status":"Active","completedObjectives":[],"title":"Title","objectives":[{"id":"talk","text":"Talk","completed":false}]%s}],"availableDialogues":[]})"),*Character.ToString(),*Category);
}
}
using namespace RPGNarrativeCategoryTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeCategoryDecodeTest,"ProjectRPG.Narrative.Category.Decode",Flags)
bool FRPGNarrativeCategoryDecodeTest::RunTest(const FString&)
{
    FRPGNarrativeSnapshot View;
    for(const auto& Pair : {TPair<FString,ERPGQuestCategory>(TEXT("Main"),ERPGQuestCategory::Main),
        {TEXT("Side"),ERPGQuestCategory::Side},{TEXT("Unspecified"),ERPGQuestCategory::Unspecified}})
    {
        if(!TestTrue(TEXT("Canonical category"),FRPGNarrativeCodec::Decode(Wire(TEXT(",\"category\":\"")+Pair.Key+TEXT("\"")),Character,View)))return false;
        TestTrue(TEXT("Category retained"),View.Quests[0].Category==Pair.Value);
        TestEqual(TEXT("Exact revision retained"),View.Revision,9007199254740993LL);
    }
    for(const FString Field : {FString(),FString(TEXT(",\"category\":null"))})
    {
        TestTrue(TEXT("Legacy accepted"),FRPGNarrativeCodec::Decode(Wire(Field),Character,View));
        TestTrue(TEXT("Legacy remains unclassified"),View.Quests[0].Category==ERPGQuestCategory::Unspecified);
    }
    for(const TCHAR* Value : {TEXT("1"),TEXT("true"),TEXT("{}"),TEXT("[]"),TEXT("\"\""),TEXT("\"side\""),TEXT("\"Unknown\"")})
    {
        const int64 RevisionBefore=View.Revision;const auto CategoryBefore=View.Quests[0].Category;
        TestFalse(FString(TEXT("Invalid category rejected: "))+Value,FRPGNarrativeCodec::Decode(Wire(FString(TEXT(",\"category\":"))+Value),Character,View));
        TestEqual(TEXT("Rejected response preserves revision"),View.Revision,RevisionBefore);
        TestTrue(TEXT("Rejected response preserves view"),View.Quests[0].Category==CategoryBefore);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeCategoryHeadingTest,"ProjectRPG.Narrative.Category.Heading",Flags)
bool FRPGNarrativeCategoryHeadingTest::RunTest(const FString&)
{
    FRPGQuestProgress Quest;Quest.QuestId=TEXT("Private.Internal.Id");Quest.Title=FText::FromString(TEXT("Authored title"));
    for(auto Category : {ERPGQuestCategory::Unspecified,ERPGQuestCategory::Main,ERPGQuestCategory::Side})
        for(auto Status : {ERPGQuestStatus::Active,ERPGQuestStatus::ReadyToReport,ERPGQuestStatus::Completed})
        {
            Quest.Category=Category;Quest.Status=Status;
            const FString Text=FRPGNarrativePresentation::QuestHeading(Quest).ToString();
            TestTrue(TEXT("Authored title visible"),Text.Contains(TEXT("Authored title")));
            TestFalse(TEXT("Internal ID hidden"),Text.Contains(Quest.QuestId));
            TestEqual(TEXT("Main label"),Text.Contains(TEXT("메인 의뢰")),Category==ERPGQuestCategory::Main);
            TestEqual(TEXT("Optional label"),Text.Contains(TEXT("선택 서브 의뢰")),Category==ERPGQuestCategory::Side);
            const TCHAR* Label=Status==ERPGQuestStatus::Active?TEXT("진행 중"):Status==ERPGQuestStatus::ReadyToReport?TEXT("보고 가능"):TEXT("완료");
            TestTrue(TEXT("Status preserved"),Text.Contains(Label));
        }
    Quest.Title=FText::GetEmpty();Quest.Category=ERPGQuestCategory::Unspecified;
    TestTrue(TEXT("Legacy fallback"),FRPGNarrativePresentation::QuestHeading(Quest).ToString().StartsWith(TEXT("의뢰 · ")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeCategoryHttpWireTest,"ProjectRPG.Narrative.Category.HttpWire",Flags)
bool FRPGNarrativeCategoryHttpWireTest::RunTest(const FString&)
{
    FString Body;const FString Path=FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_NARRATIVE_CATEGORY_WIRE"));
    if(!TestTrue(TEXT("Actual backend HTTP fixture supplied"),!Path.IsEmpty()&&FFileHelper::LoadFileToString(Body,*Path)))return false;
    TSharedPtr<FJsonObject> Root;FString Id;FGuid OwnerId;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),Root)||!Root||!Root->TryGetStringField(TEXT("characterId"),Id)||!FGuid::Parse(Id,OwnerId))return false;
    FRPGNarrativeSnapshot View;
    if(!TestTrue(TEXT("Actual server response decodes"),FRPGNarrativeCodec::Decode(Body,OwnerId,View)))return false;
    TestEqual(TEXT("Three actual categories"),View.Quests.Num(),3);
    for(const auto& Quest : View.Quests)
    {
        const auto Expected=Quest.QuestId==TEXT("Main")?ERPGQuestCategory::Main:Quest.QuestId==TEXT("Side")?ERPGQuestCategory::Side:ERPGQuestCategory::Unspecified;
        TestTrue(TEXT("Actual category maps to UE"),Quest.Category==Expected);
        TestTrue(TEXT("Actual title maps to journal"),FRPGNarrativePresentation::QuestHeading(Quest).ToString().Contains(Quest.Title.ToString()));
    }
    return true;
}
#endif
