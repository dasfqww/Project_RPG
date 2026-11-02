#include "Component/UI/RPGInteractionProgressComponent.h"
#include "Component/UI/PlayerUIComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

URPGInteractionProgressComponent::URPGInteractionProgressComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void URPGInteractionProgressComponent::OnRegister()
{
    Super::OnRegister();
    if (auto* PC = Cast<APlayerController>(GetOwner()))
        PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::OnPawnChanged);
}

void URPGInteractionProgressComponent::OnUnregister()
{
    if (auto* PC = Cast<APlayerController>(GetOwner()))
        PC->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::OnPawnChanged);
    EndInteraction(ERPGActionProgressEndReason::SourceLost);
    Super::OnUnregister();
}

void URPGInteractionProgressComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    EndInteraction(ERPGActionProgressEndReason::SourceLost);
    Super::EndPlay(Reason);
}

void URPGInteractionProgressComponent::EndInteraction(ERPGActionProgressEndReason Reason)
{
    ++Revision;
    const auto Previous = Handle;
    auto* PreviousUI = UI.Get();
    if (auto* Target = SourceTarget.Get()) Target->OnEndPlay.RemoveDynamic(this, &ThisClass::OnTargetEndPlay);
    Handle = {}; UI.Reset(); SourcePawn.Reset(); SourceTarget.Reset(); Data = {};
    // Clear ownership before callbacks; never finish a replacement display handle.
    // SourceLost is generated internally by PlayerUIComponent; public producers
    // report interruption when their target/component lifetime ends.
    const auto PublicReason = Reason == ERPGActionProgressEndReason::SourceLost
        ? ERPGActionProgressEndReason::Interrupted : Reason;
    if (PreviousUI) PreviousUI->EndActionProgress(Previous, PublicReason);
}

void URPGInteractionProgressComponent::BeginInteraction(APawn* Pawn, AActor* Target, const FText& Label, float Duration)
{
    const uint64 Before = Revision;
    EndInteraction(ERPGActionProgressEndReason::Cancelled);
    if (Revision != Before + 1) return;
    auto* PC = Cast<APlayerController>(GetOwner());
    if (!IsRegistered() || !PC || !PC->IsLocalController() || PC->GetPawn() != Pawn
        || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !IsValid(Target) || Target->IsActorBeingDestroyed()
        || Target->GetWorld() != Pawn->GetWorld() || !FMath::IsFinite(Duration) || Duration <= 0.f) return;
    auto* Source = Pawn->FindComponentByClass<UPlayerUIComponent>();
    if (!Source || !Source->IsRegistered()) return;
    const uint64 Current = ++Revision;
    UI = Source; SourcePawn = Pawn; SourceTarget = Target;
    Target->OnEndPlay.AddUniqueDynamic(this, &ThisClass::OnTargetEndPlay);
    Data.Kind = ERPGActionProgressKind::Interaction;
    Data.Label = Label.IsEmptyOrWhitespace() ? NSLOCTEXT("RPGInteraction", "ProgressLabel", "Interaction") : Label;
    Data.bHasDuration = true; Data.DurationSeconds = Duration;
    Data.TimeDisplay = ERPGActionProgressTimeDisplay::Remaining;
    const auto Created = Source->BeginActionProgress(this, Data);
    if (Revision == Current) Handle = Created;
    else Source->EndActionProgress(Created, ERPGActionProgressEndReason::Cancelled);
}

void URPGInteractionProgressComponent::UpdateInteraction(float DeltaSeconds)
{
    if (!Handle.IsValid()) return;
    auto* PC = Cast<APlayerController>(GetOwner());
    auto* Pawn = SourcePawn.Get(); auto* Target = SourceTarget.Get(); auto* Source = UI.Get();
    if (!PC || PC->GetPawn() != Pawn || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed()
        || !IsValid(Target) || Target->IsActorBeingDestroyed() || !Source || !Source->IsRegistered())
    {
        EndInteraction(ERPGActionProgressEndReason::SourceLost);
        return;
    }
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f) return;
    const uint64 Current = Revision;
    Data.ElapsedSeconds = FMath::Min(Data.DurationSeconds, Data.ElapsedSeconds + DeltaSeconds);
    if (!Source->UpdateActionProgress(Handle, Data) && Revision == Current)
        EndInteraction(ERPGActionProgressEndReason::Interrupted);
}

void URPGInteractionProgressComponent::OnPawnChanged(APawn*, APawn*)
{
    auto* PC = Cast<APlayerController>(GetOwner());
    if (!PC || PC->GetPawn() != SourcePawn.Get()) EndInteraction(ERPGActionProgressEndReason::SourceLost);
}

void URPGInteractionProgressComponent::OnTargetEndPlay(AActor* Actor, EEndPlayReason::Type)
{
    if (Actor == SourceTarget.Get()) EndInteraction(ERPGActionProgressEndReason::SourceLost);
}
