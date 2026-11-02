#include "UI/Identity/RPGGreatSwordIdentityWidget.h"

#include "Ability/Skill/RPGGreatSwordResolveAbility.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "PlayerClass/RPGPlayerClassComponent.h"
#include "RPGGameplayTags.h"

void URPGGreatSwordIdentityWidget::SetIdentitySource(UAbilitySystemComponent* Next)
{
    UnbindSource();
    Source = Next;
    BindSource();
    Refresh();
}

void URPGGreatSwordIdentityWidget::BindSource()
{
    auto* ASC = Source.Get();
    auto* Actor = ASC ? (ASC->GetAvatarActor() ? ASC->GetAvatarActor() : ASC->GetOwner()) : nullptr;
    if (!bConstructed || !ASC || !ASC->IsRegistered() || !IsValid(Actor) || Actor->IsActorBeingDestroyed()) return;
    SourceActor = Actor;
    Actor->OnEndPlay.AddUniqueDynamic(this, &ThisClass::OnSourceEndPlay);
    GaugeChanged = ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()).AddUObject(this, &ThisClass::OnAttributeChanged);
    MaximumChanged = ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetMaxIdentityGaugeAttribute()).AddUObject(this, &ThisClass::OnAttributeChanged);
    HealthChanged = ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).AddUObject(this, &ThisClass::OnAttributeChanged);
    ModeChanged = ASC->RegisterGameplayTagEvent(RPGGameplayTags::Player_Status_Identity_GreatSword_Ready, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &ThisClass::OnModeChanged);
    Growth = Actor->FindComponentByClass<URPGPlayerSkillComponent>();
    if (auto* Skills = Growth.Get()) Skills->OnSkillDataChanged.AddUniqueDynamic(this, &ThisClass::OnSkillDataChanged);
    ClassOwner = Actor->FindComponentByClass<URPGPlayerClassComponent>();
    if (auto* Class = ClassOwner.Get()) ClassChanged = Class->OnApplicationChanged.AddUObject(this, &ThisClass::Refresh);
}

void URPGGreatSwordIdentityWidget::UnbindSource()
{
    if (auto* ASC = Source.Get())
    {
        ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()).Remove(GaugeChanged);
        ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetMaxIdentityGaugeAttribute()).Remove(MaximumChanged);
        ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).Remove(HealthChanged);
        ASC->RegisterGameplayTagEvent(RPGGameplayTags::Player_Status_Identity_GreatSword_Ready, EGameplayTagEventType::NewOrRemoved).Remove(ModeChanged);
    }
    if (auto* Actor = SourceActor.Get()) Actor->OnEndPlay.RemoveDynamic(this, &ThisClass::OnSourceEndPlay);
    if (auto* Skills = Growth.Get()) Skills->OnSkillDataChanged.RemoveDynamic(this, &ThisClass::OnSkillDataChanged);
    if (auto* Class = ClassOwner.Get()) Class->OnApplicationChanged.Remove(ClassChanged);
    SourceActor.Reset(); Growth.Reset(); ClassOwner.Reset();
    GaugeChanged.Reset(); MaximumChanged.Reset(); HealthChanged.Reset(); ModeChanged.Reset(); ClassChanged.Reset();
}

void URPGGreatSwordIdentityWidget::UnbindController()
{
    if (auto* PC = Controller.Get()) PC->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::OnOwningPawnChanged);
    Controller.Reset();
}

void URPGGreatSwordIdentityWidget::NativeConstruct()
{
    Super::NativeConstruct();
    UnbindController(); UnbindSource(); bConstructed = true;
    if (auto* PC = GetOwningPlayer(); bAutoBindOwningPawn && PC && PC->IsLocalController())
    {
        Controller = PC;
        PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::OnOwningPawnChanged);
        OnOwningPawnChanged(nullptr, PC->GetPawn());
    }
    else { BindSource(); Refresh(); }
}

void URPGGreatSwordIdentityWidget::NativeDestruct()
{
    bConstructed = false;
    UnbindController(); UnbindSource(); Source.Reset(); Refresh();
    Super::NativeDestruct();
}

void URPGGreatSwordIdentityWidget::BeginDestroy()
{
    bConstructed = false;
    UnbindController(); UnbindSource(); Source.Reset();
    Super::BeginDestroy();
}

void URPGGreatSwordIdentityWidget::OnOwningPawnChanged(APawn*, APawn*)
{
    auto* PC = Controller.Get();
    if (PC != GetOwningPlayer()) return;
    SetIdentitySource(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PC ? PC->GetPawn() : nullptr, true));
}
void URPGGreatSwordIdentityWidget::OnSourceEndPlay(AActor* Actor, EEndPlayReason::Type)
{
    if (Actor == SourceActor.Get()) SetIdentitySource(nullptr);
}
void URPGGreatSwordIdentityWidget::OnAttributeChanged(const FOnAttributeChangeData&) { Refresh(); }
void URPGGreatSwordIdentityWidget::OnModeChanged(FGameplayTag, int32) { Refresh(); }
void URPGGreatSwordIdentityWidget::OnSkillDataChanged(FGameplayTag) { Refresh(); }

