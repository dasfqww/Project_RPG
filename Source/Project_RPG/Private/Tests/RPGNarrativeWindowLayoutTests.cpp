#if WITH_DEV_AUTOMATION_TESTS
#include "UI/Narrative/RPGNarrativeWindowLayout.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Layout/ArrangedChildren.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "Widgets/SWidget.h"
#include <limits>

namespace RPGNarrativeWindowLayoutTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
bool Find(const TSharedRef<SWidget>& Widget, const FGeometry& Geometry, const TSharedPtr<SWidget>& Target, FGeometry& Out, int32 Depth = 0)
{
    if (Widget.operator->() == Target.Get()) { Out = Geometry; return true; }
    if (Depth >= 40) return false;
    FArrangedChildren Children(EVisibility::All); Widget->ArrangeChildren(Geometry, Children);
    for (int32 I = 0; I < Children.Num(); ++I)
        if (Find(Children[I].Widget, Children[I].Geometry, Target, Out, Depth + 1)) return true;
    return false;
}
struct FFixture
{
    TStrongObjectPtr<UWidgetTree> Tree{NewObject<UWidgetTree>(GetTransientPackage())};
    FRPGNarrativeWindowFrame Frame;
    UTextBlock* Body = nullptr;
    UButton* Refresh = nullptr;
    TArray<UButton*> Buttons;
    TSharedPtr<SWidget> Slate;
    FFixture()
    {
        Frame = FRPGNarrativeWindowLayout::Build(*Tree);
        auto Label = [&](UVerticalBox* Parent, const FString& Text)
        {
            auto* T = Tree->ConstructWidget<UTextBlock>(); T->SetText(FText::FromString(Text));
            T->SetAutoWrapText(true); Parent->AddChildToVerticalBox(T)->SetPadding(FMargin(0, 5)); return T;
        };
        Label(Frame.Header, TEXT("인물과 이야기")); Label(Frame.Content, TEXT("화자"));
        FString LongText; for (int32 I = 0; I < 160; ++I) LongText += TEXT("A long narrative line.\n");
        Body = Label(Frame.Content, LongText); Label(Frame.Content, TEXT("현재 목표")); Label(Frame.Content, TEXT("진행 결과 확인"));
        for (float Height : {120.f, 180.f})
        {
            auto* Extra = Tree->ConstructWidget<USizeBox>(); Extra->SetHeightOverride(Height);
            Frame.Content->AddChildToVerticalBox(Extra);
        }
        Refresh = Tree->ConstructWidget<UButton>(); auto* RefreshText = Tree->ConstructWidget<UTextBlock>();
        RefreshText->SetText(FText::FromString(TEXT("대화 다시 확인"))); Refresh->SetContent(RefreshText);
        Frame.Content->AddChildToVerticalBox(Refresh)->SetPadding(FMargin(0,4));
        for (const TCHAR* Caption : {TEXT("진행 결과 다시 확인"), TEXT("닫기")})
        {
            auto* B = Tree->ConstructWidget<UButton>(); auto* T = Tree->ConstructWidget<UTextBlock>(); T->SetText(FText::FromString(Caption));
            B->SetContent(T); Frame.Footer->AddChildToVerticalBox(B)->SetPadding(FMargin(0, 4)); Buttons.Add(B);
        }
        Slate = Frame.Root->TakeWidget();
    }
    bool Measure(FVector2D Viewport, float Scale, FGeometry& Bounds, FGeometry& Scroll, TArray<FGeometry>& Footer)
    {
        FRPGNarrativeWindowLayout::ApplyViewport(Frame.Bounds, Frame.Border, Viewport);
        Slate->SlatePrepass(Scale);
        const FGeometry Root = FGeometry::MakeRoot(FVector2f(Viewport), FSlateLayoutTransform(Scale));
        if (!Find(Slate.ToSharedRef(), Root, Frame.Bounds->GetCachedWidget(), Bounds)
            || !Find(Slate.ToSharedRef(), Root, Frame.Scroll->GetCachedWidget(), Scroll)) return false;
        Footer.Reset();
        for (auto* B : Buttons)
        {
            FGeometry Geometry; if (!Find(Slate.ToSharedRef(), Root, B->GetCachedWidget(), Geometry)) return false;
            Footer.Add(Geometry);
        }
        return true;
    }
};
}
using namespace RPGNarrativeWindowLayoutTests;
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeWindowBoundsTest, "ProjectRPG.Narrative.WindowLayout.Bounds", Flags)
bool FRPGNarrativeWindowBoundsTest::RunTest(const FString&)
{
    for (const auto Size : {FVector2D(1920,1080), FVector2D(1280,720), FVector2D(640,360), FVector2D(320,240)})
    {
        const auto Window = FRPGNarrativeWindowLayout::SizeForViewport(Size);
        TestTrue(TEXT("Fits logical viewport with positive margins"), Window.X > 0 && Window.Y > 0 && Window.X < Size.X && Window.Y < Size.Y);
        TestTrue(TEXT("Desktop bounds remain limited"), Window.X <= 720 && Window.Y <= 620);
    }
    TestTrue(TEXT("Pre-viewport construction has stable fallback"), FRPGNarrativeWindowLayout::SizeForViewport(FVector2D::ZeroVector).Equals(FVector2D(720,620)));
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    TestTrue(TEXT("Invalid geometry cannot reach layout setters"), FRPGNarrativeWindowLayout::SizeForViewport(FVector2D(NaN,720)).Equals(FVector2D(720,620)));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeLongWindowTest, "ProjectRPG.Narrative.WindowLayout.LongContent", Flags)
bool FRPGNarrativeLongWindowTest::RunTest(const FString&)
{
    FFixture F; FGeometry Bounds, Scroll; TArray<FGeometry> Footer;
    if (!TestTrue(TEXT("Actual Slate layout can be arranged"), F.Measure(FVector2D(1280,720),1.f,Bounds,Scroll,Footer))) return false;
    TestTrue(TEXT("Text fits the real dialogue input limit"), F.Body->GetText().ToString().Len() <= 4096);
    TestTrue(TEXT("Long body exceeds allocated scroll area"), F.Body->GetDesiredSize().Y > Scroll.GetLocalSize().Y);
    TestTrue(TEXT("Scroll area has positive height"), Scroll.GetLocalSize().Y > 0);
    for (const auto& G : Footer)
    {
        const auto R = G.GetRenderBoundingRect(), B = Bounds.GetRenderBoundingRect();
        TestTrue(TEXT("All controls remain inside the fixed window"), R.Left >= B.Left && R.Right <= B.Right && R.Top >= B.Top && R.Bottom <= B.Bottom);
        TestTrue(TEXT("Footer stays below the content viewport"), R.Top >= Scroll.GetRenderBoundingRect().Bottom);
    }
    TestTrue(TEXT("Footer is not a scroll descendant"), F.Frame.Footer->GetParent() != F.Frame.Content);
    TestTrue(TEXT("Ordinary refresh scrolls with the content"), F.Refresh->GetParent() == F.Frame.Content);
    TestEqual(TEXT("Focus navigation scrolls content into view"), F.Frame.Scroll->GetScrollWhenFocusChanges(), EScrollWhenFocusChanges::InstantScroll);
    TestEqual(TEXT("Content always clips its descendants"), F.Frame.Scroll->GetClipping(), EWidgetClipping::ClipToBoundsAlways);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeWindowResizeTest, "ProjectRPG.Narrative.WindowLayout.Resize", Flags)
