#if WITH_DEV_AUTOMATION_TESTS
#include "Narrative/RPGNarrativeTranscript.h"
#include "UI/Narrative/RPGNarrativeTranscriptPanel.h"
#include "Misc/AutomationTest.h"
#include "Components/Button.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
struct FRPGNarrativeTranscriptTestAccess { static void Destruct(URPGNarrativeTranscriptPanel* P) { P->NativeDestruct(); } };
namespace RPGNarrativeTranscriptTests
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
const FGuid TranscriptCharacter(1,2,3,4), AlternateCharacter(5,6,7,8);
struct FFixture
{
    bool bValid=true;
    FRPGNarrativeTranscriptContext Current;
    TSharedPtr<FRPGNarrativeTranscript> Model;
    FFixture()
    {
        Current.Character=TranscriptCharacter;Current.Account=TEXT("player");Current.Endpoint=TEXT("http://owner/api");Current.bHasActive=true;
        Current.Active.Id=TEXT("Internal.Node");Current.Active.Speaker=FText::FromString(TEXT("안내인"));Current.Active.Text=FText::FromString(TEXT("읽은 대사"));
        Model=MakeShared<FRPGNarrativeTranscript>([this](auto& Out){Out=Current;return bValid;});
    }
    bool Capture(){return Model->Capture(Current.Character,Current.Active.Id);}
};
void Click(URPGNarrativeTranscriptPanel* P,const TCHAR* Name){if(auto* B=Cast<UButton>(P->GetWidgetFromName(Name)))B->OnClicked.Broadcast();}
}
using namespace RPGNarrativeTranscriptTests;
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTranscriptCaptureTest,"ProjectRPG.Narrative.Transcript.Capture",Flags)
bool FRPGTranscriptCaptureTest::RunTest(const FString&)
{
    FFixture F;TArray<FRPGNarrativeTranscriptLine> Lines;
    TestTrue(TEXT("Empty history is valid"),F.Model->Read(TranscriptCharacter,Lines));TestEqual(TEXT("No catalog auto-capture"),Lines.Num(),0);
    TestFalse(TEXT("Wrong node cannot log unseen text"),F.Model->Capture(TranscriptCharacter,TEXT("Internal.Unseen")));
    F.Current.bHasActive=false;TestFalse(TEXT("No readable active dialogue"),F.Capture());F.Current.bHasActive=true;
    TestTrue(TEXT("Explicit displayed node capture"),F.Capture());TestFalse(TEXT("Consecutive render/tick deduplicated"),F.Capture());
    F.Model->Read(TranscriptCharacter,Lines);TestEqual(TEXT("One text line"),Lines.Num(),1);
    TestEqual(TEXT("Speaker retained"),Lines[0].Speaker.ToString(),FString(TEXT("안내인")));
    F.Current.Active.Id=TEXT("Internal.Next");F.Current.Active.Text=FText::FromString(TEXT("다음 대사"));TestTrue(TEXT("Next shown line"),F.Capture());
    F.Current.Active.Text=FText::FromString(TEXT("수정된 대사"));TestTrue(TEXT("Updated visible text is retained"),F.Capture());
    F.Current.bHasActive=false;TestTrue(TEXT("Closing active dialogue keeps read log"),F.Model->Read(TranscriptCharacter,Lines));TestEqual(TEXT("Three shown lines"),Lines.Num(),3);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTranscriptScopeTest,"ProjectRPG.Narrative.Transcript.Scope",Flags)
