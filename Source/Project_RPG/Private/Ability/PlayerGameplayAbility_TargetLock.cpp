// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/PlayerGameplayAbility_TargetLock.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Character/RPGPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "UI/RPGWidgetBase.h"
#include "Controller/RPGPlayerController.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
#include "RPGGameplayTags.h"
#include "Kismet/KismetMathLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Component/RPGCharacterMovementComponent.h"
#include "EnhancedInputSubsystems.h"

#include "RPGDebugHelper.h"

void UPlayerGameplayAbility_TargetLock::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!IsValid(GetPlayerCharacterFromActorInfo()) ||
		!IsValid(GetPlayerControllerFromActorInfo()) ||
		!GetPlayerControllerFromActorInfo()->GetLocalPlayer())
	{
		CancelTargetLockAbility();
		return;
	}
	TryLockOnTarget();
	// Target discovery/widget setup can cancel and clean up this activation.
	if (!IsActive()) return;
	InitTargetLockMappingContext();
	if (!AppliedTargetLockInputSubsystem.IsValid())
	{
		CancelTargetLockAbility();
		return;
	}
	InitTargetLockMovement();

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UPlayerGameplayAbility_TargetLock::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsEndAbilityValid(Handle, ActorInfo)) return;
	ResetTargetLockMovement();
	ResetTargetLockMappingContext();
	CleanUp();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UPlayerGameplayAbility_TargetLock::OnTargetLockTick(float DeltaTime)
{
	if (!IsActive()) return;
	if (!IsValid(CurrentLockedActor) ||
		!IsValid(GetPlayerCharacterFromActorInfo()) ||
		!IsValid(GetPlayerControllerFromActorInfo()) ||
		URPGAbilityFunctionLibrary::NativeDoesActorHaveTag(CurrentLockedActor,RPGGameplayTags::Shared_Status_Death) ||
		URPGAbilityFunctionLibrary::NativeDoesActorHaveTag(GetPlayerCharacterFromActorInfo(), 
			RPGGameplayTags::Shared_Status_Death)
		)
	{
		CancelTargetLockAbility();
		return;
	}

	SetTargetLockWidgetPosition();
	if (!IsActive()) return;

	const bool bShouldOverrideRotation =
		!URPGAbilityFunctionLibrary::NativeDoesActorHaveTag(GetPlayerCharacterFromActorInfo(),
			RPGGameplayTags::Player_Status_Rolling) &&
		!URPGAbilityFunctionLibrary::NativeDoesActorHaveTag(GetPlayerCharacterFromActorInfo(),
			RPGGameplayTags::Player_Status_Blocking);

	if (bShouldOverrideRotation)
	{
		FRotator LookAtRot = UKismetMathLibrary::FindLookAtRotation(
			GetPlayerCharacterFromActorInfo()->GetActorLocation(),
			CurrentLockedActor->GetActorLocation()
		);

		LookAtRot -= FRotator(TargetLockCameraOffsetDistance, 0.f, 0.f);

		const FRotator CurrentControlRot = GetPlayerCharacterFromActorInfo()->GetControlRotation();
		const FRotator TargetRot = FMath::RInterpTo(CurrentControlRot, LookAtRot,
			DeltaTime, TargetLockRotationInterpSpeed);

		GetPlayerControllerFromActorInfo()->SetControlRotation(FRotator(TargetRot.Pitch, TargetRot.Yaw, 0.f));
		GetPlayerCharacterFromActorInfo()->SetActorRotation(FRotator(0.f, TargetRot.Yaw, 0.f));
	}
}

void UPlayerGameplayAbility_TargetLock::SwitchTarget(const FGameplayTag& InSwitchDirectionTag)
{
	if (!IsActive()) return;
	GetAvailableActorsToLock();

	TArray<AActor*> ActorsOnLeft;
	TArray<AActor*> ActorsOnRight;
	AActor* NewTargetToLock = nullptr;

	GetAvailableActorsAroundTarget(ActorsOnLeft, ActorsOnRight);
	if (!IsActive()) return;

	if (InSwitchDirectionTag == RPGGameplayTags::Player_Event_SwitchTarget_Left)
	{
		NewTargetToLock = GetNearestTargetFromAvailableActors(ActorsOnLeft);
	}
	else
	{
		NewTargetToLock = GetNearestTargetFromAvailableActors(ActorsOnRight);
	}

	if (NewTargetToLock)
	{
		CurrentLockedActor = NewTargetToLock;
		PublishLockedTarget();
	}
}