bool FRPGNarrativeWindowResizeTest::RunTest(const FString&)
{
    FFixture F;
    for (const auto Size : {FVector2D(1280,720), FVector2D(640,360), FVector2D(320,240), FVector2D(1920,1080)})
        for (const float Scale : {.75f,1.f,1.5f,2.f})
        {
            FGeometry Bounds, Scroll; TArray<FGeometry> Footer;
            if (!TestTrue(TEXT("Rearrange after size/DPI change"), F.Measure(Size,Scale,Bounds,Scroll,Footer))) return false;
            const auto R = Bounds.GetRenderBoundingRect();
            TestTrue(TEXT("Frame fits the scaled viewport"), R.Left >= 0 && R.Top >= 0 && R.Right <= Size.X*Scale && R.Bottom <= Size.Y*Scale);
            for (const auto& G : Footer)
                TestTrue(FString::Printf(TEXT("Controls fit after resize (%.0fx%.0f, scale %.2f, control bottom %.2f, frame bottom %.2f)"),
                    Size.X, Size.Y, Scale, G.GetRenderBoundingRect().Bottom, R.Bottom), G.GetRenderBoundingRect().Bottom <= R.Bottom + .1f);
        }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeWindowVisibilityTest, "ProjectRPG.Narrative.WindowLayout.Visibility", Flags)
