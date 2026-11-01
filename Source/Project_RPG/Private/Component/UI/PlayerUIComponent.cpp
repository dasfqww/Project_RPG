// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/UI/PlayerUIComponent.h"
#include "GameFramework/Controller.h"

namespace
{
bool PrepareProgress(const FRPGActionProgressData& Input, FRPGActionProgressSnapshot& Out)
{
	if (Input.Kind > ERPGActionProgressKind::Interaction
		|| Input.Phase > ERPGActionProgressPhase::AwaitingConfirmation
		|| Input.TimeDisplay > ERPGActionProgressTimeDisplay::Remaining
		|| !FMath::IsFinite(Input.ElapsedSeconds) || Input.ElapsedSeconds < 0.f
		|| Input.Stage < 0 || Input.StageCount < 0 || Input.Stage > Input.StageCount
		|| (Input.bHasDuration && (!FMath::IsFinite(Input.DurationSeconds) || Input.DurationSeconds <= 0.f))
		|| (!Input.bHasDuration && Input.TimeDisplay == ERPGActionProgressTimeDisplay::Remaining))
	{
		return false;
	}
	Out.Data = Input;
	if (!Input.bHasDuration) Out.Data.DurationSeconds = 0.f;
	const double Ratio = Input.bHasDuration
		? FMath::Clamp(static_cast<double>(Input.ElapsedSeconds) / Input.DurationSeconds, 0.0, 1.0) : 0.0;
	Out.Fraction = Input.bHasDuration ? static_cast<float>(Input.bReverseFill ? 1.0 - Ratio : Ratio) : 0.f;
	Out.DisplaySeconds = Input.TimeDisplay == ERPGActionProgressTimeDisplay::Elapsed ? Input.ElapsedSeconds
		: Input.TimeDisplay == ERPGActionProgressTimeDisplay::Remaining
			? FMath::Max(0.f, Input.DurationSeconds - Input.ElapsedSeconds) : 0.f;
	return true;
}
}

void UPlayerUIComponent::OnRegister()
{
	Super::OnRegister();
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
		Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::OnProgressControllerChanged);
	bAcceptActionProgress = true;
}

void UPlayerUIComponent::OnUnregister()
{
	bAcceptActionProgress = false;
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
		Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::OnProgressControllerChanged);
	InvalidateActionProgress();
	Super::OnUnregister();
}

void UPlayerUIComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bAcceptActionProgress = false;
	InvalidateActionProgress();
	Super::EndPlay(Reason);
}

bool UPlayerUIComponent::HasCurrentProgressSource() const
{
	if (!bAcceptActionProgress || !IsRegistered() || !ActionProgressSource.IsValid()) return false;
	if (bTrackProgressController)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());
		if (!IsValid(Pawn) || ActionProgressController.IsStale()
			|| Pawn->GetController() != ActionProgressController.Get()) return false;
	}
	return true;
}

FRPGActionProgressSnapshot UPlayerUIComponent::GetActionProgressSnapshot() const
{
	FRPGActionProgressSnapshot Snapshot = ActionProgress;
	if (Snapshot.bActive && !HasCurrentProgressSource())
	{
		Snapshot.bActive = false;
		Snapshot.EndReason = ERPGActionProgressEndReason::SourceLost;
	}
	return Snapshot;
}

FRPGActionProgressHandle UPlayerUIComponent::BeginActionProgress(UObject* Source, const FRPGActionProgressData& Data)
{
	FRPGActionProgressSnapshot Next;
	if (!bAcceptActionProgress || !IsRegistered() || !IsValid(Source) || Source->IsTemplate()
		|| !PrepareProgress(Data, Next)) return {};
	Next.Handle.Id = FGuid::NewGuid();
	Next.bActive = true;
	const FRPGActionProgressHandle Handle = Next.Handle;
	ActionProgress = MoveTemp(Next);
	ActionProgressSource = Source;
	const APawn* Pawn = Cast<APawn>(GetOwner());
	bTrackProgressController = Pawn != nullptr;
	ActionProgressController = Pawn ? Pawn->GetController() : nullptr;
	NotifyActionProgressChanged();
	// A listener may have started a different action; return this begin's handle, never that action's handle.
	return Handle;
}

bool UPlayerUIComponent::UpdateActionProgress(const FRPGActionProgressHandle& Handle, const FRPGActionProgressData& Data)
{
	if (!Handle.IsValid() || !ActionProgress.bActive || !(ActionProgress.Handle == Handle)
		|| !HasCurrentProgressSource() || Data.Kind != ActionProgress.Data.Kind) return false;
	FRPGActionProgressSnapshot Next;
	if (!PrepareProgress(Data, Next)) return false;
	Next.Handle = Handle;
	Next.bActive = true;
	ActionProgress = MoveTemp(Next);
	NotifyActionProgressChanged();
	return true;
}

bool UPlayerUIComponent::EndActionProgress(const FRPGActionProgressHandle& Handle, ERPGActionProgressEndReason Reason)
{
	if (!Handle.IsValid() || !ActionProgress.bActive || !(ActionProgress.Handle == Handle)
		|| !HasCurrentProgressSource() || Reason < ERPGActionProgressEndReason::Completed
		|| Reason > ERPGActionProgressEndReason::Interrupted) return false;
	ActionProgress.bActive = false;
	ActionProgress.EndReason = Reason;
	ActionProgressSource.Reset();
	ActionProgressController.Reset();
	NotifyActionProgressChanged();
	return true;
}

void UPlayerUIComponent::InvalidateActionProgress()
{
	const bool bWasActive = ActionProgress.bActive;
	if (bWasActive)
	{
		ActionProgress.bActive = false;
		ActionProgress.EndReason = ERPGActionProgressEndReason::SourceLost;
	}
	ActionProgressSource.Reset();
	ActionProgressController.Reset();
	if (bWasActive) NotifyActionProgressChanged();
}

void UPlayerUIComponent::OnProgressControllerChanged(APawn* Pawn, AController* OldController, AController* NewController)
{
	// Another controller-change listener may already have begun the new owner's action.
	if (Pawn == GetOwner() && ActionProgress.bActive && !HasCurrentProgressSource()) InvalidateActionProgress();
}

void UPlayerUIComponent::NotifyActionProgressChanged()
{
	// No writes after callbacks: a callback can end this action, start another, or unregister the component.
	OnActionProgressChangedNative.Broadcast();
	OnActionProgressChanged.Broadcast();
}

