#include "AnimNotify/RPGAnimNotify_SkillFinisher.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGAnimNotify_SkillFinisher)

URPGAnimNotify_SkillFinisher::URPGAnimNotify_SkillFinisher()
{
	bIsNativeBranchingPoint = true;
#if WITH_EDITORONLY_DATA
	bShouldFireInEditor = false;
#endif
}

void URPGAnimNotify_SkillFinisher::BranchingPointNotify(FBranchingPointNotifyPayload& Payload)
{
	USkeletalMeshComponent* MeshComp = Payload.SkelMeshComponent;
	AActor* Source = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (!IsValid(Source) || !Source->HasAuthority()) return;
	UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Source);
	auto* Ability = ASC ? Cast<URPGGameplayAbility_SkillContainer>(ASC->GetAnimatingAbility()) : nullptr;
	if (Ability) Ability->ApplyAuthoredSkillFinisher(MeshComp, Payload.SequenceAsset, Payload.MontageInstanceID);
}