bool FRPGNarrativeWindowVisibilityTest::RunTest(const FString&)
{
    const FSlateRect Viewport(0,0,800,600), Scroll(50,50,750,400);
    TestTrue(TEXT("Partly visible long line can be recorded"), FRPGNarrativeWindowLayout::HasVisibleArea(FSlateRect(60,60,700,2000),Scroll,Viewport));
    TestFalse(TEXT("Scrolled-out line is not displayed"), FRPGNarrativeWindowLayout::HasVisibleArea(FSlateRect(60,-400,700,40),Scroll,Viewport));
    TestFalse(TEXT("Only touching clip edge is not visible"), FRPGNarrativeWindowLayout::HasVisibleArea(FSlateRect(60,400,700,600),Scroll,Viewport));
    TestFalse(TEXT("Viewport also clips a displaced window"), FRPGNarrativeWindowLayout::HasVisibleArea(FSlateRect(900,100,1000,200),FSlateRect(850,50,1100,400),Viewport));
    TestFalse(TEXT("No arranged geometry cannot collect text"), FRPGNarrativeWindowLayout::HasVisibleArea(FSlateRect(0,0,0,0),Scroll,Viewport));
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("Non-finite geometry is rejected"), FRPGNarrativeWindowLayout::HasVisibleArea(FSlateRect(NaN,0,100,100),Scroll,Viewport));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeEntryPreviewTest, "ProjectRPG.Narrative.WindowLayout.EntryPreview", Flags)
bool FRPGNarrativeEntryPreviewTest::RunTest(const FString&)
{
    TStrongObjectPtr<UTextBlock> Label(NewObject<UTextBlock>(GetTransientPackage()));
    const FText Authored = FText::FromString(TEXT(" \t안내자\r\n첫 줄\u0085다음\u2028마지막\u2029끝 \U0001F680  "));
    FRPGNarrativeWindowLayout::SetActionLabel(*Label, Authored, true);
    TestEqual(TEXT("Line separators and repeated spaces become an inline preview without damaging Unicode"),
        Label->GetText().ToString(), FString(TEXT("안내자 첫 줄 다음 마지막 끝 \U0001F680")));
    TestFalse(TEXT("Entry preview does not wrap"), Label->GetAutoWrapText());
    TestEqual(TEXT("Narrow rows use text ellipsis"), Label->GetTextOverflowPolicy(), ETextOverflowPolicy::Ellipsis);
    FRPGNarrativeWindowLayout::SetActionLabel(*Label, FText::FromString(TEXT("\r\n\t \u2028")), true);
    TestFalse(TEXT("Whitespace-only entry remains a named actionable control"), Label->GetText().IsEmptyOrWhitespace());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeEntrySizingTest, "ProjectRPG.Narrative.WindowLayout.EntrySizing", Flags)
bool FRPGNarrativeEntrySizingTest::RunTest(const FString&)
{
    TStrongObjectPtr<UWidgetTree> Tree(NewObject<UWidgetTree>(GetTransientPackage()));
    auto* Page = Tree->ConstructWidget<UVerticalBox>(); Tree->RootWidget = Page;
    TArray<UButton*> Buttons;
    FString Long; for (int32 I = 0; I < 160; ++I) Long += TEXT("아주 긴 대사의 진입 미리보기\n");
    for (const FString& Caption : {Long, FString(TEXT("다음 대화"))})
    {
        auto* Button = Tree->ConstructWidget<UButton>(); auto* Text = Tree->ConstructWidget<UTextBlock>();
        FRPGNarrativeWindowLayout::SetActionLabel(*Text, FText::FromString(Caption), true);
        Button->SetContent(Text); Page->AddChildToVerticalBox(Button); Buttons.Add(Button);
    }
    auto Slate = Page->TakeWidget();
    for (const float Width : {80.f,160.f,320.f}) for (const float Scale : {.75f,1.f,2.f})
    {
        Slate->SlatePrepass(Scale);
        const auto Root = FGeometry::MakeRoot(FVector2f(Width,400), FSlateLayoutTransform(Scale));
        FGeometry First, Next;
        if (!TestTrue(TEXT("Both entry buttons are arranged"), Find(Slate,Root,Buttons[0]->GetCachedWidget(),First)
            && Find(Slate,Root,Buttons[1]->GetCachedWidget(),Next))) return false;
        TestTrue(TEXT("A 160-line entry occupies the same height as a short entry"),
            FMath::IsNearlyEqual(First.GetLocalSize().Y, Next.GetLocalSize().Y, .1));
        TestTrue(TEXT("Following entry stays immediately reachable"),
            Next.GetRenderBoundingRect().Bottom <= 2 * First.GetLocalSize().Y * Scale + 1);
        FGeometry Caption;
        if (!TestTrue(TEXT("The real preview text is arranged"), Find(Slate,Root,Buttons[0]->GetContent()->GetCachedWidget(),Caption))) return false;
        const auto Rect = Caption.GetRenderBoundingRect();
        TestTrue(TEXT("Ellipsis text receives the available row width"), Rect.Left >= -.1 && Rect.Right <= Width * Scale + .1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeChoiceTextTest, "ProjectRPG.Narrative.WindowLayout.ChoiceText", Flags)
bool FRPGNarrativeChoiceTextTest::RunTest(const FString&)
{
    TStrongObjectPtr<UTextBlock> Label(NewObject<UTextBlock>(GetTransientPackage()));
    const FText Authored = FText::FromString(TEXT("  첫 선택\r\n둘째 줄\t원문  "));
    FRPGNarrativeWindowLayout::SetActionLabel(*Label, Authored, true);
    auto Slate = Label->TakeWidget(); Slate->SlatePrepass(1.f);
    const float PreviewHeight = Slate->GetDesiredSize().Y;
    FRPGNarrativeWindowLayout::SetActionLabel(*Label, Authored, false);
    Slate->SlatePrepass(1.f);
    TestEqual(TEXT("Switching a built label to a choice restores the exact authored text"), Label->GetText().ToString(), Authored.ToString());
    TestTrue(TEXT("Active choices retain wrapping"), Label->GetAutoWrapText());
    TestEqual(TEXT("Active choices are not shortened by preview ellipsis"), Label->GetTextOverflowPolicy(), ETextOverflowPolicy::Clip);
    TestTrue(TEXT("Explicit choice line breaks still create multiple lines"), Slate->GetDesiredSize().Y > PreviewHeight * 1.5f);
    return true;
}
#endif
