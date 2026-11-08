#include "UI/ActionBar/RPGActionBarContext.h"
#include "UI/ActionBar/RPGActionBarLayout.h"
#include "Component/UI/QuickSlotComponent.h"
#include "GameFramework/Pawn.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/UI/RPGSkillWindowComponent.h"
#include "UI/RPGWindowInputState.h"
#include "GameFramework/PlayerController.h"

bool URPGActionBarProfile::IsValidProfile() const
{
    if (ProfileId.IsNone() || Actions.Num() > 8) return false;
    TSet<FName> Ids;
    TSet<int32> Slots;
    FGameplayTagContainer Tags;
    for (const FRPGActionBarEntry& Entry : Actions)
    {
        if (Entry.ActionId.IsNone() || Ids.Contains(Entry.ActionId) || Entry.SkillSlot < INDEX_NONE || Entry.SkillSlot >= 8)
            return false;
        Ids.Add(Entry.ActionId);
        if (Entry.SkillSlot != INDEX_NONE)
        {
            if (Slots.Contains(Entry.SkillSlot)) return false;
            Slots.Add(Entry.SkillSlot);
        }
        if (Entry.InputTag.IsValid())
        {
            if (Tags.HasTagExact(Entry.InputTag)) return false;
            Tags.AddTag(Entry.InputTag);
        }
    }
    TArray<int32> Order;
    return TryBuildDisplayOrder(Order);
}

bool URPGActionBarProfile::TryBuildDisplayOrder(TArray<int32>& OutOrder) const
{
    TArray<int32> DisplaySlots;
    for (const auto& Entry : Actions) DisplaySlots.Add(Entry.DisplaySlot);
    return RPGActionBarLayout::Resolve(DisplaySlots, NativeSlotCount, OutOrder);
}

namespace
{
FName SlotSource(int32 Slot) { return FName(*FString::Printf(TEXT("Slot.%d"), Slot)); }
FName TagSource(FGameplayTag Tag) { return FName(*(TEXT("Tag.") + Tag.ToString())); }
}

UQuickSlotComponent* URPGActionBarContext::GetSlots() const { return Cast<UQuickSlotComponent>(GetOuter()); }

bool URPGActionBarContext::IsProviderCurrent() const
{
    const UQuickSlotComponent* Slots = GetSlots();
    const APawn* Pawn = Slots ? Cast<APawn>(Slots->GetOwner()) : nullptr;
    return Provider && IsValid(Pawn) && !Pawn->IsActorBeingDestroyed()
        && Provider->GetActionBarPawn() == Pawn && Profile;
}

const FRPGActionBarEntry* URPGActionBarContext::FindAction(FName Id) const
{
    return Profile ? Profile->Actions.FindByPredicate([Id](const auto& Entry) { return Entry.ActionId == Id; }) : nullptr;
}

bool URPGActionBarContext::Activate(TSharedRef<IRPGActionBarProvider> InProvider)
{
    UQuickSlotComponent* Slots = GetSlots();
    const URPGActionBarProfile* NextProfile = InProvider->GetActionBarProfile();
    APawn* Pawn = InProvider->GetActionBarPawn();
    if (!Slots || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || Pawn != Slots->GetOwner()
        || !IsValid(NextProfile) || !NextProfile->IsValidProfile()) return false;
    if (Provider == InProvider && Profile == NextProfile) return !bShuttingDown && !bTransitioning;
    return ChangeProvider(InProvider);
}

bool URPGActionBarContext::Deactivate(const TSharedRef<IRPGActionBarProvider>& ExpectedProvider)
{
    return Provider == ExpectedProvider && ChangeProvider(nullptr);
}

bool URPGActionBarContext::ChangeProvider(TSharedPtr<IRPGActionBarProvider> Next)
{
    if (bTransitioning || bShuttingDown) return false;
    {
        TGuardValue<bool> Guard(bTransitioning, true);
        ++Revision; // invalidate old pointer callbacks before any release callback
        ReleaseAll(true);
        if (UQuickSlotComponent* Slots = GetSlots()) Slots->ReleaseAllSkillInputs(true);
        if (bShuttingDown) return false;
        Provider = MoveTemp(Next);
        Profile = Provider ? const_cast<URPGActionBarProfile*>(Provider->GetActionBarProfile()) : nullptr;
        if (Provider && !IsProviderCurrent()) { Provider.Reset(); Profile = nullptr; }
    }
    OnChanged.Broadcast();
    return true;
}

void URPGActionBarContext::Shutdown()
{
    if (bShuttingDown) return;
    bShuttingDown = true;
    ++Revision;
    ReleaseAll();
    Provider.Reset();
    Profile = nullptr;
    DownInputs.Reset();
    OnChanged.Broadcast();
    OnChanged.Clear();
}