void UPlayerGameplayAbility_TargetLock::TryLockOnTarget()
{
	GetAvailableActorsToLock();

	if (AvailableActorsToLock.IsEmpty())
	{
		CancelTargetLockAbility();
		return;
	}

	CurrentLockedActor = GetNearestTargetFromAvailableActors(AvailableActorsToLock);

	if (CurrentLockedActor)
	{
		PublishLockedTarget();
		DrawTargetLockWidget();

		SetTargetLockWidgetPosition();

		
	}
	else
	{
		CancelTargetLockAbility();
	}
}

void UPlayerGameplayAbility_TargetLock::GetAvailableActorsToLock()
{
	AvailableActorsToLock.Empty();

	TArray<FHitResult> BoxTraceHits;

	UKismetSystemLibrary::BoxTraceMultiForObjects(
		GetPlayerCharacterFromActorInfo(),
		GetPlayerCharacterFromActorInfo()->GetActorLocation(),
		GetPlayerCharacterFromActorInfo()->GetActorLocation() 
		+ GetPlayerCharacterFromActorInfo()->GetActorForwardVector() * BoxTraceDistance,
		TraceBoxSize / 2.f,
		GetPlayerCharacterFromActorInfo()->GetActorForwardVector().ToOrientationRotator(),
		BoxTraceChannel,
		false,
		TArray<AActor*>(),
		bShowPersistentDebugShape ? EDrawDebugTrace::Persistent : EDrawDebugTrace::None,
		BoxTraceHits,
		true
	);

	for (const FHitResult& TraceHit : BoxTraceHits)
	{
		if (AActor* HitActor = TraceHit.GetActor())
		{
			if (HitActor != GetPlayerCharacterFromActorInfo())
			{
				AvailableActorsToLock.AddUnique(HitActor);

				
			}
		}
	}
}

AActor* UPlayerGameplayAbility_TargetLock::GetNearestTargetFromAvailableActors(const TArray<AActor*>& InAvailableActors)
{
	float ClosestDistance = 0.f;
	return UGameplayStatics::FindNearestActor(GetPlayerCharacterFromActorInfo()->GetActorLocation(), InAvailableActors, ClosestDistance);
}

void UPlayerGameplayAbility_TargetLock::GetAvailableActorsAroundTarget(TArray<AActor*>& OutActorsOnLeft,
	TArray<AActor*>& OutActorsOnRight)
{
	if (!IsValid(CurrentLockedActor) || AvailableActorsToLock.IsEmpty())
	{
		CancelTargetLockAbility();
		return;
	}

	const FVector PlayerLocation = GetPlayerCharacterFromActorInfo()->GetActorLocation();
	const FVector PlayerToCurrentNormalized = (CurrentLockedActor->GetActorLocation() - PlayerLocation).GetSafeNormal();

	for (AActor* AvailableActor : AvailableActorsToLock)
	{
		if (!IsValid(AvailableActor) || AvailableActor == CurrentLockedActor) continue;

		const FVector PlayerToAvailableNormalized = (AvailableActor->GetActorLocation() - PlayerLocation).GetSafeNormal();

		const FVector CrossResult = FVector::CrossProduct(PlayerToCurrentNormalized, PlayerToAvailableNormalized);

		if (CrossResult.Z > 0.f)
		{
			OutActorsOnRight.AddUnique(AvailableActor);
		}
		else
		{
			OutActorsOnLeft.AddUnique(AvailableActor);
		}
	}
}

