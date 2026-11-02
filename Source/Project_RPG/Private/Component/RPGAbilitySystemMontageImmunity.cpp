#include "Component/RPGAbilitySystemComponent.h"
#include "Ability/RPGGameplayAbility.h"
#include "Combat/RPGMontageImmunityTypes.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/Equipment/RPGPlayerWeaponLoadoutComponent.h"
#include "Animation/AnimMontage.h"

float URPGAbilitySystemComponent::PlayMontage(UGameplayAbility* AnimatingAbility, FGameplayAbilityActivationInfo ActivationInfo,
	UAnimMontage* Montage, float PlayRate, FName StartSection, float StartTime)
{
	const FGuid Request = FGuid::NewGuid(); CombatMontagePlayRequest = Request;
	auto* Ability = Cast<URPGGameplayAbility>(AnimatingAbility);
	const FGuid Execution = Ability ? Ability->GetCombatExecutionId() : FGuid();
	auto* Avatar = GetAvatarActor();
	if (auto* Loadout = Avatar ? Avatar->FindComponentByClass<URPGPlayerWeaponLoadoutComponent>() : nullptr)
		Loadout->NotifyAttackStarted(AnimatingAbility);
	auto* Control = Avatar && Avatar->HasAuthority() ? Avatar->FindComponentByClass<URPGCombatControlComponent>() : nullptr;
	TArray<FRPGMontageImmunityWindow> Windows;
	if (Control)
	{
		if (Montage && !RPGMontageImmunity::ReadWindows(*Montage, Windows))
			UE_LOG(LogTemp, Warning, TEXT("Invalid montage immunity windows on %s; window protection disabled."), *GetNameSafe(Montage));
		Control->StopMontageImmunity();
		if (CombatMontagePlayRequest != Request) return -1.f;
	}
	const float Duration = Super::PlayMontage(AnimatingAbility, ActivationInfo, Montage, PlayRate, StartSection, StartTime);
	if (Duration > 0.f && Control && Ability && Montage && CombatMontagePlayRequest == Request
		&& Ability->GetCombatExecutionId() == Execution && Ability->CanUseMontageImmunity())
		Control->TrackMontageImmunity(*this, *Ability, Execution, *Montage, MoveTemp(Windows));
	return Duration;
}
