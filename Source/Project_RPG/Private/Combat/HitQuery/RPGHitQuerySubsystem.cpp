#include "Combat/HitQuery/RPGHitQuerySubsystem.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Manager/TeamManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGHitQuerySubsystem)

namespace RPGHitQuery
{
	struct FCandidate
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<UPrimitiveComponent> Component;
		TOptional<FHitResult> SweepHit;
	};

	FCollisionObjectQueryParams MakeObjectQueryParams(
		const FRPGHitQueryFilter& Filter)
	{
		FCollisionObjectQueryParams Result;
		for (const TEnumAsByte<EObjectTypeQuery> ObjectType : Filter.ObjectTypes)
		{
			const ECollisionChannel Channel =
				UEngineTypes::ConvertToCollisionChannel(ObjectType.GetValue());
			if (Channel != ECC_OverlapAll_Deprecated)
			{
				Result.AddObjectTypesToQuery(Channel);
			}
		}

		if (!Result.IsValid())
		{
			Result.AddObjectTypesToQuery(ECC_Pawn);
		}
		return Result;
	}

	FCollisionShape MakeBroadPhaseShape(const FRPGHitQueryShape& Shape)
	{
		switch (Shape.Type)
		{
		case ERPGHitQueryShape::Sphere:
			return FCollisionShape::MakeSphere(Shape.Radius);

		case ERPGHitQueryShape::Capsule:
			return FCollisionShape::MakeCapsule(Shape.Radius, Shape.HalfHeight);

		case ERPGHitQueryShape::Box:
			return FCollisionShape::MakeBox(Shape.BoxHalfExtent);

		case ERPGHitQueryShape::Sector:
		case ERPGHitQueryShape::RingSector:
			return FCollisionShape::MakeBox(
				FVector(Shape.Radius, Shape.Radius, FMath::Max(Shape.HalfHeight, 1.0f)));

		case ERPGHitQueryShape::LineSweep:
			return FCollisionShape::MakeSphere(Shape.Radius);
		}

		return FCollisionShape::MakeSphere(1.0f);
	}

	bool PassesTeamRule(
		const UWorld* World,
		const AActor* SourceActor,
		const AActor* TargetActor,
		const ERPGHitQueryTeamRule TeamRule)
	{
		if (TeamRule == ERPGHitQueryTeamRule::Any)
		{
			return true;
		}
		if (!World || !SourceActor || !TargetActor)
		{
			return false;
		}

		const UTeamManager* TeamManager = World->GetSubsystem<UTeamManager>();
		if (!TeamManager)
		{
			return false;
		}

		const ERPGTeamComparison Comparison =
			TeamManager->CompareTeams(SourceActor, TargetActor);
		return TeamRule == ERPGHitQueryTeamRule::HostileOnly
			? Comparison == ERPGTeamComparison::DifferentTeams
			: Comparison == ERPGTeamComparison::OnSameTeam;
	}

	bool PassesGameplayTagFilter(
		const AActor* TargetActor,
		const FRPGHitQueryFilter& Filter)
	{
		if (Filter.RequiredTargetTags.IsEmpty() && Filter.BlockedTargetTags.IsEmpty())
		{
			return true;
		}

		const UAbilitySystemComponent* TargetASC =
			UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(
				const_cast<AActor*>(TargetActor));
		if (!TargetASC)
		{
			return Filter.RequiredTargetTags.IsEmpty();
		}

		return TargetASC->HasAllMatchingGameplayTags(Filter.RequiredTargetTags) &&
			!TargetASC->HasAnyMatchingGameplayTags(Filter.BlockedTargetTags);
	}

	FVector ResolveTargetPoint(
		const FCandidate& Candidate,
		const ERPGHitQueryTargetPointMode PointMode,
		const FVector& ShapeOrigin)
	{
		AActor* TargetActor = Candidate.Actor.Get();
		if (!TargetActor)
		{
			return FVector::ZeroVector;
		}

		if (PointMode == ERPGHitQueryTargetPointMode::ClosestCollisionPoint)
		{
			if (UPrimitiveComponent* Component = Candidate.Component.Get())
			{
				FVector ClosestPoint = FVector::ZeroVector;
				if (Component->GetClosestPointOnCollision(ShapeOrigin, ClosestPoint) >= 0.0f)
				{
					return ClosestPoint;
				}
			}
		}

		return TargetActor->GetActorLocation();
	}

	bool HasLineOfSight(
		const UWorld* World,
		const FVector& TraceStart,
		const FVector& TraceEnd,
		const ECollisionChannel TraceChannel,
		const AActor* SourceActor,
		const AActor* TargetActor,
		const TArray<TObjectPtr<AActor>>& IgnoredActors)
	{
		if (!World || !TargetActor)
		{
			return false;
		}

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RPGHitQueryLineOfSight), false);
		QueryParams.AddIgnoredActor(SourceActor);
		for (const AActor* IgnoredActor : IgnoredActors)
		{
			QueryParams.AddIgnoredActor(IgnoredActor);
		}

		FHitResult HitResult;
		const bool bHit = World->LineTraceSingleByChannel(
			HitResult,
			TraceStart,
			TraceEnd,
			TraceChannel,
			QueryParams);
		return !bHit || HitResult.GetActor() == TargetActor;
	}

	void AddCandidate(
		TMap<TWeakObjectPtr<AActor>, FCandidate>& Candidates,
		AActor* Actor,
		UPrimitiveComponent* Component,
		const FHitResult* SweepHit = nullptr)
	{
		if (!Actor)
		{
			return;
		}

		const TWeakObjectPtr<AActor> ActorKey(Actor);
		FCandidate& Candidate = Candidates.FindOrAdd(ActorKey);
		Candidate.Actor = Actor;
		if (!Candidate.Component.IsValid() && Component)
		{
			Candidate.Component = Component;
		}
		if (!Candidate.SweepHit.IsSet() && SweepHit)
		{
			Candidate.SweepHit = *SweepHit;
			Candidate.Component = SweepHit->GetComponent();
		}
	}

	void DrawSector(
		UWorld* World,
		const FTransform& ShapeTransform,
		const FRPGHitQueryShape& Shape,
		const FColor& Color,
		const float Duration)
	{
		const int32 SegmentCount =
			FMath::Clamp(FMath::CeilToInt(Shape.AngleDegrees / 7.5f), 8, 64);
		const float StartAngle = -Shape.AngleDegrees * 0.5f;
		const float AngleStep = Shape.AngleDegrees / SegmentCount;

		auto PointAt = [&ShapeTransform](const float Radius, const float AngleDegrees)
		{
			const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);
			const FVector LocalPoint(
				FMath::Cos(AngleRadians) * Radius,
				FMath::Sin(AngleRadians) * Radius,
				0.0f);
			return ShapeTransform.TransformPositionNoScale(LocalPoint);
		};

		FVector PreviousOuter = PointAt(Shape.Radius, StartAngle);
		FVector PreviousInner = PointAt(Shape.InnerRadius, StartAngle);
		for (int32 SegmentIndex = 1; SegmentIndex <= SegmentCount; ++SegmentIndex)
		{
			const float Angle = StartAngle + AngleStep * SegmentIndex;
			const FVector CurrentOuter = PointAt(Shape.Radius, Angle);
			DrawDebugLine(World, PreviousOuter, CurrentOuter, Color, false, Duration);
			PreviousOuter = CurrentOuter;

			if (Shape.Type == ERPGHitQueryShape::RingSector)
			{
				const FVector CurrentInner = PointAt(Shape.InnerRadius, Angle);
				DrawDebugLine(World, PreviousInner, CurrentInner, Color, false, Duration);
				PreviousInner = CurrentInner;
			}
		}

		const float RadialStart =
			Shape.Type == ERPGHitQueryShape::RingSector ? Shape.InnerRadius : 0.0f;
		DrawDebugLine(
			World,
			PointAt(RadialStart, StartAngle),
			PointAt(Shape.Radius, StartAngle),
			Color,
			false,
			Duration);
		DrawDebugLine(
			World,
			PointAt(RadialStart, -StartAngle),
			PointAt(Shape.Radius, -StartAngle),
			Color,
			false,
			Duration);

		if (Shape.HalfHeight > 0.0f)
		{
			const FVector Up = ShapeTransform.GetUnitAxis(EAxis::Z) * Shape.HalfHeight;
			DrawDebugLine(
				World,
				ShapeTransform.GetLocation() - Up,
				ShapeTransform.GetLocation() + Up,
				Color,
				false,
				Duration);
		}
	}
}