FRPGActionBarActionView URPGActionBarContext::ReadAction(FName Id) const
{
    if (bTransitioning || bShuttingDown || !IsProviderCurrent() || !FindAction(Id)) return {};
    const APawn* Pawn = Provider->GetActionBarPawn();
    if (!Pawn || !Pawn->IsLocallyControlled()) return {};
    const auto* PC = Cast<APlayerController>(Pawn->GetController());
    const auto* Windows = PC ? PC->FindComponentByClass<URPGSkillWindowComponent>() : nullptr;
    const auto InputState = Windows ? Windows->GetWindowInputState() : nullptr;
    if (InputState && InputState->HasOpenWindows()) return {};
    const auto Current = Provider;
    const uint64 ReadRevision = Revision;
    auto View = Current->ReadAction(Id);
    View.CooldownRemaining = FMath::IsFinite(View.CooldownRemaining) ? FMath::Max(0.f, View.CooldownRemaining) : 0.f;
    View.CooldownDuration = FMath::IsFinite(View.CooldownDuration) ? FMath::Max(0.f, View.CooldownDuration) : 0.f;
    if (Provider != Current || Revision != ReadRevision || !IsProviderCurrent()) return {};
    return View;
}

FRPGActionBarIdentityView URPGActionBarContext::ReadIdentity() const
{
    if (bTransitioning || bShuttingDown || !IsProviderCurrent()) return {};
    const APawn* Pawn = Provider->GetActionBarPawn();
    if (!Pawn || !Pawn->IsLocallyControlled()) return {};
    const AController* ControllerBefore = Pawn->GetController();
    const auto Current = Provider;
    const uint64 ReadRevision = Revision;
    auto View = Current->ReadIdentity();
    if (Provider != Current || Revision != ReadRevision || !IsProviderCurrent()
        || !Pawn->IsLocallyControlled() || Pawn->GetController() != ControllerBefore) return {};
    return View;
}

void URPGActionBarContext::PressAction(FName Source, FName Id, uint64 ExpectedRevision)
{
    if (Source.IsNone() || bTransitioning || bShuttingDown || Revision != ExpectedRevision
        || DownInputs.Contains(Source)) return;
    DownInputs.Add(Source);
    if (!IsProviderCurrent() || !FindAction(Id) || !ReadAction(Id).bAvailable || Revision != ExpectedRevision) return;
    const bool bAlreadyPressed = PressedActions.FindKey(Id) != nullptr;
    PressedActions.Add(Source, Id);
    const auto Current = Provider;
    if (!bAlreadyPressed) Current->PressAction(Id);
}

void URPGActionBarContext::ReleaseInput(FName Source)
{
    DownInputs.Remove(Source);
    FName Id;
    if (!PressedActions.RemoveAndCopyValue(Source, Id)) return;
    const auto Current = Provider;
    if (Current && !PressedActions.FindKey(Id)) Current->ReleaseAction(Id);
}

void URPGActionBarContext::ReleaseAll(bool bKeepPhysicalState)
{
    if (!bKeepPhysicalState) DownInputs.Reset();
    TSet<FName> Ids;
    for (const auto& Pair : PressedActions) Ids.Add(Pair.Value);
    PressedActions.Reset();
    const auto Current = Provider;
    // DownInputs survives a mode switch: held keys need a fresh release/press.
    if (Current) for (FName Id : Ids) Current->ReleaseAction(Id);
    const auto Tags = BaseHeldTags.Array();
    BaseHeldTags.Reset();
    if (const auto* Slots = GetSlots())
        if (auto* ASC = Cast<URPGAbilitySystemComponent>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Slots->GetOwner())))
            for (const auto Tag : Tags) ASC->OnAbilityInputReleased(Tag);
}

void URPGActionBarContext::PressSlot(int32 Slot)
{
    if (Slot < 0 || Slot >= 8) return;
    const auto* Entry = Profile ? Profile->Actions.FindByPredicate([Slot](const auto& E) { return E.SkillSlot == Slot; }) : nullptr;
    PressAction(SlotSource(Slot), Entry ? Entry->ActionId : NAME_None, Revision);
}
void URPGActionBarContext::ReleaseSlot(int32 Slot) { ReleaseInput(SlotSource(Slot)); }
bool URPGActionBarContext::PressTag(FGameplayTag Tag)
{
    if (!Tag.IsValid()) return true;
    const bool bConsumed = HasOverride() || DownInputs.Contains(TagSource(Tag)) || bTransitioning || bShuttingDown;
    const auto* Entry = Profile ? Profile->Actions.FindByPredicate([Tag](const auto& E) { return E.InputTag == Tag; }) : nullptr;
    PressAction(TagSource(Tag), Entry ? Entry->ActionId : NAME_None, Revision);
    if (!bConsumed) BaseHeldTags.Add(Tag);
    return bConsumed;
}
void URPGActionBarContext::ReleaseTag(FGameplayTag Tag)
{
    BaseHeldTags.Remove(Tag);
    ReleaseInput(TagSource(Tag));
}
