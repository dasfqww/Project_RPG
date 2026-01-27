// Fill out your copyright notice in the Description page of Project Settings.


#include "RPGFunctionLibrary.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Interface/PawnCombatInterface.h"
#include "GenericTeamAgentInterface.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "RPGGameplayTags.h"
#include "Type/RPGCountDownAction.h"
#include "GameInstance/RPGGameInstance.h"
#include "Controller/RPGPlayerController.h"
#include "Component/RPGInventoryComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Components/Widget.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "UI/Inventory/RPGInventoryBase.h"
#include "UI/Inventory/Spatial/RPGInventoryGrid.h"
#include "Blueprint/WidgetBlueprintLibrary.h" 
#include "DrawDebugHelpers.h"
#include "Components/DecalComponent.h"

#include "RPGDebugHelper.h"

URPGAbilitySystemComponent* URPGFunctionLibrary::NativeGetWarriorASCFromActor(AActor* InActor)
{
	if (!IsValid(InActor)) return nullptr;

	return Cast<URPGAbilitySystemComponent>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(InActor));
}

void URPGFunctionLibrary::AddGameplayTagToActorIfNone(AActor* InActor, FGameplayTag TagToAdd)
{
	URPGAbilitySystemComponent* ASC = NativeGetWarriorASCFromActor(InActor);
	if (!ASC) return;

	if (!ASC->HasMatchingGameplayTag(TagToAdd))
	{
		ASC->AddLooseGameplayTag(TagToAdd);
	}
}

void URPGFunctionLibrary::RemoveGameplayTagFromActorIfFound(AActor* InActor, FGameplayTag TagToRemove)
{
	URPGAbilitySystemComponent* ASC = NativeGetWarriorASCFromActor(InActor);
	if (!ASC) return;

	if (ASC->HasMatchingGameplayTag(TagToRemove))
	{
		ASC->RemoveLooseGameplayTag(TagToRemove);
	}
}

bool URPGFunctionLibrary::NativeDoesActorHaveTag(AActor* InActor, FGameplayTag TagToCheck)
{
	URPGAbilitySystemComponent* ASC = NativeGetWarriorASCFromActor(InActor);
	return ASC ? ASC->HasMatchingGameplayTag(TagToCheck) : false;
}

void URPGFunctionLibrary::BP_DoesActorHaveTag(AActor* InActor, FGameplayTag TagToCheck, ERPGConfirmType& OutConfirmType)
{
	OutConfirmType = NativeDoesActorHaveTag(InActor, TagToCheck) ? ERPGConfirmType::Yes : ERPGConfirmType::No;
}

UPawnCombatComponent* URPGFunctionLibrary::NativeGetPawnCombatComponentFromActor(AActor* InActor)
{
	if (!IsValid(InActor)) return nullptr;

	if (IPawnCombatInterface* PawnCombatInterface = Cast<IPawnCombatInterface>(InActor))
	{
		return PawnCombatInterface->GetPawnCombatComponent();
	}

	return nullptr;
}

UPawnCombatComponent* URPGFunctionLibrary::BP_GetPawnCombatComponentFromActor(AActor* InActor, ERPGValidType& OutValidType)
{
	UPawnCombatComponent* CombatComponent = NativeGetPawnCombatComponentFromActor(InActor);
	OutValidType = CombatComponent ? ERPGValidType::Valid : ERPGValidType::Invalid;

	return CombatComponent;
}

bool URPGFunctionLibrary::IsTargetPawnHostile(APawn* QueryPawn, APawn* TargetPawn)
{
	if (!IsValid(QueryPawn) || !IsValid(TargetPawn)) return false;

	IGenericTeamAgentInterface* QueryTeamAgent = Cast<IGenericTeamAgentInterface>(QueryPawn->GetController());
	IGenericTeamAgentInterface* TargetTeamAgent = Cast<IGenericTeamAgentInterface>(TargetPawn->GetController());

	if (QueryTeamAgent && TargetTeamAgent)
	{
		return QueryTeamAgent->GetGenericTeamId() != TargetTeamAgent->GetGenericTeamId();
	}

	return false;
}

float URPGFunctionLibrary::GetScalableFloatValueAtLevel(const FScalableFloat& InScalableFloat, float InLevel)
{
	return InScalableFloat.GetValueAtLevel(InLevel);
}