bool FRPGTranscriptScopeTest::RunTest(const FString&)
{
    FFixture F;TArray<FRPGNarrativeTranscriptLine> Lines;F.Capture();
    F.Current.Character=AlternateCharacter;TestFalse(TEXT("Old window cannot read new character"),F.Model->Read(TranscriptCharacter,Lines));
    TestTrue(TEXT("New character has own empty log"),F.Model->Read(AlternateCharacter,Lines));TestEqual(TEXT("No cross-character lines"),Lines.Num(),0);F.Capture();
    F.Model->Clear(AlternateCharacter);F.Current.Character=TranscriptCharacter;F.Model->Read(TranscriptCharacter,Lines);TestEqual(TEXT("Clearing other character preserves original"),Lines.Num(),1);
    F.Current.Account=TEXT("other");F.Model->Read(TranscriptCharacter,Lines);TestEqual(TEXT("Account isolation"),Lines.Num(),0);
    F.Model->Clear(TranscriptCharacter);F.Current.Account=TEXT("player");F.Model->Read(TranscriptCharacter,Lines);TestEqual(TEXT("Different account cannot clear old log"),Lines.Num(),1);
    F.Current.Endpoint+=TEXT("/other");F.Model->Read(TranscriptCharacter,Lines);TestEqual(TEXT("Endpoint isolation"),Lines.Num(),0);
    F.Current.Endpoint=TEXT("http://owner/api");F.bValid=false;
    TestFalse(TEXT("Auth loss hides log"),F.Model->Read(TranscriptCharacter,Lines));TestEqual(TEXT("Output cleared on invalid read"),Lines.Num(),0);
    TestFalse(TEXT("Auth loss blocks capture"),F.Capture());TestFalse(TEXT("Auth loss blocks clear"),F.Model->Clear(TranscriptCharacter));
    F.bValid=true;F.Model->Read(TranscriptCharacter,Lines);TestEqual(TEXT("Same owner recovery"),Lines.Num(),1);
    F.Model->Reset();F.Model->Read(TranscriptCharacter,Lines);TestEqual(TEXT("Shutdown forgets text"),Lines.Num(),0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTranscriptBoundsTest,"ProjectRPG.Narrative.Transcript.Bounds",Flags)
bool FRPGTranscriptBoundsTest::RunTest(const FString&)
{
    FFixture F;TArray<FRPGNarrativeTranscriptLine> Lines;
    for(int32 I=0;I<100;++I){F.Current.Active.Id=FString::Printf(TEXT("N.%d"),I);F.Current.Active.Text=FText::FromString(FString::FromInt(I));F.Capture();}
    F.Model->Read(TranscriptCharacter,Lines);TestEqual(TEXT("Bounded entry count"),Lines.Num(),FRPGNarrativeTranscript::MaximumLines);
    TestEqual(TEXT("Oldest trimmed"),Lines[0].Text.ToString(),FString(TEXT("36")));
    for(int32 I=0;I<100;++I){F.Current.Active.Id=FString::Printf(TEXT("L.%d"),I);F.Current.Active.Text=FText::FromString(FString::ChrN(4096,TCHAR('a')));F.Capture();}
    F.Model->Read(TranscriptCharacter,Lines);int32 Total=0;for(const auto& Line:Lines)Total+=Line.Speaker.ToString().Len()+Line.Text.ToString().Len();
    TestTrue(TEXT("Bounded text budget"),Total<=FRPGNarrativeTranscript::MaximumCharacters);
    F.Current.Active.Text=FText::FromString(FString::ChrN(4097,TCHAR('a')));TestFalse(TEXT("Oversized line rejected"),F.Capture());
    F.Model->Clear(TranscriptCharacter);F.Model->Read(TranscriptCharacter,Lines);TestEqual(TEXT("Clear empties bounded log"),Lines.Num(),0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTranscriptPanelTest,"ProjectRPG.Narrative.Transcript.Panel",Flags)
bool FRPGTranscriptPanelTest::RunTest(const FString&)
{
    FFixture F;F.Capture();TStrongObjectPtr<UGameInstance> Game(NewObject<UGameInstance>(GEngine));
    Game->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/Transcript_%s"),*FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    UWorld* World=Game->GetWorld();if(!TestNotNull(TEXT("Private world"),World))return false;
    {
        TStrongObjectPtr<URPGNarrativeTranscriptPanel> Panel(CreateWidget<URPGNarrativeTranscriptPanel>(Game.Get()));Panel->Configure(F.Model,TranscriptCharacter);const auto Slate=Panel->TakeWidget();
        TestTrue(TEXT("Log closed initially"),Panel->GetDisplayedTranscript().IsEmpty());Click(Panel.Get(),TEXT("OpenTranscript"));
        const auto Text=Panel->GetDisplayedTranscript().ToString();TestTrue(TEXT("Read line visible"),Text.Contains(TEXT("안내인: 읽은 대사")));TestFalse(TEXT("Internal node hidden"),Text.Contains(TEXT("Internal.")));
        Click(Panel.Get(),TEXT("CloseTranscript"));TestTrue(TEXT("Hide is not clear"),Panel->GetDisplayedTranscript().IsEmpty());
        FRPGNarrativeTranscriptTestAccess::Destruct(Panel.Get());
        TStrongObjectPtr<URPGNarrativeTranscriptPanel> Reopened(CreateWidget<URPGNarrativeTranscriptPanel>(Game.Get()));Reopened->Configure(F.Model,TranscriptCharacter);const auto ReopenedSlate=Reopened->TakeWidget();
        Click(Reopened.Get(),TEXT("OpenTranscript"));TestTrue(TEXT("Reopen retains session text"),Reopened->GetDisplayedTranscript().ToString().Contains(TEXT("읽은 대사")));
        F.bValid=false;TestTrue(TEXT("Invalid owner hides immediately"),Reopened->GetDisplayedTranscript().IsEmpty());Click(Reopened.Get(),TEXT("ClearTranscript"));
        F.bValid=true;Click(Reopened.Get(),TEXT("OpenTranscript"));TestTrue(TEXT("Invalid clear preserved text"),Reopened->GetDisplayedTranscript().ToString().Contains(TEXT("읽은 대사")));
        Click(Reopened.Get(),TEXT("ClearTranscript"));TestTrue(TEXT("Explicit clear gives empty state"),Reopened->GetDisplayedTranscript().ToString().Contains(TEXT("없습니다")));
        TestEqual(TEXT("Review never changed active dialogue"),F.Current.Active.Id,FString(TEXT("Internal.Node")));
        FRPGNarrativeTranscriptTestAccess::Destruct(Reopened.Get());
    }
    Game->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}
#endif