bool URPGHitQuerySubsystem::ExecuteHitQuery(
	const FRPGHitQueryContext& Context,
	TArray<FRPGHitQueryResult>& OutResults) const
{
	OutResults.Reset();

	UWorld* World = GetWorld();
	if (!World || !FRPGHitQueryMath::IsShapeValid(Context.Profile.Shape))
	{
		return false;
	}

	const FRPGHitQueryShape& Shape = Context.Profile.Shape;
	const FRPGHitQueryFilter& Filter = Context.Profile.Filter;
	const FTransform ShapeTransform =
		FRPGHitQueryMath::ResolveShapeTransform(Context.QueryTransform, Shape);
	const FVector ShapeOrigin = ShapeTransform.GetLocation();
	const FQuat ShapeRotation = ShapeTransform.GetRotation();

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RPGHitQuery), false);
	if (!Filter.bIncludeSourceActor)
	{
		QueryParams.AddIgnoredActor(Context.SourceActor);
	}
	for (const AActor* IgnoredActor : Context.IgnoredActors)
	{
		QueryParams.AddIgnoredActor(IgnoredActor);
	}
	for (const AActor* AlreadyHitActor : Context.AlreadyHitActors)
	{
		QueryParams.AddIgnoredActor(AlreadyHitActor);
	}

	const FCollisionObjectQueryParams ObjectQueryParams =
		RPGHitQuery::MakeObjectQueryParams(Filter);
	TMap<TWeakObjectPtr<AActor>, RPGHitQuery::FCandidate> Candidates;

	if (Shape.Type == ERPGHitQueryShape::LineSweep)
	{
		const FVector SweepEnd =
			ShapeOrigin + ShapeTransform.GetUnitAxis(EAxis::X) * Shape.Length;
		TArray<FHitResult> Hits;
		World->SweepMultiByObjectType(
			Hits,
			ShapeOrigin,
			SweepEnd,
			ShapeRotation,
			ObjectQueryParams,
			RPGHitQuery::MakeBroadPhaseShape(Shape),
			QueryParams);

		for (const FHitResult& Hit : Hits)
		{
			RPGHitQuery::AddCandidate(
				Candidates,
				Hit.GetActor(),
				Hit.GetComponent(),
				&Hit);
		}
	}
	else
	{
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByObjectType(
			Overlaps,
			ShapeOrigin,
			ShapeRotation,
			ObjectQueryParams,
			RPGHitQuery::MakeBroadPhaseShape(Shape),
			QueryParams);

		for (const FOverlapResult& Overlap : Overlaps)
		{
			RPGHitQuery::AddCandidate(
				Candidates,
				Overlap.GetActor(),
				Overlap.GetComponent());
		}
	}

	TSet<TWeakObjectPtr<AActor>> ExplicitlyIgnored;
	for (AActor* IgnoredActor : Context.IgnoredActors)
	{
		ExplicitlyIgnored.Add(IgnoredActor);
	}
	for (AActor* AlreadyHitActor : Context.AlreadyHitActors)
	{
		ExplicitlyIgnored.Add(AlreadyHitActor);
	}

	for (const TPair<TWeakObjectPtr<AActor>, RPGHitQuery::FCandidate>& Pair : Candidates)
	{
		const RPGHitQuery::FCandidate& Candidate = Pair.Value;
		AActor* TargetActor = Candidate.Actor.Get();
		if (!IsValid(TargetActor) ||
			ExplicitlyIgnored.Contains(TargetActor) ||
			(!Filter.bIncludeSourceActor && TargetActor == Context.SourceActor))
		{
			continue;
		}
		if (!RPGHitQuery::PassesTeamRule(
			World,
			Context.SourceActor,
			TargetActor,
			Filter.TeamRule))
		{
			continue;
		}
		if (!RPGHitQuery::PassesGameplayTagFilter(TargetActor, Filter))
		{
			continue;
		}

		const FVector QueryPoint = RPGHitQuery::ResolveTargetPoint(
			Candidate,
			Filter.TargetPointMode,
			ShapeOrigin);
		if (!FRPGHitQueryMath::IsPointInsideShape(
			ShapeTransform,
			Shape,
			QueryPoint))
		{
			continue;
		}
		if (Filter.bRequireLineOfSight &&
			!RPGHitQuery::HasLineOfSight(
				World,
				ShapeOrigin,
				QueryPoint,
				Filter.LineOfSightTraceChannel,
				Context.SourceActor,
				TargetActor,
				Context.IgnoredActors))
		{
			continue;
		}

		FRPGHitQueryResult& Result = OutResults.AddDefaulted_GetRef();
		Result.TargetActor = TargetActor;
		Result.TargetComponent = Candidate.Component.Get();
		Result.QueryPoint = QueryPoint;
		Result.Distance = FVector::Distance(ShapeOrigin, QueryPoint);
		if (Candidate.SweepHit.IsSet())
		{
			Result.HitResult = Candidate.SweepHit.GetValue();
		}
		else
		{
			const FVector Normal = (ShapeOrigin - QueryPoint).GetSafeNormal();
			Result.HitResult = FHitResult(
				TargetActor,
				Candidate.Component.Get(),
				QueryPoint,
				Normal);
		}
	}

	if (Filter.SortMode != ERPGHitQuerySortMode::None)
	{
		const bool bNearestFirst = Filter.SortMode == ERPGHitQuerySortMode::Nearest;
		OutResults.Sort([bNearestFirst](
			const FRPGHitQueryResult& Left,
			const FRPGHitQueryResult& Right)
		{
			if (FMath::IsNearlyEqual(Left.Distance, Right.Distance))
			{
				return GetNameSafe(Left.TargetActor).Compare(
					GetNameSafe(Right.TargetActor),
					ESearchCase::CaseSensitive) < 0;
			}
			return bNearestFirst
				? Left.Distance < Right.Distance
				: Left.Distance > Right.Distance;
		});
	}

	if (Filter.MaxResults > 0 && OutResults.Num() > Filter.MaxResults)
	{
		OutResults.SetNum(Filter.MaxResults, EAllowShrinking::No);
	}

	if (Context.Profile.bDrawDebug)
	{
		DrawDebugHitQuery(Context.QueryTransform, Context.Profile);
		for (const FRPGHitQueryResult& Result : OutResults)
		{
			DrawDebugSphere(
				World,
				Result.QueryPoint,
				12.0f,
				8,
				FColor::Green,
				false,
				Context.Profile.DebugDuration);
		}
	}

	return !OutResults.IsEmpty();
}