void UPlayerGameplayAbility_TargetLock::DrawTargetLockWidget()
{
	if (!DrawnTargetLockWidget)
	{
		checkf(TargetLockWidgetClass, TEXT("Forgot to assign a valid widget class in Blueprint"));

		DrawnTargetLockWidget = CreateWidget<URPGWidgetBase>(GetPlayerControllerFromActorInfo(), TargetLockWidgetClass);

		check(DrawnTargetLockWidget);

		DrawnTargetLockWidget->AddToViewport();
	}
}

void UPlayerGameplayAbility_TargetLock::SetTargetLockWidgetPosition()
{
	if (!IsValid(DrawnTargetLockWidget) || !IsValid(CurrentLockedActor))
	{
		CancelTargetLockAbility();
		return;
	}

	FVector2D ScreenPosition;
	UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
		GetPlayerControllerFromActorInfo(),
		CurrentLockedActor->GetActorLocation(),
		ScreenPosition,
		true
	);

	if (TargetLockWidgetSize == FVector2D::ZeroVector)
	{
		DrawnTargetLockWidget->WidgetTree->ForEachWidget(
			[this](UWidget* FoundWidget)
			{
				if (USizeBox* FoundSizeBox = Cast<USizeBox>(FoundWidget))
				{
					TargetLockWidgetSize.X = FoundSizeBox->GetWidthOverride();
					TargetLockWidgetSize.Y = FoundSizeBox->GetHeightOverride();
				}
			}
		);
	}

	ScreenPosition -= (TargetLockWidgetSize / 2.f);

	DrawnTargetLockWidget->SetPositionInViewport(ScreenPosition, false);
}

void UPlayerGameplayAbility_TargetLock::InitTargetLockMovement()
{
	ResetTargetLockMovement();
	TargetLockMovement = Cast<URPGCharacterMovementComponent>(GetPlayerCharacterFromActorInfo()->GetCharacterMovement());
	if (TargetLockMovement.IsValid()) TargetLockSpeedLimit = TargetLockMovement->BeginWalkSpeedLimit(TargetLockMaxWalkSpeed);
}

void UPlayerGameplayAbility_TargetLock::InitTargetLockMappingContext()
{
	const ARPGPlayerController* Controller = GetPlayerControllerFromActorInfo();
	const ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer
		? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer) : nullptr;
	if (!Subsystem || !TargetLockMappingContext) return;
	Subsystem->AddMappingContext(TargetLockMappingContext, 3);
	AppliedTargetLockInputSubsystem = Subsystem;
}

void UPlayerGameplayAbility_TargetLock::CancelTargetLockAbility()
{
	CancelAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true);

}

void UPlayerGameplayAbility_TargetLock::CleanUp()
{
	AvailableActorsToLock.Empty();

	if (ARPGPlayerController* PlayerController =
		GetPlayerControllerFromActorInfo())
	{
		PlayerController->SetSkillLockedTarget(nullptr);
	}
	CurrentLockedActor = nullptr;

	if (DrawnTargetLockWidget)
	{
		DrawnTargetLockWidget->RemoveFromParent();
	}

	DrawnTargetLockWidget = nullptr;

	TargetLockWidgetSize = FVector2D::ZeroVector;

	ResetTargetLockMovement();
}

void UPlayerGameplayAbility_TargetLock::PublishLockedTarget()
{
	if (ARPGPlayerController* PlayerController =
		GetPlayerControllerFromActorInfo())
	{
		PlayerController->SetSkillLockedTarget(CurrentLockedActor);
	}
}

void UPlayerGameplayAbility_TargetLock::ResetTargetLockMovement()
{
	if (TargetLockMovement.IsValid()) TargetLockMovement->EndWalkSpeedLimit(TargetLockSpeedLimit);
	TargetLockSpeedLimit.Invalidate(); TargetLockMovement.Reset();
}

void UPlayerGameplayAbility_TargetLock::ResetTargetLockMappingContext()
{
	// A cancelled target search never installed a context. Only release the
	// subsystem actually used by this activation, not a replacement controller.
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = AppliedTargetLockInputSubsystem.Get())
	{
		Subsystem->RemoveMappingContext(TargetLockMappingContext);
	}
	AppliedTargetLockInputSubsystem.Reset();
}
