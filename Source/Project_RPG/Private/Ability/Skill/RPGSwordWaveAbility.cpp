#include "Ability/Skill/RPGSwordWaveAbility.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Item/RPGSwordWaveProjectile.h"
#include "RPGGameplayTags.h"
#include "Skill/RPGClassSkillPolicy.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSwordWaveAbility)

URPGSwordWaveAbility::URPGSwordWaveAbility()
{
	ProjectileClass = ARPGSwordWaveProjectile::StaticClass();
}

bool URPGSwordWaveAbility::PlaySkillMontage(FName StartSection)
{
	const uint64 Serial = SkillDamageActivationSerial;
	if (!Super::PlaySkillMontage(StartSection)) return false;
	if (!IsActive() || Serial != SkillDamageActivationSerial) return false;
	const auto* Info = GetCurrentActorInfo();
	if (Info && Info->IsNetAuthority())
	{
		UAnimInstance* Anim = Info->GetAnimInstance();
		const FAnimMontageInstance* Instance = Anim ? Anim->GetActiveInstanceForMontage(ActiveSkillSpec.Montage) : nullptr;
		if (!Instance) return false;
		ReleaseMontageInstanceId = Instance->GetInstanceID();
		ReleaseAnimInstance = Anim;
		Anim->OnPlayMontageNotifyBegin.AddUniqueDynamic(this, &ThisClass::HandleSwordWaveRelease);
	}
	return true;
}

void URPGSwordWaveAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsEndAbilityValid(Handle, ActorInfo)) return;
	if (UAnimInstance* Anim = ReleaseAnimInstance.Get())
		Anim->OnPlayMontageNotifyBegin.RemoveDynamic(this, &ThisClass::HandleSwordWaveRelease);
	ReleaseAnimInstance.Reset();
	ReleaseMontageInstanceId = INDEX_NONE;
	const TWeakObjectPtr<ARPGSwordWaveProjectile> Projectile = ActiveProjectile;
	ActiveProjectile.Reset();
	bLaunchAttempted = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	if (ARPGSwordWaveProjectile* Actor = Projectile.Get()) Actor->Destroy();
}

void URPGSwordWaveAbility::HandleSwordWaveRelease(FName NotifyName, const FBranchingPointNotifyPayload& Payload)
{
	static const FName ReleaseName(TEXT("SwordWaveRelease"));
	auto* Pawn = Cast<APawn>(GetSkillSourceActor());
	const auto* Info = GetCurrentActorInfo();
	const auto* Anim = ReleaseAnimInstance.Get();
	const auto* ASC = Info ? Info->AbilitySystemComponent.Get() : nullptr;
	if (NotifyName != ReleaseName || bLaunchAttempted || !bSkillCommitted || !IsActive() || bIsAbilityEnding
		|| !IsValid(Pawn) || !Pawn->HasAuthority() || !ASC || ASC->GetAnimatingAbility() != this
		|| !Anim || !IsValid(Payload.SkelMeshComponent) || Payload.SkelMeshComponent != Info->SkeletalMeshComponent.Get()
		|| Payload.SkelMeshComponent->GetAnimInstance() != Anim || Payload.SequenceAsset != ActiveSkillSpec.Montage
		|| Payload.MontageInstanceID != ReleaseMontageInstanceId || !ActiveTargetResult.bIsValid || !ProjectileClass
		|| !CastEquipmentEffectLifetime.IsCurrent(Pawn, Pawn))
	{
		return;
	}

	const FVector Direction = ActiveTargetResult.AimDirection.GetSafeNormal2D();
	if (Direction.ContainsNaN() || Direction.IsNearlyZero()) return;
	bLaunchAttempted = true;
	const FTransform SpawnTransform(Direction.Rotation(), Pawn->GetActorLocation());
	ARPGSwordWaveProjectile* Projectile = Pawn->GetWorld()->SpawnActorDeferred<ARPGSwordWaveProjectile>(
		ProjectileClass, SpawnTransform, Pawn, Pawn, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(Projectile)) return;
	ActiveProjectile = Projectile;
	Projectile->OwningCast = this;
	Projectile->FinishSpawning(SpawnTransform);
}

bool URPGSwordWaveAbility::ApplySwordWaveHit(const ARPGSwordWaveProjectile& Projectile, APawn* Target, const FHitResult& Hit)
{
	auto* Pawn = Cast<APawn>(GetSkillSourceActor());
	if (!IsActive() || bIsAbilityEnding || !bSkillCommitted || ActiveProjectile.Get() != &Projectile || !IsValid(Pawn)
		|| !Pawn->HasAuthority() || Projectile.GetInstigator() != Pawn || !CastEquipmentEffectLifetime.IsCurrent(Pawn, Pawn)
		|| !IsValid(Target) || Hit.GetActor() != Target || !DamageEffectClass)
	{
		return false;
	}
	const auto* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
	if (TargetASC && TargetASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Invincible)) return false;

	const auto Profile = ActiveSkillSpec.SecurityProfile;
	const float Damage = ActiveSkillSpec.ScaleHitDamage(ActiveSkillSpec.BaseDamage, CommittedClassDamageMultiplier);
	if (!Profile.IsValid() || !FMath::IsFinite(Damage) || Damage <= 0.f || Damage > Profile.MaximumDamagePerHit) return false;

	const uint64 Serial = SkillDamageActivationSerial;
	FText Error;
	if (!ApplySkillDamageToServerHit(Pawn, Hit, DamageEffectClass, Damage, FGameplayTag(), Profile, Error)) return false;
	if (IsActive() && Serial == SkillDamageActivationSerial && !bClassHitNotified)
	{
		bClassHitNotified = true;
		if (const auto* Policy = Cast<IRPGClassSkillPolicy>(ActiveClassSkillPolicy))
			Policy->OnClassSkillHitConfirmed(*this);
	}
	return true;
}