bool URPGHitQuerySubsystem::IsTargetEligible(
	AActor* SourceActor,
	AActor* TargetActor,
	const FVector& QueryOrigin,
	const FRPGHitQueryFilter& Filter,
	const TArray<TObjectPtr<AActor>>& IgnoredActors) const
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(TargetActor) ||
		(!Filter.bIncludeSourceActor && TargetActor == SourceActor) ||
		IgnoredActors.Contains(TargetActor))
	{
		return false;
	}
	if (!RPGHitQuery::PassesTeamRule(
		World, SourceActor, TargetActor, Filter.TeamRule) ||
		!RPGHitQuery::PassesGameplayTagFilter(TargetActor, Filter))
	{
		return false;
	}

	return !Filter.bRequireLineOfSight ||
		RPGHitQuery::HasLineOfSight(
			World,
			QueryOrigin,
			TargetActor->GetActorLocation(),
			Filter.LineOfSightTraceChannel,
			SourceActor,
			TargetActor,
			IgnoredActors);
}

bool URPGHitQuerySubsystem::IsPointInsideHitShape(
	const FTransform& QueryTransform,
	const FRPGHitQueryShape& Shape,
	const FVector& WorldPoint)
{
	return FRPGHitQueryMath::IsPointInsideShape(
		FRPGHitQueryMath::ResolveShapeTransform(QueryTransform, Shape),
		Shape,
		WorldPoint);
}