FGameplayTag URPGFunctionLibrary::ComputeHitReactDirectionTag(AActor* InAttacker, AActor* InVictim, float& OutAngleDifference)
{
	if (!IsValid(InAttacker) || !IsValid(InVictim)) return RPGGameplayTags::Shared_Status_HitReact_Front;

	const FVector VictimForward = InVictim->GetActorForwardVector();
	const FVector VictimToAttackerNormalized = (InAttacker->GetActorLocation() - InVictim->GetActorLocation()).GetSafeNormal();

	const float DotResult = FVector::DotProduct(VictimForward, VictimToAttackerNormalized);
	OutAngleDifference = UKismetMathLibrary::DegAcos(DotResult);

	const FVector CrossResult = FVector::CrossProduct(VictimForward, VictimToAttackerNormalized);

	if (CrossResult.Z < 0.f)
	{
		OutAngleDifference *= -1.f;
	}

	if (OutAngleDifference >= -45.f && OutAngleDifference <= 45.f)
	{
		return RPGGameplayTags::Shared_Status_HitReact_Front;
	}
	else if (OutAngleDifference < -45.f && OutAngleDifference >= -135.f)
	{
		return RPGGameplayTags::Shared_Status_HitReact_Left;
	}
	else if (OutAngleDifference < -135.f || OutAngleDifference>135.f)
	{
		return RPGGameplayTags::Shared_Status_HitReact_Back;
	}
	else if (OutAngleDifference > 45.f && OutAngleDifference <= 135.f)
	{
		return RPGGameplayTags::Shared_Status_HitReact_Right;
	}

	return RPGGameplayTags::Shared_Status_HitReact_Front;
}

bool URPGFunctionLibrary::IsValidBlock(AActor* InAttacker, AActor* InDefender)
{
	if (!IsValid(InAttacker) || !IsValid(InDefender)) return false;

	const float DotResult = FVector::DotProduct(InAttacker->GetActorForwardVector(), InDefender->GetActorForwardVector());
	return DotResult < -0.1f;
}

TArray<FHitResult> URPGFunctionLibrary::DoSphereTrace(UObject* WorldContext, FVector Origin,
	float Radius, float TraceLength, TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes, AActor* IgnoredActor)
{
	FVector End = Origin + (FVector::ForwardVector * TraceLength);
	TArray<FHitResult> HitResults;

	TArray<AActor*> ActorsToIgnore;
	if (IgnoredActor)
	{
		ActorsToIgnore.Add(IgnoredActor);
	}

	UKismetSystemLibrary::SphereTraceMultiForObjects(
		WorldContext,
		Origin,
		End,
		Radius,
		ObjectTypes,
		false,
		ActorsToIgnore,
		EDrawDebugTrace::None,
		HitResults,
		true
	);

	return HitResults;
}

TArray<FHitResult> URPGFunctionLibrary::DoBoxTrace(UObject* WorldContext, FVector Start, FVector HalfSize, 
	FRotator Rotation, float TraceLength, TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes)
{
	FVector End = Start + Rotation.Vector() * TraceLength;
	TArray<FHitResult> HitResults;

	UKismetSystemLibrary::BoxTraceMultiForObjects(
		WorldContext,
		Start,
		End,
		HalfSize,
		Rotation,
		ObjectTypes,
		false,
		TArray<AActor*>(),
		EDrawDebugTrace::Persistent,
		HitResults,
		true
	);

	return HitResults;
}

bool URPGFunctionLibrary::ApplyGameplayEffectSpecHandleToTargetActor(AActor* InInstigator, AActor* InTargetActor, const FGameplayEffectSpecHandle& InSpecHandle)
{
	if (!InSpecHandle.Data.IsValid()) return false;

	URPGAbilitySystemComponent* SourceASC = NativeGetWarriorASCFromActor(InInstigator);
	URPGAbilitySystemComponent* TargetASC = NativeGetWarriorASCFromActor(InTargetActor);

	if (!SourceASC || !TargetASC) return false;

	FActiveGameplayEffectHandle ActiveGameplayEffectHandle = 
		SourceASC->ApplyGameplayEffectSpecToTarget(*InSpecHandle.Data, TargetASC);

	return ActiveGameplayEffectHandle.WasSuccessfullyApplied();
}

void URPGFunctionLibrary::CountDown(const UObject* WorldContextObject, float TotalTime, float UpdateInterval,
	float& OutRemainingTime, ERPGCountDownActionInput CountDownInput,
	UPARAM(DisplayName = "Output") ERPGCountDownActionOutput& CountDownOutput, FLatentActionInfo LatentInfo)
{
	UWorld* World = nullptr;
	if (GEngine)
	{
		World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	}

	if (!World) return;

	FLatentActionManager& LatentActionManager = World->GetLatentActionManager();
	FRPGCountDownAction* FoundAction = LatentActionManager.FindExistingAction<FRPGCountDownAction>(LatentInfo.CallbackTarget, LatentInfo.UUID);

	if (CountDownInput == ERPGCountDownActionInput::Start)
	{
		if (!FoundAction)
		{
			LatentActionManager.AddNewAction(
				LatentInfo.CallbackTarget,
				LatentInfo.UUID,
				new FRPGCountDownAction(TotalTime, UpdateInterval, OutRemainingTime, CountDownOutput, LatentInfo)
			);
		}
	}
	else if (CountDownInput == ERPGCountDownActionInput::Cancel)
	{
		if (FoundAction)
		{
			FoundAction->CancelAction();
		}
	}
}

