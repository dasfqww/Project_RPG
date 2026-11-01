#pragma once
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/RootMotionSource.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

namespace RPGCombatMotion
{
	inline double ServerNow(const UWorld* World)
	{
		const auto* Clock = World ? World->GetGameState() : nullptr;
		return Clock ? Clock->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.);
	}
	inline bool Supported(const UCharacterMovementComponent* Movement)
	{
		const auto* Character = Movement ? Movement->GetCharacterOwner() : nullptr;
		return Movement && Movement->IsActive() && Movement->UpdatedComponent
			&& Movement->UpdatedComponent->IsQueryCollisionEnabled() && Character
			&& (Character->GetController() || Movement->bRunPhysicsWithNoController || Character->GetLocalRole() == ROLE_SimulatedProxy)
			&& (Movement->MovementMode == MOVE_Walking || Movement->MovementMode == MOVE_NavWalking || Movement->MovementMode == MOVE_Falling);
	}
	inline bool ForeignMotion(const UCharacterMovementComponent* Movement, FName Owned)
	{
		const auto Foreign = [Owned](const TSharedPtr<FRootMotionSource>& Source)
		{
			return Source.IsValid() && (Owned.IsNone() || Source->InstanceName != Owned)
				&& !Source->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval)
				&& !Source->Status.HasFlag(ERootMotionSourceStatusFlags::Finished);
		};
		const auto* Character = Movement ? Movement->GetCharacterOwner() : nullptr;
		return Movement && ((Character && Character->GetMesh() && Character->GetMesh()->IsPlayingRootMotion())
			|| Movement->CurrentRootMotion.RootMotionSources.ContainsByPredicate(Foreign)
			|| Movement->CurrentRootMotion.PendingAddRootMotionSources.ContainsByPredicate(Foreign));
	}
	/** Retire exactly one source, including prediction replay. Preserve gravity and foreign motion. */
	inline void Retire(ACharacter* Character, FName Name)
	{
		auto* Movement = Character ? Character->GetCharacterMovement() : nullptr;
		if (!Movement || Name.IsNone()) return;
		const bool bOwned = Movement->GetRootMotionSource(Name).IsValid();
		const bool bForeign = ForeignMotion(Movement, Name);
		Movement->RemoveRootMotionSource(Name);
		Character->SavedRootMotion.RemoveRootMotionSource(Name);
		if (Character->GetLocalRole() == ROLE_AutonomousProxy && Movement->HasPredictionData_Client())
		{
			auto* Prediction = Movement->GetPredictionData_Client_Character();
			for (const auto& Move : Prediction->SavedMoves) if (Move.IsValid()) Move->SavedRootMotion.RemoveRootMotionSource(Name);
			if (Prediction->PendingMove.IsValid()) Prediction->PendingMove->SavedRootMotion.RemoveRootMotionSource(Name);
			if (Prediction->LastAckedMove.IsValid()) Prediction->LastAckedMove->SavedRootMotion.RemoveRootMotionSource(Name);
		}
		if (bOwned && !bForeign) Movement->Velocity.X = Movement->Velocity.Y = 0.f;
	}
}
