// Fill out your copyright notice in the Description page of Project Settings.

#include "FunctionLibrary/RPGCombatFunctionLibrary.h"
#include "Interface/PawnCombatInterface.h"
#include "GenericTeamAgentInterface.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "RPGGameplayTags.h"
#include "DrawDebugHelpers.h"
#include "Components/DecalComponent.h"

UPawnCombatComponent* URPGCombatFunctionLibrary::NativeGetPawnCombatComponentFromActor(AActor* InActor)
{
	if (!IsValid(InActor)) return nullptr;

	if (IPawnCombatInterface* PawnCombatInterface = Cast<IPawnCombatInterface>(InActor))
	{
		return PawnCombatInterface->GetPawnCombatComponent();
	}

	return nullptr;
}

UPawnCombatComponent* URPGCombatFunctionLibrary::BP_GetPawnCombatComponentFromActor(AActor* InActor, ERPGValidType& OutValidType)
{
	UPawnCombatComponent* CombatComponent = NativeGetPawnCombatComponentFromActor(InActor);
	OutValidType = CombatComponent ? ERPGValidType::Valid : ERPGValidType::Invalid;

	return CombatComponent;
}

bool URPGCombatFunctionLibrary::IsTargetPawnHostile(APawn* QueryPawn, APawn* TargetPawn)
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

FGameplayTag URPGCombatFunctionLibrary::ComputeHitReactDirectionTag(AActor* InAttacker, AActor* InVictim, float& OutAngleDifference)
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

bool URPGCombatFunctionLibrary::IsValidBlock(AActor* InAttacker, AActor* InDefender)
{
	if (!IsValid(InAttacker) || !IsValid(InDefender)) return false;

	const float DotResult = FVector::DotProduct(InAttacker->GetActorForwardVector(), InDefender->GetActorForwardVector());
	return DotResult < -0.1f;
}

TArray<FHitResult> URPGCombatFunctionLibrary::DoSphereTrace(UObject* WorldContext, FVector Origin, float Radius, float TraceLength, TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes, AActor* IgnoredActor)
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

TArray<FHitResult> URPGCombatFunctionLibrary::DoBoxTrace(UObject* WorldContext, FVector Start, FVector HalfSize, FRotator Rotation, float TraceLength, TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes)
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

bool URPGCombatFunctionLibrary::FanShapeCollisionCheck(const UObject* WorldContextObject, const FVector& Center, const FVector& ForwardDirection, float Radius, float AngleDegrees, const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, AActor* IgnoredActor, TArray<AActor*>& OutActors, bool bDrawDebug, FColor DebugColor, float DebugDuration)
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

UDecalComponent* URPGCombatFunctionLibrary::SpawnFanShapeDecal(const UObject* WorldContextObject, UMaterialInterface* DecalMaterial, const FVector& Location, const FRotator& Rotation, float Radius, float AngleDegrees, const FLinearColor& Color, float Duration)
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
