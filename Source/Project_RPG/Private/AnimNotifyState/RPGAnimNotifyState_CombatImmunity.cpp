#include "AnimNotifyState/RPGAnimNotifyState_CombatImmunity.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "GameFramework/Character.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	void Refresh(USkeletalMeshComponent* Mesh)
	{
		auto* Owner = Mesh ? Cast<ACharacter>(Mesh->GetOwner()) : nullptr;
		if (Owner && Owner->HasAuthority() && Owner->GetMesh() == Mesh)
			if (auto* Control = Owner->FindComponentByClass<URPGCombatControlComponent>()) Control->RefreshMontageImmunity();
	}
}
URPGAnimNotifyState_CombatImmunity::URPGAnimNotifyState_CombatImmunity()
{
	Immunities.Flags = 3; // Flinch and displacement; status ailments remain independent.
	bIsNativeBranchingPoint = true;
#if WITH_EDITORONLY_DATA
	bShouldFireInEditor = false;
	NotifyColor = FColor(240, 190, 60);
#endif
}
void URPGAnimNotifyState_CombatImmunity::NotifyBegin(USkeletalMeshComponent* Mesh, UAnimSequenceBase*, float, const FAnimNotifyEventReference&) { Refresh(Mesh); }
void URPGAnimNotifyState_CombatImmunity::NotifyTick(USkeletalMeshComponent* Mesh, UAnimSequenceBase*, float, const FAnimNotifyEventReference&) { Refresh(Mesh); }
void URPGAnimNotifyState_CombatImmunity::NotifyEnd(USkeletalMeshComponent* Mesh, UAnimSequenceBase*, const FAnimNotifyEventReference&) { Refresh(Mesh); }
#if WITH_EDITOR
bool URPGAnimNotifyState_CombatImmunity::CanBePlaced(UAnimSequenceBase* Animation) const { return Animation && Animation->IsA<UAnimMontage>(); }
EDataValidationResult URPGAnimNotifyState_CombatImmunity::IsDataValid(FDataValidationContext& Context) const
{
	if (!Immunities.IsValid())
	{
		Context.AddError(NSLOCTEXT("RPGCombatImmunity", "InvalidWindowFlags", "Montage immunity contains unknown flags."));
		return EDataValidationResult::Invalid;
	}
	return EDataValidationResult::Valid;
}
#endif
