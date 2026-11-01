#include "Combat/RPGApprovedHitContext.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBaseCharacter.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "RPGGameplayTags.h"
#include "Security/RPGDungeonGameplayAuthority.h"

namespace
{
	uint32 Epoch(const AActor* Actor)
	{
		const auto* Control = IsValid(Actor) ? Actor->FindComponentByClass<URPGCombatControlComponent>() : nullptr;
		return Control ? Control->GetLifetimeEpoch() : 0;
	}
	uint32 Revision(const AActor* Actor)
	{
		const auto* Character = Cast<ARPGBaseCharacter>(Actor);
		return Character ? Character->GetCombatPresentationRevision() : 0;
	}
	bool Living(AActor* Actor, const UAbilitySystemComponent* ASC)
	{
		const auto* Attributes = ASC ? ASC->GetSet<URPGAttributeSet>() : nullptr;
		return IsValid(Actor) && !Actor->IsActorBeingDestroyed() && ASC && ASC->GetAvatarActor() == Actor
			&& UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Actor) == ASC && Attributes
			&& FMath::IsFinite(Attributes->GetCurrentHealth()) && Attributes->GetCurrentHealth() > 0.f
			&& !ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death) && !IsDungeonGameplayRevoked(Actor);
	}
}

FRPGApprovedHitContext::FRPGApprovedHitContext(AActor* Source, AActor* Target)
	: SourceActor(Source), TargetActor(Target),
	SourceASC(IsValid(Source) ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Source) : nullptr),
	TargetASC(IsValid(Target) ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target) : nullptr),
	SourceRevision(Revision(Source)), TargetRevision(Revision(Target)), SourceControlEpoch(Epoch(Source)), TargetControlEpoch(Epoch(Target))
{
	if (IsValid(Source) && IsValid(Target))
	{
		AwayDirection = (Target->GetActorLocation() - Source->GetActorLocation()).GetSafeNormal2D();
		if (AwayDirection.IsNearlyZero()) AwayDirection = -Target->GetActorForwardVector().GetSafeNormal2D();
	}
}

bool FRPGApprovedHitContext::IsCurrent(AActor* Source, AActor* Target) const
{
	return SourceActor.Get() == Source && TargetActor.Get() == Target && Living(Source, SourceASC.Get())
		&& Living(Target, TargetASC.Get()) && Source->HasAuthority() && Target->HasAuthority()
		&& Source->GetWorld() == Target->GetWorld() && SourceRevision == Revision(Source) && TargetRevision == Revision(Target)
		&& SourceControlEpoch == Epoch(Source) && TargetControlEpoch == Epoch(Target);
}
