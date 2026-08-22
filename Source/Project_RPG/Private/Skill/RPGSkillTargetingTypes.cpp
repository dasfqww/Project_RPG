#include "Skill/RPGSkillTargetingTypes.h"

#include "Engine/EngineTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSkillTargetingTypes)

FRPGSkillGroundPointTargetingConfig::FRPGSkillGroundPointTargetingConfig()
{
	GroundObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldStatic));
	GroundObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldDynamic));
}

void FRPGSkillTargetResult::Reset()
{
	bIsValid = false;
	TargetActor = nullptr;
	SourceLocation = FVector::ZeroVector;
	TargetLocation = FVector::ZeroVector;
	AimDirection = FVector::ForwardVector;
	HitQueryTransform = FTransform::Identity;
	AimHit = FHitResult();
	bOrientSourceToAim = false;
}

FGameplayAbilityTargetDataHandle FRPGSkillTargetDataCodec::Encode(
	const FRPGSkillTargetResult& TargetResult)
{
	FGameplayAbilityTargetDataHandle Handle;
	if (!TargetResult.bIsValid)
	{
		return Handle;
	}

	FVector AimDirection = TargetResult.AimDirection.GetSafeNormal();
	if (AimDirection.IsNearlyZero())
	{
		AimDirection =
			(TargetResult.TargetLocation - TargetResult.SourceLocation)
			.GetSafeNormal();
	}
	if (AimDirection.IsNearlyZero())
	{
		return Handle;
	}

	FGameplayAbilityTargetData_LocationInfo* LocationData =
		new FGameplayAbilityTargetData_LocationInfo();
	LocationData->SourceLocation.LocationType =
		EGameplayAbilityTargetingLocationType::LiteralTransform;
	LocationData->SourceLocation.LiteralTransform = FTransform(
		AimDirection.Rotation(),
		TargetResult.SourceLocation);
	LocationData->TargetLocation.LocationType =
		EGameplayAbilityTargetingLocationType::LiteralTransform;
	LocationData->TargetLocation.LiteralTransform = FTransform(
		AimDirection.Rotation(),
		TargetResult.TargetLocation);
	Handle.Add(LocationData);

	if (IsValid(TargetResult.TargetActor))
	{
		FGameplayAbilityTargetData_ActorArray* ActorData =
			new FGameplayAbilityTargetData_ActorArray();
		ActorData->SourceLocation = LocationData->SourceLocation;
		ActorData->TargetActorArray.Add(TargetResult.TargetActor);
		Handle.Add(ActorData);
	}

	return Handle;
}

bool FRPGSkillTargetDataCodec::Decode(
	const FGameplayAbilityTargetDataHandle& TargetData,
	FRPGSkillTargetResult& OutTargetResult)
{
	OutTargetResult.Reset();
	if (TargetData.Num() < 1 || TargetData.Num() > 2)
	{
		return false;
	}

	const FGameplayAbilityTargetData* RawLocationData = TargetData.Get(0);
	if (!RawLocationData ||
		RawLocationData->GetScriptStruct() !=
			FGameplayAbilityTargetData_LocationInfo::StaticStruct())
	{
		return false;
	}

	const FGameplayAbilityTargetData_LocationInfo* LocationData =
		static_cast<const FGameplayAbilityTargetData_LocationInfo*>(
			RawLocationData);
	const FTransform SourceTransform =
		LocationData->SourceLocation.GetTargetingTransform();
	const FTransform TargetTransform =
		LocationData->TargetLocation.GetTargetingTransform();
	const FVector SourceLocation = SourceTransform.GetLocation();
	const FVector TargetLocation = TargetTransform.GetLocation();
	if (SourceLocation.ContainsNaN() || TargetLocation.ContainsNaN())
	{
		return false;
	}

	AActor* TargetActor = nullptr;
	if (TargetData.Num() == 2)
	{
		const FGameplayAbilityTargetData* RawActorData = TargetData.Get(1);
		if (!RawActorData ||
			RawActorData->GetScriptStruct() !=
				FGameplayAbilityTargetData_ActorArray::StaticStruct())
		{
			return false;
		}

		const TArray<TWeakObjectPtr<AActor>> TargetActors =
			RawActorData->GetActors();
		if (TargetActors.Num() != 1 || !TargetActors[0].IsValid())
		{
			return false;
		}
		TargetActor = TargetActors[0].Get();
	}

	FVector AimDirection = (TargetLocation - SourceLocation).GetSafeNormal();
	if (AimDirection.IsNearlyZero())
	{
		AimDirection = SourceTransform.GetUnitAxis(EAxis::X).GetSafeNormal();
	}
	if (AimDirection.IsNearlyZero() || AimDirection.ContainsNaN())
	{
		return false;
	}

	OutTargetResult.bIsValid = true;
	OutTargetResult.TargetActor = TargetActor;
	OutTargetResult.SourceLocation = SourceLocation;
	OutTargetResult.TargetLocation = TargetLocation;
	OutTargetResult.AimDirection = AimDirection;
	OutTargetResult.HitQueryTransform =
		FTransform(AimDirection.Rotation(), SourceLocation);
	return true;
}

float FRPGSkillTargetingMath::CalculateSoftTargetScore(
	const float AngleDegrees,
	const float Distance,
	const float MaxAngleDegrees,
	const float MaxRange,
	const float AngleWeight,
	const float DistanceWeight)
{
	const float SafeMaxAngle = FMath::Max(MaxAngleDegrees, KINDA_SMALL_NUMBER);
	const float SafeMaxRange = FMath::Max(MaxRange, KINDA_SMALL_NUMBER);
	const float SafeAngleWeight = FMath::Max(0.0f, AngleWeight);
	const float SafeDistanceWeight = FMath::Max(0.0f, DistanceWeight);
	const float WeightSum = SafeAngleWeight + SafeDistanceWeight;
	if (WeightSum <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float AngleScore =
		1.0f - FMath::Clamp(AngleDegrees / SafeMaxAngle, 0.0f, 1.0f);
	const float DistanceScore =
		1.0f - FMath::Clamp(Distance / SafeMaxRange, 0.0f, 1.0f);
	return (
		AngleScore * SafeAngleWeight +
		DistanceScore * SafeDistanceWeight) / WeightSum;
}