void URPGHitQuerySubsystem::DrawDebugHitQuery(
	const FTransform& QueryTransform,
	const FRPGHitQueryProfile& Profile) const
{
	UWorld* World = GetWorld();
	if (!World || !FRPGHitQueryMath::IsShapeValid(Profile.Shape))
	{
		return;
	}

	const FRPGHitQueryShape& Shape = Profile.Shape;
	const FTransform ShapeTransform =
		FRPGHitQueryMath::ResolveShapeTransform(QueryTransform, Shape);
	const FVector Origin = ShapeTransform.GetLocation();
	const FQuat Rotation = ShapeTransform.GetRotation();
	const FColor Color = Profile.DebugColor;
	const float Duration = Profile.DebugDuration;

	switch (Shape.Type)
	{
	case ERPGHitQueryShape::Sphere:
		DrawDebugSphere(World, Origin, Shape.Radius, 24, Color, false, Duration);
		break;

	case ERPGHitQueryShape::Capsule:
		DrawDebugCapsule(
			World,
			Origin,
			Shape.HalfHeight,
			Shape.Radius,
			Rotation,
			Color,
			false,
			Duration);
		break;

	case ERPGHitQueryShape::Box:
		DrawDebugBox(
			World,
			Origin,
			Shape.BoxHalfExtent,
			Rotation,
			Color,
			false,
			Duration);
		break;

	case ERPGHitQueryShape::Sector:
	case ERPGHitQueryShape::RingSector:
		RPGHitQuery::DrawSector(
			World,
			ShapeTransform,
			Shape,
			Color,
			Duration);
		break;

	case ERPGHitQueryShape::LineSweep:
	{
		const FVector End =
			Origin + ShapeTransform.GetUnitAxis(EAxis::X) * Shape.Length;
		DrawDebugLine(World, Origin, End, Color, false, Duration);
		DrawDebugSphere(World, Origin, Shape.Radius, 16, Color, false, Duration);
		DrawDebugSphere(World, End, Shape.Radius, 16, Color, false, Duration);
		break;
	}
	}
}