void URPGGreatSwordIdentityWidget::Refresh()
{
    FRPGGreatSwordIdentityPresentation Next;
    const auto* ASC = Source.Get();
    const auto* Actor = SourceActor.Get();
    const auto* Stats = ASC ? ASC->GetSet<URPGAttributeSet>() : nullptr;
    if (bConstructed && ASC && ASC->IsRegistered() && IsValid(Actor) && !Actor->IsActorBeingDestroyed()
        && ASC->GetAvatarActor() == Actor && URPGGreatSwordResolveAbility::IsConfiguredFor(Actor) && Stats
        && FMath::IsFinite(Stats->GetCurrentIdentityGauge()) && FMath::IsFinite(Stats->GetMaxIdentityGauge())
        && Stats->GetMaxIdentityGauge() > 0.f && FMath::IsFinite(Stats->GetCurrentHealth()) && Stats->GetCurrentHealth() > 0.f)
    {
        Next.bHasSource = true;
        Next.bUnlocked = Growth.IsValid() && Growth->IsClassIdentityUnlocked();
        Next.Maximum = Stats->GetMaxIdentityGauge();
        Next.Gauge = FMath::Clamp(Stats->GetCurrentIdentityGauge(), 0.f, Next.Maximum);
        Next.Fraction = Next.Gauge / Next.Maximum;
        Next.bBerserk = Next.bUnlocked && URPGGreatSwordResolveAbility::IsBerserk(Actor);
    }
    const bool bModeChanged = Displayed.bBerserk != Next.bBerserk;
    Displayed = Next;
    Present(bModeChanged);
}

FText URPGGreatSwordIdentityWidget::GetIdentityText() const
{
    if (!Displayed.bHasSource) return FText::GetEmpty();
    if (!Displayed.bUnlocked) return NSLOCTEXT("GreatSwordIdentityHUD", "Locked", "고유 능력 미해금");
    const FText State = Displayed.bBerserk ? NSLOCTEXT("GreatSwordIdentityHUD", "Berserk", "폭주")
        : Displayed.Fraction >= 1.f ? NSLOCTEXT("GreatSwordIdentityHUD", "Full", "투지 충전 완료")
        : NSLOCTEXT("GreatSwordIdentityHUD", "Gather", "투지");
    FNumberFormattingOptions Format;
    Format.SetUseGrouping(false).SetMinimumFractionalDigits(0).SetMaximumFractionalDigits(0);
    return FText::Format(NSLOCTEXT("GreatSwordIdentityHUD", "StateGauge", "{0}  {1}/{2}"), State,
        FText::AsNumber(Displayed.Gauge, &Format), FText::AsNumber(Displayed.Maximum, &Format));
}

void URPGGreatSwordIdentityWidget::Present(bool bModeChanged)
{
    SetVisibility(Displayed.bHasSource ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    if (WBP_IdentityGaugeBar)
    {
        // These exact primitive names are validated by ConfigureGreatSwordIdentityHUD.
        const FLinearColor Color = Displayed.bBerserk ? FLinearColor(1.f, .08f, .015f)
            : Displayed.Fraction >= 1.f ? FLinearColor(1.f, .65f, .08f) : FLinearColor(.65f, .25f, .08f);
        if (auto* Bar = Cast<UProgressBar>(WBP_IdentityGaugeBar->GetWidgetFromName(TEXT("ProgressBar_Main"))))
        {
            Bar->SetPercent(Displayed.bUnlocked ? Displayed.Fraction : 0.f);
            Bar->SetFillColorAndOpacity(Color);
        }
        if (auto* Label = Cast<UTextBlock>(WBP_IdentityGaugeBar->GetWidgetFromName(TEXT("ProgressText"))))
        {
            Label->SetText(GetIdentityText());
            Label->SetVisibility(ESlateVisibility::HitTestInvisible);
            auto Font = Label->GetFont();
            Font.Size = 16; // The authored identity bar is narrower than the HP/MP bars.
            Label->SetFont(Font);
        }
        if (auto* Timer = WBP_IdentityGaugeBar->GetWidgetFromName(TEXT("ProgressTimeText"))) Timer->SetVisibility(ESlateVisibility::Collapsed);
    }
    if (bConstructed && bModeChanged && InterpUp)
        PlayAnimation(InterpUp, 0.f, 1, Displayed.bBerserk ? EUMGSequencePlayMode::Forward : EUMGSequencePlayMode::Reverse);
}
