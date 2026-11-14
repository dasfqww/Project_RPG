#if WITH_DEV_AUTOMATION_TESTS
#include "UI/Lobby/RPGTradingWindowLayout.h"
#include "UI/Lobby/RPGTradeWidget.h"
#include "UI/Lobby/RPGAuctionWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "InputCoreTypes.h"
#include "ImageUtils.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/WidgetPath.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "Slate/WidgetRenderer.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SVirtualWindow.h"
#include <limits>

struct FRPGTradingWindowTestAccess
{
    static void Resize(URPGTradeWidget& W,const FGeometry& G){W.NativeTick(G,0);}
    static void Resize(URPGAuctionWidget& W,const FGeometry& G){W.NativeTick(G,0);}
    static USizeBox* Bounds(URPGTradeWidget& W){return W.WindowBounds;}
    static USizeBox* Bounds(URPGAuctionWidget& W){return W.WindowBounds;}
    static UButton* Close(URPGTradeWidget& W){return W.CloseButton;}
    static UButton* Close(URPGAuctionWidget& W){return W.CloseButton;}
    static void Status(URPGTradeWidget& W,const FText& T){W.StatusText->SetText(T);}
    static void Status(URPGAuctionWidget& W,const FText& T){W.StatusText->SetText(T);}
    static FReply Escape(URPGTradeWidget& W,bool Repeat){return W.NativeOnKeyDown(FGeometry(),FKeyEvent(EKeys::Escape,FModifierKeysState(),0,Repeat,0,0));}
    static FReply Escape(URPGAuctionWidget& W,bool Repeat){return W.NativeOnKeyDown(FGeometry(),FKeyEvent(EKeys::Escape,FModifierKeysState(),0,Repeat,0,0));}
};
namespace RPGTradingWindowTests
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
bool Find(const TSharedRef<SWidget>& W,const FGeometry& G,const TSharedPtr<SWidget>& Target,FGeometry& Out,int Depth=0)
{
    if(W.operator->()==Target.Get()){Out=G;return true;}if(Depth>40)return false;
    FArrangedChildren Children(EVisibility::All);W->ArrangeChildren(G,Children);
    for(int i=0;i<Children.Num();++i)if(Find(Children[i].Widget,Children[i].Geometry,Target,Out,Depth+1))return true;
    return false;
}
template<class T> struct FFixture
{
    TStrongObjectPtr<T> Widget{NewObject<T>()};
    TSharedRef<SWidget> Slate=Widget->TakeWidget();
    UScrollBox* Scroll=nullptr;
    FFixture(const TCHAR* Prefix)
    {
        Scroll=Cast<UScrollBox>(Widget->GetWidgetFromName(FName(FString(Prefix)+TEXT("ContentScroll"))));
        FString Long;for(int i=0;i<80;++i)Long+=TEXT("거래 내용과 처리 결과를 확인하세요. Long item details and exact amounts.\n");
        FRPGTradingWindowTestAccess::Status(*Widget,FText::FromString(Long));
    }
    FGeometry Root(FVector2D Pixels,float Scale)
    {return FGeometry::MakeRoot(FVector2f(Pixels/Scale),FSlateLayoutTransform(Scale));}
    bool Measure(FVector2D Pixels,float Scale,FGeometry& Bounds,FGeometry& Close,FGeometry& Content)
    {
        const auto G=Root(Pixels,Scale);
        FRPGTradingWindowTestAccess::Resize(*Widget,G);Slate->SlatePrepass(Scale);
        return Find(Slate,G,FRPGTradingWindowTestAccess::Bounds(*Widget)->GetCachedWidget(),Bounds)
            && Find(Slate,G,FRPGTradingWindowTestAccess::Close(*Widget)->GetCachedWidget(),Close)
            && Scroll && Find(Slate,G,Scroll->GetCachedWidget(),Content);
    }
};
template<class T> bool BoundsCases(FAutomationTestBase& Test,const TCHAR* Prefix)
{
    FFixture<T> F(Prefix);FGeometry Bounds,Close,Scroll;
    for(const auto Pixels:{FVector2D(1920,1080),FVector2D(1280,720),FVector2D(640,360),FVector2D(320,240)})
        for(float Scale:{.75f,1.f,1.5f,2.f})
        {
            if(!Test.TestTrue(TEXT("Actual widget tree arranges after viewport/DPI resize"),F.Measure(Pixels,Scale,Bounds,Close,Scroll)))return false;
            const auto B=Bounds.GetRenderBoundingRect(),C=Close.GetRenderBoundingRect(),S=Scroll.GetRenderBoundingRect();
            Test.TestTrue(TEXT("Frame remains strictly inside physical viewport"),B.Left>=0 && B.Top>=0 && B.Right<Pixels.X && B.Bottom<Pixels.Y);
            Test.TestTrue(TEXT("Close remains visible and below scroll content"),C.Left>=B.Left && C.Right<=B.Right+.1f && C.Bottom<=B.Bottom+.1f && C.Top>=S.Bottom-.1f);
            Test.TestTrue(TEXT("Long contents retain a positive scroll viewport"),S.Bottom>S.Top && S.Right>S.Left);
        }
    Test.TestTrue(TEXT("Close has a fixed parent outside the content scroll"),FRPGTradingWindowTestAccess::Close(*F.Widget)->GetParent()!=F.Scroll);
    Test.TestEqual(TEXT("Focus navigation reveals controls"),F.Scroll->GetScrollWhenFocusChanges(),EScrollWhenFocusChanges::InstantScroll);
    Test.TestEqual(TEXT("Scrolling clips all descendants"),F.Scroll->GetClipping(),EWidgetClipping::ClipToBoundsAlways);
    int32 Closed=0;F.Widget->OnClosed.AddLambda([&Closed]{++Closed;});
    Test.TestTrue(TEXT("Escape handled"),FRPGTradingWindowTestAccess::Escape(*F.Widget,false).IsEventHandled());
    FRPGTradingWindowTestAccess::Escape(*F.Widget,true);Test.TestEqual(TEXT("Repeated escape never triggers another close"),Closed,1);
    return true;
}
template<class T> bool RenderCases(FAutomationTestBase& Test,const TCHAR* Prefix)
{
    FString Directory=FPlatformMisc::GetEnvironmentVariable(TEXT("RPG_TRADING_CAPTURE_DIR"));
    if(Directory.IsEmpty())Directory=FPaths::ProjectSavedDir()/TEXT("Automation/TradingWindowCaptures");
    IFileManager::Get().MakeDirectory(*Directory,true);
    for(const auto Pixels:{FVector2D(1280,720),FVector2D(640,360),FVector2D(320,240)})
    {
        FFixture<T> F(Prefix);FGeometry Bounds,Close,Scroll;const float Scale=Pixels.X==320?1.5f:1.f;
        if(!Test.TestTrue(TEXT("GPU fixture has arranged native widget geometry"),F.Measure(Pixels,Scale,Bounds,Close,Scroll)))return false;
        auto Window=SNew(SVirtualWindow).Size(Pixels/Scale);Window->SetContent(F.Slate);
        auto& App=FSlateApplication::Get();App.RegisterVirtualWindow(Window);
        auto* Renderer=new FWidgetRenderer(true,true);
        TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(Pixels,TF_Bilinear,true));
        for(int i=0;i<3;++i){Renderer->DrawWindow(Target.Get(),Window->GetHittestGrid(),Window,Scale,Pixels,1.f/30.f);FlushRenderingCommands();}
        TArray<FColor> Data;const bool Read=Target->GameThread_GetRenderTargetResource()->ReadPixels(Data);
        Test.TestTrue(TEXT("GPU pixels read back"),Read && Data.Num()==static_cast<int32>(Pixels.X*Pixels.Y));
        TArray64<uint8> PNG;if(Read)FImageUtils::PNGCompressImageArray(Pixels.X,Pixels.Y,TArrayView64<const FColor>(Data.GetData(),Data.Num()),PNG);
        const FString Name=FString::Printf(TEXT("%s-%.0fx%.0f.png"),Prefix,Pixels.X,Pixels.Y);
        Test.TestTrue(TEXT("Final rendered PNG saved"),PNG.Num()>0 && FFileHelper::SaveArrayToFile(PNG,*(Directory/Name)));
        const auto ActualBounds=FRPGTradingWindowTestAccess::Bounds(*F.Widget)->GetCachedGeometry().GetRenderBoundingRect();
        Test.TestTrue(TEXT("Rendered frame fits actual GPU viewport"),ActualBounds.Left>=0 && ActualBounds.Top>=0 && ActualBounds.Right<=Pixels.X && ActualBounds.Bottom<=Pixels.Y);
        const auto S=F.Scroll->GetCachedGeometry().GetRenderBoundingRect();const FVector2D WheelPoint((S.Left+S.Right)*.5f,(S.Top+S.Bottom)*.5f);
        Test.TestTrue(TEXT("Rendered long content has positive scroll height"),S.Bottom>S.Top);
        const FWidgetPath WheelPath(Window->GetHittestGrid().GetBubblePath(FVector2f(WheelPoint),0.f,false,0));
        Test.TestTrue(TEXT("Rendered scroll area is hit-testable"),WheelPath.IsValid() && WheelPath.ContainsWidget(F.Scroll->GetCachedWidget().Get()));
        const FPointerEvent Wheel(0,WheelPoint,WheelPoint,TSet<FKey>(),EKeys::Invalid,-3,FModifierKeysState());
        Test.TestTrue(TEXT("Real Slate wheel routing handles content scroll"),App.RouteMouseWheelOrGestureEvent(WheelPath,Wheel).IsEventHandled());
        Renderer->DrawWindow(Target.Get(),Window->GetHittestGrid(),Window,Scale,Pixels,1.f/30.f);FlushRenderingCommands();
        const auto C=FRPGTradingWindowTestAccess::Close(*F.Widget)->GetCachedGeometry().GetRenderBoundingRect();const FVector2D Point((C.Left+C.Right)*.5f,(C.Top+C.Bottom)*.5f);
        Test.TestTrue(TEXT("Rendered close is wholly inside physical viewport"),C.Left>=0 && C.Top>=0 && C.Right<=Pixels.X && C.Bottom<=Pixels.Y);
        const FWidgetPath Path(Window->GetHittestGrid().GetBubblePath(FVector2f(Point),0.f,false,0));
        Test.TestTrue(TEXT("Fixed close remains hit-testable after wheel"),Path.IsValid() && Path.ContainsWidget(FRPGTradingWindowTestAccess::Close(*F.Widget)->GetCachedWidget().Get()));
        int32 Closed=0;F.Widget->OnClosed.AddLambda([&Closed]{++Closed;});
        App.SetHandleDeviceInputWhenApplicationNotActive(true);
        const FPointerEvent Move(0,Point,Point-FVector2D(1,1),TSet<FKey>(),EKeys::Invalid,0,FModifierKeysState());App.RoutePointerMoveEvent(Path,Move,true);
        TSet<FKey> Pressed;Pressed.Add(EKeys::LeftMouseButton);
        const FPointerEvent Down(0,Point,Point,Pressed,EKeys::LeftMouseButton,0,FModifierKeysState()),Up(0,Point,Point,TSet<FKey>(),EKeys::LeftMouseButton,0,FModifierKeysState());
        Test.TestTrue(TEXT("Real Slate pointer down handled"),App.RoutePointerDownEvent(Path,Down).IsEventHandled());
        Test.TestTrue(TEXT("Real Slate pointer up handled"),App.RoutePointerUpEvent(Path,Up).IsEventHandled());
        Test.TestEqual(TEXT("Pointer route invokes native close exactly once"),Closed,1);
        App.ReleaseAllPointerCapture();App.UnregisterVirtualWindow(Window);BeginCleanup(Renderer);FlushRenderingCommands();
    }
    return true;
}
}
using namespace RPGTradingWindowTests;
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradingWindowBounds,"ProjectRPG.Trading.Window.BoundsAndClose",Flags)
bool FTradingWindowBounds::RunTest(const FString&)
{return BoundsCases<URPGTradeWidget>(*this,TEXT("Trade")) && BoundsCases<URPGAuctionWidget>(*this,TEXT("Auction"));}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradingWindowInvalid,"ProjectRPG.Trading.Window.InvalidGeometry",Flags)
bool FTradingWindowInvalid::RunTest(const FString&)
{
    const FVector2D Maximum(680,720);const auto NaN=std::numeric_limits<double>::quiet_NaN();
    for(const auto View:{FVector2D::ZeroVector,FVector2D(-1,720),FVector2D(NaN,720)})TestTrue(TEXT("Pre-layout and invalid geometry use stable finite fallback"),FRPGTradingWindowLayout::SizeForViewport(View,Maximum).Equals(Maximum));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradingWindowRender,"ProjectRPG.Trading.Window.GpuRenderAndPointer",Flags|EAutomationTestFlags::NonNullRHI)
bool FTradingWindowRender::RunTest(const FString&)
{return RenderCases<URPGTradeWidget>(*this,TEXT("Trade")) && RenderCases<URPGAuctionWidget>(*this,TEXT("Auction"));}
#endif
