#include "UI/Identity/RPGGreatSwordIdentityWidget.h"

#include "AbilitySystemGlobals.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Controller/RPGPlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

URPGGreatSwordIdentityViewModel& URPGGreatSwordIdentityWidget::EnsureViewModel()
{
    if (!ViewModel) ViewModel = NewObject<URPGGreatSwordIdentityViewModel>(this);
    return *ViewModel;
}
void URPGGreatSwordIdentityWidget::SetIdentitySource(UAbilitySystemComponent* Next) { EnsureViewModel().SetSource(Next); }
FRPGGreatSwordIdentityPresentation URPGGreatSwordIdentityWidget::GetDisplayedIdentity() const
{
    return ViewModel ? ViewModel->Displayed : FRPGGreatSwordIdentityPresentation();
}
FText URPGGreatSwordIdentityWidget::GetIdentityText() const { return ViewModel ? ViewModel->GetIdentityText() : FText::GetEmpty(); }
void URPGGreatSwordIdentityWidget::UnbindController()
{
    if (auto* PC = Controller.Get()) PC->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::OnOwningPawnChanged);
    Controller.Reset();
}
void URPGGreatSwordIdentityWidget::NativeConstruct()
{
    Super::NativeConstruct();
    UnbindController(); bConstructed = true;
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    auto& VM = EnsureViewModel();
    VM.OnPresentationChanged.Remove(PresentationChanged);
    PresentationChanged = VM.OnPresentationChanged.AddUObject(this, &ThisClass::Present);
    VM.Activate();
    if (auto* PC = GetOwningPlayer(); bAutoBindOwningPawn && PC && PC->IsLocalController())
    {
        Controller = PC;
        PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::OnOwningPawnChanged);
        OnOwningPawnChanged(nullptr, PC->GetPawn());
    }
    Present();
}
void URPGGreatSwordIdentityWidget::NativeDestruct()
{
    bConstructed = false; UnbindController();
    if (ViewModel) { ViewModel->OnPresentationChanged.Remove(PresentationChanged); ViewModel->Deactivate(); }
    PresentationChanged.Reset(); bLastBerserk = false; SetVisibility(ESlateVisibility::Collapsed);
    Super::NativeDestruct();
}
void URPGGreatSwordIdentityWidget::BeginDestroy()
{
    bConstructed = false; UnbindController();
    if (ViewModel) { ViewModel->OnPresentationChanged.Remove(PresentationChanged); ViewModel->Deactivate(); }
    Super::BeginDestroy();
}
void URPGGreatSwordIdentityWidget::OnOwningPawnChanged(APawn*, APawn*)
{
    const auto* PC = Controller.Get();
    if (PC == GetOwningPlayer())
        SetIdentitySource(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PC ? PC->GetPawn() : nullptr, true));
}
void URPGGreatSwordIdentityWidget::Present()
{
    const auto State = GetDisplayedIdentity();
    // The action-bar presenter owns external hiding (e.g. mounting). State
    // updates must never uncollapse its hidden base widget. Missing state paints nothing.
    // Keep original bindings/primitive values for authored module compatibility,
    // but only the ring paints. No obsolete rage icon can cover the new meter.
    if (WBP_IdentityGaugeBar)
    {
        WBP_IdentityGaugeBar->SetRenderOpacity(0.f);
        WBP_IdentityGaugeBar->SetVisibility(ESlateVisibility::HitTestInvisible);
        if (auto* Bar = Cast<UProgressBar>(WBP_IdentityGaugeBar->GetWidgetFromName(TEXT("ProgressBar_Main"))))
            Bar->SetPercent(ViewModel ? ViewModel->GaugePercent : 0.f);
        if (auto* Text = Cast<UTextBlock>(WBP_IdentityGaugeBar->GetWidgetFromName(TEXT("ProgressText"))))
        {
            Text->SetText(GetIdentityText()); Text->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        if (auto* Timer = WBP_IdentityGaugeBar->GetWidgetFromName(TEXT("ProgressTimeText"))) Timer->SetVisibility(ESlateVisibility::Collapsed);
    }
    if (WidgetTree)
        if (auto* Icon = WidgetTree->FindWidget(TEXT("WBP_AbilityIconSlot_RageSkill")))
        { Icon->SetRenderOpacity(0.f); Icon->SetVisibility(ESlateVisibility::HitTestInvisible); }
    if (bConstructed && bLastBerserk != State.bBerserk && InterpUp)
        PlayAnimation(InterpUp, 0.f, 1, State.bBerserk ? EUMGSequencePlayMode::Forward : EUMGSequencePlayMode::Reverse);
    bLastBerserk = State.bBerserk;
    InvalidateLayoutAndVolatility();
}

int32 URPGGreatSwordIdentityWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry,
    const FSlateRect& Cull, FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const
{
    const int32 ChildLayer = Super::NativePaint(Args, Geometry, Cull, Elements, Layer, Style, bEnabled);
    if (!ViewModel || !ViewModel->Displayed.bHasSource) return ChildLayer;
    // Authored module stays 144x144, host scales it to 96x96. Draw vectors in
    // that module's local square so no new texture/material or viewport tick is needed.
    const FVector2D Size = Geometry.GetLocalSize();
    const float Side = FMath::Min(Size.X, Size.Y);
    if (Side <= 0.f) return ChildLayer;
    const float Scale = Side / 144.f;
    const FVector2D Origin((Size.X - Side) / 2.f, Size.Y - Side);
    const FVector2D Center = Origin + FVector2D(Side / 2.f);
    const float Radius = 65.f * Scale;
    const auto& State = ViewModel->Displayed;
    const FLinearColor Accent = !State.bUnlocked ? FLinearColor(.28f,.25f,.21f)
        : State.bBerserk ? FLinearColor(1.f,.18f,.055f)
        : State.Fraction >= 1.f ? FLinearColor(1.f,.72f,.24f) : FLinearColor(.85f,.43f,.13f);
    const FLinearColor Tint = Style.GetColorAndOpacityTint() * GetRenderOpacity();
    const int32 PaintLayer = ChildLayer + 1;
    static const FSlateRoundedBoxBrush Disc(FLinearColor(.035f,.025f,.018f,.98f),72.f);
    FSlateDrawElement::MakeBox(Elements, PaintLayer, Geometry.ToPaintGeometry(FVector2D(Side),FSlateLayoutTransform(Origin)), &Disc,
        ESlateDrawEffect::None, Disc.GetTint(Style) * Tint);
    auto Arc = [&](float From, float To, float R, float Width, FLinearColor Color)
    {
        TArray<FVector2D> Points;
        const int32 Steps = FMath::Max(1,FMath::CeilToInt((To-From)*96.f));
        for (int32 I=0;I<=Steps;++I)
        {
            const float Angle = -HALF_PI + 2.f*PI*FMath::Lerp(From,To,float(I)/Steps);
            Points.Add(Center + FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*R);
        }
        FSlateDrawElement::MakeLines(Elements,PaintLayer+1,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color*Tint,true,Width*Scale);
    };
    Arc(0,1,Radius,7.f,FLinearColor(.16f,.12f,.085f));
    Arc(0,1,Radius+6.f*Scale,1.f,FLinearColor(.45f,.34f,.22f,.8f));
    if (ViewModel->GaugePercent > 0.f) Arc(0,ViewModel->GaugePercent,Radius,7.f,Accent);
    // Twelve quiet calibration marks retain a readable empty/partial circle.
    for (int32 I=0;I<12;++I)
    {
        const float Angle=-HALF_PI+I*2.f*PI/12.f;
        const FVector2D Direction(FMath::Cos(Angle),FMath::Sin(Angle));
        TArray<FVector2D> Points{Center+Direction*(Radius-7.f*Scale),Center+Direction*(Radius-3.f*Scale)};
        FSlateDrawElement::MakeLines(Elements,PaintLayer+2,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,FLinearColor(.62f,.48f,.29f)*Tint,true,Scale);
    }
    auto Text = [&](const FText& Value,float Y,int32 FontSize,FLinearColor Color)
    {
        if (Value.IsEmpty()) return;
        const auto Font=FCoreStyle::GetDefaultFontStyle("Bold",FMath::Max(1,FMath::RoundToInt(FontSize*Scale)));
        const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Value,Font);
        const FVector2D Position(Center.X-Measure.X/2.f,Origin.Y+Y*Scale-Measure.Y/2.f);
        FSlateDrawElement::MakeText(Elements,PaintLayer+3,Geometry.ToPaintGeometry(FVector2D(Side),FSlateLayoutTransform(Position)),Value,Font,ESlateDrawEffect::None,Color*Tint);
    };
    Text(ViewModel->ModeText,42.f,18,State.bUnlocked ? FLinearColor(.93f,.79f,.57f) : FLinearColor(.55f,.51f,.44f));
    Text(ViewModel->PercentText,73.f,28,Accent);
    FText KeyText;
    if (const auto* PC=Cast<ARPGPlayerController>(GetOwningPlayer()); PC && State.bUnlocked)
    {
        const FGameplayTag Tag=ViewModel->GetActivationInputTag();
        const FKey Key=Tag.IsValid() ? PC->GetCurrentKeyForTag(Tag) : FKey();
        if (Key.IsValid()) KeyText=Key.GetDisplayName(false);
    }
    Text(KeyText,106.f,18,FLinearColor(.86f,.80f,.69f));
    return PaintLayer+3;
}