URPGGameInstance* URPGFunctionLibrary::GetRPGGameInstance(const UObject* WorldContextObject)
{
	if (GEngine)
	{
		if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
		{
			return World->GetGameInstance<URPGGameInstance>();
		}
	}
	return nullptr;
}

void URPGFunctionLibrary::ToggleInputMode(const UObject* WorldContextObject, ERPGInputMode InInputMode)
{
	APlayerController* PlayerController = nullptr;
	if (GEngine)
	{
		if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
		{
			PlayerController = World->GetFirstPlayerController();
		}
	}

	if (!PlayerController) return;

	if (InInputMode == ERPGInputMode::GameOnly)
	{
		FInputModeGameOnly GameOnlyMode;
		PlayerController->SetInputMode(GameOnlyMode);
		PlayerController->bShowMouseCursor = false;
	}
	else if (InInputMode == ERPGInputMode::UIOnly)
	{
		FInputModeUIOnly UIOnlyMode;
		PlayerController->SetInputMode(UIOnlyMode);
		PlayerController->bShowMouseCursor = true;
	}
}

bool URPGFunctionLibrary::FanShapeCollisionCheck(const UObject* WorldContextObject, 
	const FVector& Center, const FVector& ForwardDirection, 
	float Radius, float AngleDegrees, 
	const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, 
	AActor* IgnoredActor, TArray<AActor*>& OutActors, 
	bool bDrawDebug, FColor DebugColor, float DebugDuration)
{
	OutActors.Empty();
	if (!WorldContextObject || !WorldContextObject->GetWorld()) return false;

	TArray<AActor*> OverlappedActors;
	TArray<AActor*> ActorsToIgnore;
	if (IgnoredActor) ActorsToIgnore.Add(IgnoredActor);

	UKismetSystemLibrary::SphereOverlapActors(
		WorldContextObject, Center, Radius, ObjectTypes, nullptr, ActorsToIgnore, OverlappedActors
	);

	const float HalfAngleCos = FMath::Cos(FMath::DegreesToRadians(AngleDegrees * 0.5f));
	const FVector ForwardNormal = ForwardDirection.GetSafeNormal();

	for (AActor* TargetActor : OverlappedActors)
	{
		if (!IsValid(TargetActor)) continue;
		FVector DirectionToTarget = (TargetActor->GetActorLocation() - Center).GetSafeNormal();
		if (FVector::DotProduct(ForwardNormal, DirectionToTarget) >= HalfAngleCos)
		{
			OutActors.Add(TargetActor);
		}
	}

	if (bDrawDebug)
	{
		UWorld* World = WorldContextObject->GetWorld();
		FVector LeftLine = ForwardNormal.RotateAngleAxis(-AngleDegrees * 0.5f, FVector::UpVector);
		FVector RightLine = ForwardNormal.RotateAngleAxis(AngleDegrees * 0.5f, FVector::UpVector);

		DrawDebugLine(World, Center, Center + LeftLine * Radius, DebugColor, false, DebugDuration, 0, 1.5f);
		DrawDebugLine(World, Center, Center + RightLine * Radius, DebugColor, false, DebugDuration, 0, 1.5f);

		const int32 Segments = 12;
		FVector PreviousStep = Center + LeftLine * Radius;
		for (int32 i = 1; i <= Segments; ++i)
		{
			float CurrentAngle = -AngleDegrees * 0.5f + (AngleDegrees / Segments) * i;
			FVector CurrentStep = Center + ForwardNormal.RotateAngleAxis(CurrentAngle, FVector::UpVector) * Radius;
			DrawDebugLine(World, PreviousStep, CurrentStep, DebugColor, false, DebugDuration, 0, 1.5f);
			PreviousStep = CurrentStep;
		}
	}

	return OutActors.Num() > 0;
}

