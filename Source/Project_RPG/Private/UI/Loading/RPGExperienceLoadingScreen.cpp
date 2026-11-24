#include "UI/Loading/RPGExperienceLoadingScreen.h"

#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Manager/Component/RPGExperienceManagerComponent.h"
#include "Settings/RPGExperienceSettings.h"
#include "Brushes/SlateColorBrush.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RPGExperienceUI"

namespace
{
    const FLinearColor Accent(0.76f, 0.49f, 0.23f);
    const FSlateColorBrush Backdrop(FLinearColor(0.018f, 0.024f, 0.035f));
    const FSlateColorBrush Footer(FLinearColor(0.005f, 0.008f, 0.013f, 0.72f));
    const FSlateColorBrush Track(FLinearColor(0.32f, 0.25f, 0.17f, 0.44f));

    class SLoadingArt final : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SLoadingArt) {}
        SLATE_END_ARGS()

        void Construct(const FArguments&)
        {
            Texture.Reset(LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Loading/T_LoadingCitadel.T_LoadingCitadel")));
            if (Texture)
            {
                Brush.SetResourceObject(Texture.Get());
            }
        }

        virtual FVector2D ComputeDesiredSize(float) const override
        {
            return FVector2D::ZeroVector;
        }

        virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
            FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool) const override
        {
            const FVector2D ViewSize = Geometry.GetLocalSize();
            if (!Texture || Texture->GetSizeX() <= 0 || Texture->GetSizeY() <= 0)
            {
                FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(),
                    &Backdrop, ESlateDrawEffect::None, Backdrop.GetTint(Style));
                return Layer;
            }

            const FVector2D ArtSize(Texture->GetSizeX(), Texture->GetSizeY());
            const double Scale = FMath::Max(ViewSize.X / ArtSize.X, ViewSize.Y / ArtSize.Y);
            const FVector2D DrawSize = ArtSize * Scale;
            const FVector2D Offset = (ViewSize - DrawSize) * 0.5;
            FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(DrawSize, FSlateLayoutTransform(Offset)),
                &Brush, ESlateDrawEffect::None, Style.GetColorAndOpacityTint());
            return Layer;
        }

    private:
        TStrongObjectPtr<UTexture2D> Texture;
        FSlateBrush Brush;
    };

    const FProgressBarStyle& GetLoadingBarStyle()
    {
        static const FProgressBarStyle Style = []
        {
            FProgressBarStyle LoadingBarStyle =
                FCoreStyle::Get().GetWidgetStyle<FProgressBarStyle>("ProgressBar");
            LoadingBarStyle.SetBackgroundImage(Track);
            LoadingBarStyle.MarqueeImage.TintColor = FSlateColor(Accent);
            return LoadingBarStyle;
        }();
        return Style;
    }
}

URPGExperienceLoadingScreen::URPGExperienceLoadingScreen(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

bool URPGExperienceLoadingScreen::IsExperienceFailed() const
{
    const UWorld* World = GetWorld();
    const AGameStateBase* State = World ? World->GetGameState() : nullptr;
    const URPGExperienceManagerComponent* Experience = IsValid(State)
        ? State->FindComponentByClass<URPGExperienceManagerComponent>() : nullptr;
    return IsValid(Experience) && Experience->GetLoadState() == ERPGExperienceLoadState::Failed;
}

FText URPGExperienceLoadingScreen::GetTitleText() const
{
    return IsExperienceFailed() ? LOCTEXT("FailedTitle", "게임을 시작할 수 없습니다")
        : LOCTEXT("PreparingTitle", "게임을 준비하고 있습니다");
}

FText URPGExperienceLoadingScreen::GetMessageText() const
{
    return IsExperienceFailed() ? GetDefault<URPGExperienceSettings>()->LoadingFailureMessage
        : LOCTEXT("PreparingMessage", "잠시만 기다려 주세요.");
}

TSharedRef<SWidget> URPGExperienceLoadingScreen::RebuildWidget()
{
    const TWeakObjectPtr<URPGExperienceLoadingScreen> WeakThis(this);
    return SNew(SOverlay)
        .Clipping(EWidgetClipping::ClipToBounds)
        + SOverlay::Slot()
        [
            SNew(SLoadingArt)
        ]
        + SOverlay::Slot().VAlign(VAlign_Bottom)
        [
            SNew(SBorder)
            .BorderImage(&Footer)
            .Padding(FMargin(44.f, 24.f, 44.f, 27.f))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock)
                    .Text_Lambda([WeakThis]()
                    {
                        const URPGExperienceLoadingScreen* Screen = WeakThis.Get();
                        return Screen ? Screen->GetTitleText() : FText::GetEmpty();
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 30))
                    .ColorAndOpacity(FLinearColor(0.94f, 0.89f, 0.79f))
                    .AutoWrapText(true)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0.f, 7.f, 0.f, 0.f)
                [
                    SNew(STextBlock)
                    .Text_Lambda([WeakThis]()
                    {
                        const URPGExperienceLoadingScreen* Screen = WeakThis.Get();
                        return Screen ? Screen->GetMessageText() : FText::GetEmpty();
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 15))
                    .ColorAndOpacity(FLinearColor(0.77f, 0.78f, 0.78f))
                    .AutoWrapText(true)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0.f, 20.f, 0.f, 0.f)
                [
                    SNew(SBox)
                    .HeightOverride(4.f)
                    .Visibility_Lambda([WeakThis]()
                    {
                        const URPGExperienceLoadingScreen* Screen = WeakThis.Get();
                        return Screen && !Screen->IsExperienceFailed()
                            ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                    })
                    [
                        SNew(SProgressBar)
                        .Style(&GetLoadingBarStyle())
                        .Percent(TOptional<float>())
                    ]
                ]
            ]
        ];
}

#undef LOCTEXT_NAMESPACE
