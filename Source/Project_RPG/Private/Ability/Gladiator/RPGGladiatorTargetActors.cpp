#include "Ability/Gladiator/RPGGladiatorTargetActors.h"

#include "Components/MeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGGladiatorTargetActors)

ARPGGameplayAbilityTargetActor_LineTraceHighlight::ARPGGameplayAbilityTargetActor_LineTraceHighlight(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void ARPGGameplayAbilityTargetActor_LineTraceHighlight::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	HighlightActor(false, CachedTracedActor.Get());
	Super::EndPlay(EndPlayReason);
}

FHitResult ARPGGameplayAbilityTargetActor_LineTraceHighlight::PerformTrace(AActor* InSourceActor)
{
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RPGGladiatorLineTraceHighlight), false);
	QueryParams.bReturnPhysicalMaterial = true;
	QueryParams.AddIgnoredActor(InSourceActor);

	const FVector TraceStart = StartLocation.GetTargetingTransform().GetLocation();
	FVector TraceEnd;
	AimWithPlayerController(InSourceActor, QueryParams, TraceStart, TraceEnd);

	FHitResult HitResult;
	LineTraceWithFilter(
		HitResult,
		InSourceActor->GetWorld(),
		Filter,
		TraceStart,
		TraceEnd,
		TraceProfile.Name,
		QueryParams);

	HighlightActor(false, CachedTracedActor.Get());
	CachedTracedActor.Reset();

	if (HitResult.bBlockingHit)
	{
		CachedTracedActor = HitResult.GetActor();
		HighlightActor(true, CachedTracedActor.Get());
	}
	else
	{
		HitResult.Location = TraceEnd;
	}

#if ENABLE_DRAW_DEBUG
	if (bDebug)
	{
		DrawDebugLine(GetWorld(), TraceStart, TraceEnd, FColor::Red);
	}
#endif

	return HitResult;
}

void ARPGGameplayAbilityTargetActor_LineTraceHighlight::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (AGameplayAbilityWorldReticle* LocalReticleActor = ReticleActor.Get())
	{
		const ACharacter* SourceCharacter = Cast<ACharacter>(SourceActor);
		LocalReticleActor->SetActorLocation(
			SourceCharacter && SourceCharacter->GetMesh()
				? SourceCharacter->GetMesh()->GetComponentLocation()
				: (SourceActor ? SourceActor->GetActorLocation() : FVector::ZeroVector));
		LocalReticleActor->SetActorRotation(FRotator::ZeroRotator);
	}
}

void ARPGGameplayAbilityTargetActor_LineTraceHighlight::HighlightActor(
	const bool bShouldHighlight,
	AActor* ActorToHighlight)
{
	if (!IsValid(ActorToHighlight))
	{
		return;
	}

	TArray<UMeshComponent*> MeshComponents;
	ActorToHighlight->GetComponents(MeshComponents);
	for (UMeshComponent* MeshComponent : MeshComponents)
	{
		if (MeshComponent)
		{
			MeshComponent->SetRenderCustomDepth(bShouldHighlight);
		}
	}
}
