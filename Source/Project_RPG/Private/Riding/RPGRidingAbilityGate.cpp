#include "Riding/RPGRidingAbilityGate.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingAbilityPolicy.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"

namespace
{
    bool Matches(const FGameplayTag& Tag, const TCHAR* Parent)
    {
        const auto Root = FGameplayTag::RequestGameplayTag(Parent, false);
        return Root.IsValid() && Tag.MatchesTag(Root);
    }
    bool BasicInput(const FGameplayTag& Input)
    { return Matches(Input, TEXT("InputTag.LightAttack")) || Matches(Input, TEXT("InputTag.HeavyAttack")); }
    URPGRidingComponent* Mounted(const AActor* Actor)
    {
        auto* Riding = IsValid(Actor) ? Actor->FindComponentByClass<URPGRidingComponent>() : nullptr;
        if (!Riding) return nullptr;
        const auto State = Riding->GetRidingState();
        return RPGRiding::IsCanonical(State) && State.Mode == ERPGRidingMode::Mounted ? Riding : nullptr;
    }
    bool BasicSpec(const FGameplayAbilitySpec& Spec)
    {
        if (Spec.Ability) for (const auto& Tag : Spec.Ability->GetAssetTags())
            if (Matches(Tag, TEXT("Player.Ability.Attack"))) return true;
        for (const auto& Tag : Spec.GetDynamicSpecSourceTags()) if (BasicInput(Tag)) return true;
        return false;
    }
    void Suppress(UAbilitySystemComponent& ASC, FGameplayAbilitySpec& Pressed,
        TSet<FGameplayAbilitySpecHandle>& Handles, FGameplayTagContainer& Tags)
    {
        FGameplayTagContainer Inputs;
        for (const auto& Tag : Pressed.GetDynamicSpecSourceTags()) if (Matches(Tag, TEXT("InputTag"))) Inputs.AddTag(Tag);
        Tags.AppendTags(Inputs);
        Handles.Add(Pressed.Handle); Pressed.InputPressed = false;
        for (auto& Other : ASC.GetActivatableAbilities())
            if (Other.GetDynamicSpecSourceTags().HasAnyExact(Inputs)) { Handles.Add(Other.Handle); Other.InputPressed = false; }
    }
}

bool FRPGRidingAbilityGate::IsManualAbility(const UGameplayAbility* Ability)
{
    if (!Ability || Ability->GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::LocalOnly) return false;
    if (const auto* Policy = Cast<IRPGRidingAbilityPolicy>(Ability); Policy && (Policy->IsAutomaticRidingExempt() || Policy->IsRidingActionAbility())) return false;
    const auto& Tags = Ability->GetAssetTags();
    for (const auto& Tag : Tags)
        if (Matches(Tag, TEXT("Shared.Ability.Death")) || Matches(Tag, TEXT("Shared.Ability.HitReact"))) return false;
    const auto Lock = FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.TargetLock"), false);
    return !(Tags.Num() == 1 && Lock.IsValid() && Tags.HasTagExact(Lock));
}
bool FRPGRidingAbilityGate::IsBlocked(const AActor* Actor, const UGameplayAbility* Ability)
{ return Mounted(Actor) && IsManualAbility(Ability); }
bool FRPGRidingAbilityGate::BlocksMounting(const UGameplayAbility* Ability)
{
    if (IsManualAbility(Ability)) return true;
    if (Ability) for (const auto& Tag : Ability->GetAssetTags()) if (Matches(Tag, TEXT("Shared.Ability.HitReact"))) return true;
    return false;
}

void FRPGRidingAbilityGate::Reset()
{
    Avatar.Reset(); Epoch.Invalidate(); SuppressedSpecs.Reset(); SuppressedTags.Reset();
}
void FRPGRidingAbilityGate::Synchronize(UAbilitySystemComponent& ASC)
{
    AActor* Current = ASC.GetAvatarActor();
    const auto* Riding = IsValid(Current) ? Current->FindComponentByClass<URPGRidingComponent>() : nullptr;
    const auto CurrentEpoch = Riding ? Riding->GetRidingState().Key.RiderEpoch : FGuid{};
    if (Current != Avatar.Get() || CurrentEpoch != Epoch) { Reset(); Avatar = Current; Epoch = CurrentEpoch; }
    for (auto It = SuppressedSpecs.CreateIterator(); It; ++It)
        if (!ASC.FindAbilitySpecFromHandle(*It)) It.RemoveCurrent();
}

bool FRPGRidingAbilityGate::ConsumeTagPress(UAbilitySystemComponent& ASC, const FGameplayTag& Input)
{
    Synchronize(ASC);
    if (!Input.IsValid()) return false;
    if (SuppressedTags.HasTagExact(Input)) return true;
    bool bBasic = BasicInput(Input);
    for (const auto& Spec : ASC.GetActivatableAbilities())
        if (Spec.GetDynamicSpecSourceTags().HasTagExact(Input) && BasicSpec(Spec) && IsManualAbility(Spec.Ability)) bBasic = true;
    if (!bBasic) return false;
    auto* Riding = Mounted(Avatar.Get()); if (!Riding) return false;
    SuppressedTags.AddTag(Input);
    for (auto& Spec : ASC.GetActivatableAbilities())
        if (Spec.GetDynamicSpecSourceTags().HasTagExact(Input)) { SuppressedSpecs.Add(Spec.Handle); Spec.InputPressed = false; }
    Riding->RequestSetMounted(false, {}); // The existing reliable request still performs server binding/revision/budget checks.
    return true; // Never replay this press, even if a listen server dismounted synchronously.
}

bool FRPGRidingAbilityGate::ConsumeSpecPress(UAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle)
{
    Synchronize(ASC);
    auto* Spec = ASC.FindAbilitySpecFromHandle(Handle);
    if (!Spec) return false;
    if (SuppressedSpecs.Contains(Handle)) { Spec->InputPressed = false; return true; }
    auto* Riding = Mounted(Avatar.Get());
    if (!Riding) return false;
    if (IsManualAbility(Spec->Ability) && BasicSpec(*Spec))
    {
        Suppress(ASC, *Spec, SuppressedSpecs, SuppressedTags);
        Riding->RequestSetMounted(false, {});
        return true;
    }
    if (IsBlocked(Avatar.Get(), Spec->Ability)) { Suppress(ASC, *Spec, SuppressedSpecs, SuppressedTags); return true; }
    return false;
}

void FRPGRidingAbilityGate::ReleaseTag(UAbilitySystemComponent& ASC, const FGameplayTag& Input)
{
    Synchronize(ASC); SuppressedTags.RemoveTag(Input);
    for (const auto& Spec : ASC.GetActivatableAbilities())
        if (Spec.GetDynamicSpecSourceTags().HasTagExact(Input)) SuppressedSpecs.Remove(Spec.Handle);
}
void FRPGRidingAbilityGate::ReleaseSpec(UAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle)
{
    Synchronize(ASC); SuppressedSpecs.Remove(Handle);
    if (const auto* Spec = ASC.FindAbilitySpecFromHandle(Handle))
    {
        FGameplayTagContainer Inputs;
        for (const auto& Tag : Spec->GetDynamicSpecSourceTags()) if (Matches(Tag, TEXT("InputTag"))) Inputs.AddTag(Tag);
        SuppressedTags.RemoveTags(Inputs);
        // A handle release must balance the whole logical input group suppressed by that press.
        for (const auto& Other : ASC.GetActivatableAbilities())
            if (Other.GetDynamicSpecSourceTags().HasAnyExact(Inputs)) SuppressedSpecs.Remove(Other.Handle);
    }
}
