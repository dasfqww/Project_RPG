#include "FunctionLibrary/RPGSecurityBlueprintLibrary.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "FunctionLibrary/RPGCombatFunctionLibrary.h"
#include "GameplayEffect.h"
#include "RPGGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSecurityBlueprintLibrary)

namespace RPGSecurityBlueprint
{
	UAbilitySystemComponent* FindAbilitySystem(AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return nullptr;
		}
		if (UAbilitySystemComponent* ASC =
			UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor))
		{
			return ASC;
		}
		return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(
			Actor->GetOwner());
	}

	bool ValidateStandaloneHit(
		AActor& SourceActor,
		AActor& TargetActor,
		const FHitResult& Hit,
		const FRPGSkillSecurityProfile& Profile,
		FText& OutError)
	{
		if (Hit.GetActor() != &TargetActor ||
			SourceActor.GetWorld() != TargetActor.GetWorld() ||
			SourceActor.GetActorLocation().ContainsNaN() ||
			TargetActor.GetActorLocation().ContainsNaN() ||
			Hit.ImpactPoint.ContainsNaN())
		{
			OutError = FText::FromString(
				TEXT("Server hit contains invalid source, target, or spatial data."));
			return false;
		}

		FVector BoundsOrigin = TargetActor.GetActorLocation();
		FVector BoundsExtent = FVector::ZeroVector;
		TargetActor.GetActorBounds(true, BoundsOrigin, BoundsExtent, false);
		const float AllowedDistance =
			Profile.MaximumServerHitDistance + BoundsExtent.Size();
		if (FVector::DistSquared(SourceActor.GetActorLocation(), BoundsOrigin) >
			FMath::Square(AllowedDistance))
		{
			OutError = FText::FromString(
				TEXT("Server hit exceeds the skill security profile range."));
			return false;
		}

		const FVector BoundsMin = BoundsOrigin - BoundsExtent;
		const FVector BoundsMax = BoundsOrigin + BoundsExtent;
		const FVector ClosestPoint(
			FMath::Clamp(Hit.ImpactPoint.X, BoundsMin.X, BoundsMax.X),
			FMath::Clamp(Hit.ImpactPoint.Y, BoundsMin.Y, BoundsMax.Y),
			FMath::Clamp(Hit.ImpactPoint.Z, BoundsMin.Z, BoundsMax.Z));
		if (FVector::DistSquared(ClosestPoint, Hit.ImpactPoint) >
			FMath::Square(FMath::Max(0.0f, Profile.HitLocationTolerance)))
		{
			OutError = FText::FromString(
				TEXT("Server impact point does not intersect the target bounds."));
			return false;
		}
		return true;
	}
}

URPGSecurityValidationComponent*
URPGSecurityBlueprintLibrary::GetSecurityValidationComponent(AActor* Actor)
{
	return IsValid(Actor)
		? Actor->FindComponentByClass<URPGSecurityValidationComponent>()
		: nullptr;
}

