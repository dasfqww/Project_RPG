#include "UI/MVVM/RPGGreatSwordIdentityViewModel.h"

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

#include "PlayerClass/RPGPlayerClassDefinition.h"

void URPGGreatSwordIdentityViewModel::SetSource(UAbilitySystemComponent* Next)
{
    UnbindSource(); Source = Next; BindSource(); Refresh();
}
void URPGGreatSwordIdentityViewModel::Activate()
{
    UnbindSource(); bActive = true; BindSource(); Refresh();
}
void URPGGreatSwordIdentityViewModel::Deactivate()
{
    bActive = false; UnbindSource(); Source.Reset(); Refresh();
}
void URPGGreatSwordIdentityViewModel::BeginDestroy()
{
    bActive = false; UnbindSource(); Source.Reset(); Super::BeginDestroy();
}
FGameplayTag URPGGreatSwordIdentityViewModel::GetActivationInputTag() const
{
    const auto* Skills = Growth.Get();
    const auto* Definition = Skills ? Skills->GetPlayerClassDefinition() : nullptr;
    return Definition ? Definition->IdentityInputTag : FGameplayTag();
}
void URPGGreatSwordIdentityViewModel::BindSource()
{
    auto* ASC = Source.Get();
    auto* Actor = ASC ? (ASC->GetAvatarActor() ? ASC->GetAvatarActor() : ASC->GetOwner()) : nullptr;
    if (!bActive || !ASC || !ASC->IsRegistered() || !IsValid(Actor) || Actor->IsActorBeingDestroyed()) return;
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

void URPGGreatSwordIdentityViewModel::UnbindSource()
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

void URPGGreatSwordIdentityViewModel::OnSourceEndPlay(AActor* Actor, EEndPlayReason::Type)
{
    if (Actor == SourceActor.Get()) SetSource(nullptr);
}
void URPGGreatSwordIdentityViewModel::OnAttributeChanged(const FOnAttributeChangeData&) { Refresh(); }
void URPGGreatSwordIdentityViewModel::OnModeChanged(FGameplayTag, int32) { Refresh(); }
void URPGGreatSwordIdentityViewModel::OnSkillDataChanged(FGameplayTag) { Refresh(); }

void URPGGreatSwordIdentityViewModel::Refresh()
{
    FRPGGreatSwordIdentityPresentation Next;
    const auto* ASC = Source.Get();
    const auto* Actor = SourceActor.Get();
    const auto* Stats = ASC ? ASC->GetSet<URPGAttributeSet>() : nullptr;
    if (bActive && ASC && ASC->IsRegistered() && IsValid(Actor) && !Actor->IsActorBeingDestroyed()
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
    const bool bChanged = Displayed.bHasSource != Next.bHasSource || Displayed.bUnlocked != Next.bUnlocked
        || Displayed.bBerserk != Next.bBerserk || Displayed.Gauge != Next.Gauge || Displayed.Maximum != Next.Maximum;
    if (!bChanged) return;
    Displayed = Next;
    GaugePercent = Next.bUnlocked ? Next.Fraction : 0.f;
    ModeText = !Next.bHasSource ? FText::GetEmpty()
        : !Next.bUnlocked ? NSLOCTEXT("GreatSwordIdentityHUD", "CompactLocked", "미해금")
        : Next.bBerserk ? NSLOCTEXT("GreatSwordIdentityHUD", "CompactBerserk", "폭주")
        : Next.Fraction >= 1.f ? NSLOCTEXT("GreatSwordIdentityHUD", "CompactFull", "충전 완료")
        : NSLOCTEXT("GreatSwordIdentityHUD", "CompactGather", "투지");
    FNumberFormattingOptions PercentFormat;
    PercentFormat.SetUseGrouping(false).SetMinimumFractionalDigits(0).SetMaximumFractionalDigits(0);
    PercentText = Next.bHasSource && Next.bUnlocked ? FText::AsPercent(Next.Fraction, &PercentFormat)
        : Next.bHasSource ? NSLOCTEXT("GreatSwordIdentityHUD", "LockedPercent", "—") : FText::GetEmpty();
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(Displayed);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(GaugePercent);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(ModeText);
    UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(PercentText);
    // All state is published before callbacks; observers may rebind this source.
    OnPresentationChanged.Broadcast();
}

FText URPGGreatSwordIdentityViewModel::GetIdentityText() const
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