UDecalComponent* URPGFunctionLibrary::SpawnFanShapeDecal(const UObject* WorldContextObject, UMaterialInterface* DecalMaterial, const FVector& Location, const FRotator& Rotation, float Radius, float AngleDegrees, const FLinearColor& Color, float Duration)
{
	if (!WorldContextObject || !DecalMaterial) return nullptr;

	FVector DecalSize(Radius, Radius, Radius);
	UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(WorldContextObject, DecalMaterial, DecalSize, Location, Rotation, Duration);

	if (Decal)
	{
		UMaterialInstanceDynamic* DynamicMat = Decal->CreateDynamicMaterialInstance();
		if (DynamicMat)
		{
			DynamicMat->SetScalarParameterValue(TEXT("Radius"), Radius);
			DynamicMat->SetScalarParameterValue(TEXT("Angle"), AngleDegrees);
			DynamicMat->SetVectorParameterValue(TEXT("Color"), Color);
		}
		Decal->SetFadeScreenSize(0.001f);
	}
	return Decal;
}

FString URPGFunctionLibrary::FormatTimeToMMSS(float InSeconds)
{
	int32 TotalSeconds = FMath::FloorToInt(InSeconds);
	return FString::Printf(TEXT("%02d:%02d"), TotalSeconds / 60, TotalSeconds % 60);
}

FVector2D URPGFunctionLibrary::GetClampedWidgetPosition(const FVector2D& Boundary, const FVector2D& WidgetSize, const FVector2D& MousePos)
{
	FVector2D ClampedPosition = MousePos;
	ClampedPosition.X = FMath::Clamp(MousePos.X, 0.f, Boundary.X - WidgetSize.X);
	ClampedPosition.Y = FMath::Clamp(MousePos.Y, 0.f, Boundary.Y - WidgetSize.Y);
	return ClampedPosition;
}

int32 URPGFunctionLibrary::GetIndexFromWidgetPosition(const FIntPoint& Position, const int32 Columns)
{
	return Position.X + Position.Y * Columns;
}

FIntPoint URPGFunctionLibrary::GetPositionFromWidgetIndex(const int32 Index, const int32 Columns)
{
	return FIntPoint(Index % Columns, Index / Columns);
}

URPGHoverItem* URPGFunctionLibrary::GetHoverItem(APlayerController* PC)
{
	if (!IsValid(PC)) return nullptr;
	URPGInventoryComponent* IC = GetComponentFromPlayerController<URPGInventoryComponent>(PC);
	if (!IC) return nullptr;

	URPGInventoryBase* InventoryBase = IC->GetInventoryMenu();
	return IsValid(InventoryBase) ? InventoryBase->GetHoverItem() : nullptr;
}

EItemCategory URPGFunctionLibrary::GetItemCategoryFromItemPickup(ARPGPickUpBase* ItemPickup)
{
	return IsValid(ItemPickup) ? ItemPickup->GetItemManifest().GetItemCategory() : EItemCategory::None;
}

FVector2D URPGFunctionLibrary::GetWidgetPosition(UWidget* Widget)
{
	if (!Widget) return FVector2D::ZeroVector;
	const FGeometry Geometry = Widget->GetCachedGeometry();
	FVector2D PixelPosition, ViewportPosition;
	USlateBlueprintLibrary::LocalToViewport(Widget, Geometry, USlateBlueprintLibrary::GetLocalTopLeft(Geometry), PixelPosition, ViewportPosition);
	return ViewportPosition;
}

FVector2D URPGFunctionLibrary::GetWidgetSize(UWidget* Widget)
{
	return Widget ? Widget->GetCachedGeometry().GetLocalSize() : FVector2D::ZeroVector;
}

bool URPGFunctionLibrary::IsWithinBounds(const FVector2D& BoundaryPos, const FVector2D& WidgetSize, const FVector2D& MousePos)
{
	return MousePos.X >= BoundaryPos.X && MousePos.X <= (BoundaryPos.X + WidgetSize.X) &&
		MousePos.Y >= BoundaryPos.Y && MousePos.Y <= (BoundaryPos.Y + WidgetSize.Y);
}

void URPGFunctionLibrary::ItemHovered(APlayerController* PC, URPGItemBase* Item)
{
	if (!IsValid(PC)) return;
	URPGInventoryComponent* InvenComp = GetComponentFromPlayerController<URPGInventoryComponent>(PC);
	if (!IsValid(InvenComp)) return;

	URPGInventoryBase* InventoryBase = InvenComp->GetInventoryMenu();
	if (!IsValid(InventoryBase) || InventoryBase->HasHoverItem()) return;

	InventoryBase->OnItemHovered(Item);
}

void URPGFunctionLibrary::ItemUnhovered(APlayerController* PC)
{
	if (!IsValid(PC)) return;
	URPGInventoryComponent* InvenComp = GetComponentFromPlayerController<URPGInventoryComponent>(PC);
	if (!IsValid(InvenComp)) return;

	URPGInventoryBase* InventoryBase = InvenComp->GetInventoryMenu();
	if (IsValid(InventoryBase)) InventoryBase->OnItemUnHovered();
}