bool URPGSecurityBlueprintLibrary::ValidateAuthorizedServerHit(
	AActor* SourceActor,
	const FHitResult& ServerHit,
	const float Damage,
	const FRPGSkillSecurityProfile& SecurityProfile,
	FText& OutError)
{
	OutError = FText::GetEmpty();
	AActor* TargetActor = ServerHit.GetActor();
	FString ProfileError;
	if (!IsValid(SourceActor) || !SourceActor->HasAuthority())
	{
		OutError = FText::FromString(
			TEXT("Only the server may validate an authorized hit."));
		return false;
	}
	if (!IsValid(TargetActor) || TargetActor == SourceActor)
	{
		OutError = FText::FromString(
			TEXT("Authorized hit requires a valid, distinct target."));
		return false;
	}
	if (!SecurityProfile.IsValid(&ProfileError))
	{
		OutError = FText::FromString(ProfileError);
		return false;
	}
	if (!FMath::IsFinite(Damage) || Damage <= 0.0f ||
		Damage > SecurityProfile.MaximumDamagePerHit)
	{
		OutError = FText::FromString(
			TEXT("Damage exceeds the authored skill security profile."));
		if (URPGSecurityValidationComponent* Security =
			GetSecurityValidationComponent(SourceActor))
		{
			Security->ReportViolation(
				ERPGSecurityViolationType::InvalidDamage,
				ERPGSecurityViolationSeverity::Critical,
				20.0f,
				OutError.ToString());
		}
		return false;
	}

	if (const APawn* SourcePawn = Cast<APawn>(SourceActor))
	{
		if (const APawn* TargetPawn = Cast<APawn>(TargetActor);
			TargetPawn && !URPGCombatFunctionLibrary::IsTargetPawnHostile(
				const_cast<APawn*>(SourcePawn),
				const_cast<APawn*>(TargetPawn)))
		{
			OutError = FText::FromString(
				TEXT("Friendly targets cannot receive skill damage."));
			return false;
		}
	}

	if (URPGSecurityValidationComponent* Security =
		GetSecurityValidationComponent(SourceActor))
	{
		FString RejectionReason;
		if (!Security->ValidateCombatHit(
			TargetActor,
			ServerHit,
			SecurityProfile.MaximumServerHitDistance,
			SecurityProfile.HitLocationTolerance,
			RejectionReason) ||
			!Security->ValidateDamage(Damage, RejectionReason))
		{
			OutError = FText::FromString(RejectionReason);
			return false;
		}
	}
	else if (!RPGSecurityBlueprint::ValidateStandaloneHit(
		*SourceActor,
		*TargetActor,
		ServerHit,
		SecurityProfile,
		OutError))
	{
		return false;
	}

	return true;
}

bool URPGSecurityBlueprintLibrary::ApplyAuthorizedServerDamage(
	AActor* SourceActor,
	const FHitResult& ServerHit,
	const TSubclassOf<UGameplayEffect> DamageEffectClass,
	const float Damage,
	FGameplayTag SetByCallerDamageTag,
	const FRPGSkillSecurityProfile& SecurityProfile,
	FActiveGameplayEffectHandle& OutEffectHandle,
	FText& OutError)
{
	OutEffectHandle = FActiveGameplayEffectHandle();
	OutError = FText::GetEmpty();
	if (!DamageEffectClass)
	{
		OutError = FText::FromString(TEXT("Damage requires a valid GameplayEffect class."));
		return false;
	}
	if (!ValidateAuthorizedServerHit(
		SourceActor,
		ServerHit,
		Damage,
		SecurityProfile,
		OutError))
	{
		return false;
	}
	AActor* TargetActor = ServerHit.GetActor();

	UAbilitySystemComponent* SourceASC =
		RPGSecurityBlueprint::FindAbilitySystem(SourceActor);
	UAbilitySystemComponent* TargetASC =
		RPGSecurityBlueprint::FindAbilitySystem(TargetActor);
	if (!SourceASC || !TargetASC || SourceASC == TargetASC)
	{
		OutError = FText::FromString(TEXT("Source or target Ability System is unavailable."));
		return false;
	}

	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	Context.AddInstigator(SourceASC->GetAvatarActor(), SourceActor);
	Context.AddHitResult(ServerHit);
	FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
		DamageEffectClass,
		1.0f,
		Context);
	if (!Spec.IsValid())
	{
		OutError = FText::FromString(TEXT("Unable to build the damage GameplayEffect spec."));
		return false;
	}

	if (!SetByCallerDamageTag.IsValid())
	{
		SetByCallerDamageTag = RPGGameplayTags::Shared_SetByCaller_BaseDamage;
	}
	if (!SetByCallerDamageTag.IsValid())
	{
		OutError = FText::FromString(TEXT("A valid SetByCaller damage tag is required."));
		return false;
	}
	Spec.Data->SetSetByCallerMagnitude(SetByCallerDamageTag, Damage);
	OutEffectHandle = SourceASC->ApplyGameplayEffectSpecToTarget(
		*Spec.Data,
		TargetASC);
	if (!OutEffectHandle.WasSuccessfullyApplied())
	{
		OutError = FText::FromString(TEXT("The target rejected the damage GameplayEffect."));
		return false;
	}
	return true;
}
