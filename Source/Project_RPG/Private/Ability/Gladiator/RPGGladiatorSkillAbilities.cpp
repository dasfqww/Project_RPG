#include "Ability/Gladiator/RPGGladiatorSkillAbilities.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "NiagaraSystem.h"
#include "RPGGameplayTags.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGGladiatorSkillAbilities)

namespace RPGGladiatorAbility
{
	void ReportInvalidTarget(AActor* SourceActor, const FString& Detail)
	{
		if (URPGSecurityValidationComponent* Security = SourceActor
			? SourceActor->FindComponentByClass<
				URPGSecurityValidationComponent>()
			: nullptr)
		{
			Security->ReportInvalidTargetData(Detail);
		}
	}

	bool IsFiniteLocation(const FVector& Location)
	{
		return !Location.ContainsNaN() &&
			FMath::IsFinite(Location.X) &&
			FMath::IsFinite(Location.Y) &&
			FMath::IsFinite(Location.Z);
	}

	bool ValidateTargetedEffectData(
		AActor* SourceActor,
		const FGameplayAbilityTargetDataHandle& SubmittedData,
		const float MaximumRange,
		FGameplayAbilityTargetDataHandle& OutValidatedData)
	{
		OutValidatedData.Clear();
		if (!IsValid(SourceActor) || !SourceActor->HasAuthority() ||
			SubmittedData.Num() != 1)
		{
			ReportInvalidTarget(
				SourceActor,
				TEXT("Targeting spell submitted an invalid target count."));
			return false;
		}

		const FGameplayAbilityTargetData* TargetData = SubmittedData.Get(0);
		const FHitResult* SubmittedHit =
			TargetData ? TargetData->GetHitResult() : nullptr;
		AActor* TargetActor = SubmittedHit ? SubmittedHit->GetActor() : nullptr;
		if (!IsValid(TargetActor) ||
			TargetActor->GetWorld() != SourceActor->GetWorld())
		{
			ReportInvalidTarget(
				SourceActor,
				TEXT("Targeting spell submitted an invalid target actor."));
			return false;
		}

		FVector TargetOrigin = TargetActor->GetActorLocation();
		FVector TargetExtent = FVector::ZeroVector;
		TargetActor->GetActorBounds(true, TargetOrigin, TargetExtent, false);
		const float AllowedRange = FMath::Max(1.0f, MaximumRange) +
			TargetExtent.Size();
		if (!IsFiniteLocation(TargetOrigin) ||
			FVector::DistSquared(SourceActor->GetActorLocation(), TargetOrigin) >
				FMath::Square(AllowedRange))
		{
			ReportInvalidTarget(
				SourceActor,
				TEXT("Targeting spell exceeded its server range."));
			return false;
		}

		FCollisionQueryParams QueryParams(
			SCENE_QUERY_STAT(RPGGladiatorTargetValidation),
			false,
			SourceActor);
		FHitResult VisibilityHit;
		const FVector TraceStart =
			SourceActor->GetActorLocation() + FVector::UpVector * 50.0f;
		const bool bBlocked = SourceActor->GetWorld()->LineTraceSingleByChannel(
			VisibilityHit,
			TraceStart,
			TargetOrigin,
			ECC_Visibility,
			QueryParams);
		if (bBlocked)
		{
			AActor* BlockingActor = VisibilityHit.GetActor();
			const bool bReachedTarget = BlockingActor == TargetActor ||
				(BlockingActor && BlockingActor->GetOwner() == TargetActor) ||
				TargetActor->GetOwner() == BlockingActor;
			if (!bReachedTarget)
			{
				ReportInvalidTarget(
					SourceActor,
					TEXT("Targeting spell failed the server line-of-sight check."));
				return false;
			}
		}

		const FVector ImpactPoint = bBlocked
			? FVector(VisibilityHit.ImpactPoint)
			: TargetOrigin;
		const FVector ImpactNormal =
			(ImpactPoint - TraceStart).GetSafeNormal();
		const FHitResult ServerHit(
			TargetActor,
			bBlocked ? VisibilityHit.GetComponent() : nullptr,
			ImpactPoint,
			ImpactNormal);
		OutValidatedData.Add(
			new FGameplayAbilityTargetData_SingleTargetHit(ServerHit));
		return true;
	}

	bool ResolveGroundTargetLocation(
		AActor* SourceActor,
		const FGameplayAbilityTargetDataHandle& SubmittedData,
		const float MaximumRange,
		const float AcceptanceMultiplier,
		const float TraceHalfHeight,
		const FCollisionProfileName& TraceProfile,
		FVector& OutLocation)
	{
		if (!IsValid(SourceActor) || !SourceActor->HasAuthority())
		{
			return false;
		}
		if (!SubmittedData.IsValid(0))
		{
			OutLocation = SourceActor->GetActorLocation();
			return true;
		}
		if (SubmittedData.Num() != 1)
		{
			ReportInvalidTarget(
				SourceActor,
				TEXT("Ground skill submitted an invalid target count."));
			return false;
		}

		const FGameplayAbilityTargetData* TargetData = SubmittedData.Get(0);
		const FHitResult* SubmittedHit =
			TargetData ? TargetData->GetHitResult() : nullptr;
		if (!SubmittedHit)
		{
			ReportInvalidTarget(
				SourceActor,
				TEXT("Ground skill submitted no hit result."));
			return false;
		}

		FVector Candidate(SubmittedHit->Location);
		if (!IsFiniteLocation(Candidate))
		{
			ReportInvalidTarget(
				SourceActor,
				TEXT("Ground skill submitted a non-finite location."));
			return false;
		}
		const float AllowedRange = FMath::Max(1.0f, MaximumRange) *
			FMath::Clamp(AcceptanceMultiplier, 1.0f, 2.0f);
		if (FVector::DistSquared(SourceActor->GetActorLocation(), Candidate) >
			FMath::Square(AllowedRange))
		{
			ReportInvalidTarget(
				SourceActor,
				TEXT("Ground skill exceeded its server range."));
			return false;
		}

		const float VerticalTraceDistance =
			FMath::Max(500.0f, FMath::Abs(TraceHalfHeight) * 2.0f);
		const FVector TraceStart =
			Candidate + FVector::UpVector * VerticalTraceDistance;
		const FVector TraceEnd =
			Candidate - FVector::UpVector * VerticalTraceDistance;
		FCollisionQueryParams GroundParams(
			SCENE_QUERY_STAT(RPGGladiatorGroundValidation),
			false,
			SourceActor);
		FHitResult GroundHit;
		const bool bFoundGround = !TraceProfile.Name.IsNone()
			? SourceActor->GetWorld()->LineTraceSingleByProfile(
				GroundHit,
				TraceStart,
				TraceEnd,
				TraceProfile.Name,
				GroundParams)
			: SourceActor->GetWorld()->LineTraceSingleByChannel(
				GroundHit,
				TraceStart,
				TraceEnd,
				ECC_Visibility,
				GroundParams);
		if (!bFoundGround || !IsFiniteLocation(FVector(GroundHit.ImpactPoint)))
		{
			ReportInvalidTarget(
				SourceActor,
				TEXT("Ground skill could not resolve an authoritative surface."));
			return false;
		}

		OutLocation = FVector(GroundHit.ImpactPoint);
		if (FVector::DistSquared(SourceActor->GetActorLocation(), OutLocation) >
			FMath::Square(AllowedRange))
		{
			ReportInvalidTarget(
				SourceActor,
				TEXT("Resolved ground surface exceeded the server range."));
			return false;
		}

		FCollisionQueryParams VisibilityParams(
			SCENE_QUERY_STAT(RPGGladiatorGroundVisibility),
			false,
			SourceActor);
		FHitResult VisibilityHit;
		const FVector VisibilityStart =
			SourceActor->GetActorLocation() + FVector::UpVector * 50.0f;
		const FVector VisibilityEnd = OutLocation + FVector::UpVector * 25.0f;
		if (SourceActor->GetWorld()->LineTraceSingleByChannel(
			VisibilityHit,
			VisibilityStart,
			VisibilityEnd,
			ECC_Visibility,
			VisibilityParams) &&
			FVector::DistSquared(
				FVector(VisibilityHit.ImpactPoint), VisibilityEnd) >
				FMath::Square(100.0f))
		{
			ReportInvalidTarget(
				SourceActor,
				TEXT("Ground skill failed the server line-of-sight check."));
			return false;
		}

		return true;
	}

	FGameplayTag FindTag(const TCHAR* TagName)
	{
		return FGameplayTag::RequestGameplayTag(FName(TagName), false);
	}

	UAnimMontage* ResolveMeleeMontage(UAnimMontage* PreferredMontage)
	{
		if (PreferredMontage)
		{
			return PreferredMontage;
		}

		// The D1 sample did not ship its three melee skill montages with this project.
		// Use the current player skill montage for presentation while native hit logic runs.
		return LoadObject<UAnimMontage>(
			nullptr,
			TEXT("/Game/Blueprints/Character/Player/Anim/Montage/AM_PlayerSkill.AM_PlayerSkill"));
	}

	UAnimMontage* ResolveBuffMontage(UAnimMontage* PreferredMontage)
	{
		return PreferredMontage
			? PreferredMontage
			: LoadObject<UAnimMontage>(
				nullptr,
				TEXT("/Game/Blueprints/Character/Player/Anim/Montage/AM_PlayerToggleSkill.AM_PlayerToggleSkill"));
	}

	bool CommitIfGrounded(URPGGameplayAbility* Ability)
	{
		if (const ACharacter* Character = Cast<ACharacter>(Ability->GetAvatarActorFromActorInfo()))
		{
			if (Character->GetCharacterMovement() && Character->GetCharacterMovement()->IsFalling())
			{
				return false;
			}
		}

		return Ability->K2_CommitAbility();
	}

	void ApplyTimedStunFallback(AActor* TargetActor, const float Duration)
	{
		UAbilitySystemComponent* TargetASC =
			UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
		const FGameplayTag StunStatusTag = FindTag(TEXT("Status.Stun"));
		if (!TargetASC || !StunStatusTag.IsValid() || Duration <= 0.0f)
		{
			return;
		}

		TargetASC->AddLooseGameplayTag(StunStatusTag);
		ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor);
		if (TargetCharacter && TargetCharacter->GetCharacterMovement())
		{
			TargetCharacter->GetCharacterMovement()->DisableMovement();
		}

		TWeakObjectPtr<UAbilitySystemComponent> WeakASC(TargetASC);
		TWeakObjectPtr<ACharacter> WeakCharacter(TargetCharacter);
		FTimerHandle StunTimer;
		TargetActor->GetWorldTimerManager().SetTimer(
			StunTimer,
			FTimerDelegate::CreateLambda([WeakASC, WeakCharacter, StunStatusTag]()
			{
				if (!WeakASC.IsValid())
				{
					return;
				}

				WeakASC->RemoveLooseGameplayTag(StunStatusTag);
				if (WeakCharacter.IsValid() &&
					!WeakASC->HasMatchingGameplayTag(StunStatusTag) &&
					!WeakASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death))
				{
					if (UCharacterMovementComponent* Movement = WeakCharacter->GetCharacterMovement())
					{
						Movement->SetMovementMode(MOVE_Walking);
					}
				}
			}),
			Duration,
			false);
	}

	void TriggerStun(AActor* SourceActor, AActor* TargetActor, const float Duration)
	{
		UAbilitySystemComponent* TargetASC =
			UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
		const FGameplayTag StunEventTag = FindTag(TEXT("GameplayEvent.Stun"));
		if (!TargetASC || !StunEventTag.IsValid())
		{
			return;
		}

		FGameplayEventData StunPayload;
		StunPayload.Instigator = SourceActor;
		StunPayload.Target = TargetActor;
		StunPayload.EventMagnitude = Duration;
		if (TargetASC->HandleGameplayEvent(StunEventTag, &StunPayload) == 0)
		{
			ApplyTimedStunFallback(TargetActor, Duration);
		}
	}
}

URPGGameplayAbility_Skill_ShieldBash::URPGGameplayAbility_Skill_ShieldBash(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FRPGGladiatorEquipmentInfo& EquipmentInfo = EquipmentInfos.AddDefaulted_GetRef();
	EquipmentInfo.EquipmentType = ERPGGladiatorEquipmentType::Weapon;
	EquipmentInfo.WeaponHandType = EWeaponHandType::LeftHand;
	EquipmentInfo.RequiredWeaponType = ERPGGladiatorWeaponType::Shield;
}

URPGGameplayAbility_Skill_GroundBreaker::URPGGameplayAbility_Skill_GroundBreaker(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FRPGGladiatorEquipmentInfo& EquipmentInfo = EquipmentInfos.AddDefaulted_GetRef();
	EquipmentInfo.EquipmentType = ERPGGladiatorEquipmentType::Weapon;
	EquipmentInfo.WeaponHandType = EWeaponHandType::TwoHand;
	EquipmentInfo.RequiredWeaponType = ERPGGladiatorWeaponType::GreatSword;
}

URPGGameplayAbility_Skill_WhirlwindSlash::URPGGameplayAbility_Skill_WhirlwindSlash(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FRPGGladiatorEquipmentInfo& EquipmentInfo = EquipmentInfos.AddDefaulted_GetRef();
	EquipmentInfo.EquipmentType = ERPGGladiatorEquipmentType::Weapon;
	EquipmentInfo.WeaponHandType = EWeaponHandType::TwoHand;
	EquipmentInfo.RequiredWeaponType = ERPGGladiatorWeaponType::TwoHandSword;
}

URPGGameplayAbility_Skill_PiercingShot::URPGGameplayAbility_Skill_PiercingShot(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FRPGGladiatorEquipmentInfo& EquipmentInfo = EquipmentInfos.AddDefaulted_GetRef();
	EquipmentInfo.EquipmentType = ERPGGladiatorEquipmentType::Weapon;
	EquipmentInfo.WeaponHandType = EWeaponHandType::TwoHand;
	EquipmentInfo.RequiredWeaponType = ERPGGladiatorWeaponType::Bow;
}

URPGGameplayAbility_Skill_Targeting::URPGGameplayAbility_Skill_Targeting(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FRPGGladiatorEquipmentInfo& EquipmentInfo = EquipmentInfos.AddDefaulted_GetRef();
	EquipmentInfo.EquipmentType = ERPGGladiatorEquipmentType::Weapon;
	EquipmentInfo.WeaponHandType = EWeaponHandType::TwoHand;
	EquipmentInfo.RequiredWeaponType = ERPGGladiatorWeaponType::Staff;
}

URPGGameplayAbility_Skill_AOE::URPGGameplayAbility_Skill_AOE(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FRPGGladiatorEquipmentInfo& EquipmentInfo = EquipmentInfos.AddDefaulted_GetRef();
	EquipmentInfo.EquipmentType = ERPGGladiatorEquipmentType::Weapon;
	EquipmentInfo.WeaponHandType = EWeaponHandType::TwoHand;
	EquipmentInfo.RequiredWeaponType = ERPGGladiatorWeaponType::Staff;
}

void URPGGameplayAbility_Skill_Buff::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive())
	{
		return;
	}
	if (!RPGGladiatorAbility::CommitIfGrounded(this))
	{
		K2_EndAbility();
		return;
	}

	ApplyEffect();
	ApplyAdditionalEffects();

	if (UAnimMontage* MontageToPlay = RPGGladiatorAbility::ResolveBuffMontage(BuffMontage))
	{
		UAbilityTask_PlayMontageAndWait* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, TEXT("GladiatorBuff"), MontageToPlay);
		Task->OnCompleted.AddDynamic(this, &ThisClass::OnMontageFinished);
		Task->OnBlendOut.AddDynamic(this, &ThisClass::OnMontageFinished);
		Task->OnInterrupted.AddDynamic(this, &ThisClass::OnMontageFinished);
		Task->OnCancelled.AddDynamic(this, &ThisClass::OnMontageFinished);
		Task->ReadyForActivation();
		return;
	}

	K2_EndAbility();
}

void URPGGameplayAbility_Skill_Buff::ApplyEffect()
{
	if (!BuffGameplayEffectClass)
	{
		return;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(BuffGameplayEffectClass);
	if (SpecHandle.IsValid())
	{
		if (UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo())
		{
			FGameplayEffectContextHandle EffectContext = SourceASC->MakeEffectContext();
			EffectContext.AddSourceObject(BuffEffect);
			SpecHandle.Data->SetContext(EffectContext);
		}
		ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, SpecHandle);
	}
}

void URPGGameplayAbility_Skill_Buff::ApplyAdditionalEffects_Implementation()
{
}

void URPGGameplayAbility_Skill_Buff::OnMontageFinished()
{
	K2_EndAbility();
}

void URPGGameplayAbility_Skill_ShieldBash::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive())
	{
		return;
	}
	if (!RPGGladiatorAbility::CommitIfGrounded(this))
	{
		K2_EndAbility();
		return;
	}

	const bool bUsesImportedMontage = ShieldBashMontage != nullptr;
	if (bUsesImportedMontage)
	{
		if (UAbilityTask_WaitGameplayEvent* BeginTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, RPGGladiatorAbility::FindTag(TEXT("GameplayEvent.Montage.Begin")), nullptr, true, true))
		{
			BeginTask->EventReceived.AddDynamic(this, &ThisClass::OnShieldBashBegin);
			BeginTask->ReadyForActivation();
		}
	}
	else
	{
		OnShieldBashBegin(FGameplayEventData());
	}

	UAnimMontage* MontageToPlay = RPGGladiatorAbility::ResolveMeleeMontage(ShieldBashMontage);
	if (!MontageToPlay)
	{
		K2_EndAbility();
		return;
	}

	UAbilityTask_PlayMontageAndWait* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, TEXT("ShieldBash"), MontageToPlay);
	Task->OnCompleted.AddDynamic(this, &ThisClass::OnMontageFinished);
	Task->OnBlendOut.AddDynamic(this, &ThisClass::OnMontageFinished);
	Task->OnInterrupted.AddDynamic(this, &ThisClass::OnMontageFinished);
	Task->OnCancelled.AddDynamic(this, &ThisClass::OnMontageFinished);
	Task->ReadyForActivation();
}

void URPGGameplayAbility_Skill_ShieldBash::OnShieldBashBegin(FGameplayEventData Payload)
{
	ACharacter* SourceCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!SourceCharacter || !SourceCharacter->HasAuthority())
	{
		return;
	}

	UCapsuleComponent* Capsule = SourceCharacter->GetCapsuleComponent();
	if (!Capsule)
	{
		return;
	}

	const FVector OverlapLocation = SourceCharacter->GetActorLocation() +
		SourceCharacter->GetActorForwardVector() * Distance;
	const float Radius = Capsule->GetScaledCapsuleRadius() * RadiusMultiplier;
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes = {
		UEngineTypes::ConvertToObjectType(ECC_Pawn)
	};
	const TArray<AActor*> ActorsToIgnore = { SourceCharacter };
	TArray<AActor*> OverlappedActors;
	if (!UKismetSystemLibrary::CapsuleOverlapActors(
		this, OverlapLocation, Radius, HalfHeight, ObjectTypes,
		APawn::StaticClass(), ActorsToIgnore, OverlappedActors))
	{
		return;
	}

	for (AActor* TargetActor : OverlappedActors)
	{
		const FVector ImpactPoint = TargetActor->GetActorLocation();
		const FHitResult HitResult(
			TargetActor, nullptr, ImpactPoint,
			(TargetActor->GetActorLocation() - SourceCharacter->GetActorLocation()).GetSafeNormal());
		if (!TryProcessHitResult(
			HitResult, Damage, IsCharacterBlockingHit(TargetActor), nullptr,
			GetFirstEquipmentActor()))
		{
			continue;
		}

		UAbilitySystemComponent* TargetASC =
			UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
		const FGameplayTag KnockbackTag =
			RPGGladiatorAbility::FindTag(TEXT("GameplayEvent.Knockback"));
		FGameplayEventData KnockbackPayload;
		KnockbackPayload.Instigator = SourceCharacter;
		KnockbackPayload.Target = TargetActor;
		KnockbackPayload.EventMagnitude = StunDuration;
		const int32 TriggeredAbilities = TargetASC && KnockbackTag.IsValid()
			? TargetASC->HandleGameplayEvent(KnockbackTag, &KnockbackPayload)
			: 0;

		if (TriggeredAbilities == 0)
		{
			if (ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor))
			{
				if (URPGSecurityValidationComponent* Security =
					TargetCharacter->FindComponentByClass<
						URPGSecurityValidationComponent>())
				{
					Security->AuthorizeMovementDiscontinuity(
						0.5f,
						400.0f,
						TEXT("ShieldBashKnockback"));
				}
				const FVector LaunchDirection =
					(TargetActor->GetActorLocation() - SourceCharacter->GetActorLocation()).GetSafeNormal2D();
				TargetCharacter->LaunchCharacter(
					LaunchDirection * 600.0f + FVector::UpVector * 150.0f, true, true);
			}

			TWeakObjectPtr<AActor> WeakSource(SourceCharacter);
			TWeakObjectPtr<AActor> WeakTarget(TargetActor);
			FTimerHandle KnockbackTimer;
			TargetActor->GetWorldTimerManager().SetTimer(
				KnockbackTimer,
				FTimerDelegate::CreateLambda([WeakSource, WeakTarget, StunTime = StunDuration]()
				{
					if (WeakSource.IsValid() && WeakTarget.IsValid())
					{
						RPGGladiatorAbility::TriggerStun(
							WeakSource.Get(), WeakTarget.Get(), StunTime);
					}
				}),
				0.35f,
				false);
		}
	}
}

void URPGGameplayAbility_Skill_ShieldBash::OnMontageFinished()
{
	K2_EndAbility();
}

void URPGGameplayAbility_Skill_GroundBreaker::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive())
	{
		return;
	}
	if (!RPGGladiatorAbility::CommitIfGrounded(this))
	{
		K2_EndAbility();
		return;
	}

	const bool bUsesImportedMontage = GroundBreakerMontage != nullptr;
	if (bUsesImportedMontage)
	{
		if (UAbilityTask_WaitGameplayEvent* BeginTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, RPGGladiatorAbility::FindTag(TEXT("GameplayEvent.Montage.Begin")), nullptr, true, true))
		{
			BeginTask->EventReceived.AddDynamic(this, &ThisClass::OnGroundBreakerBegin);
			BeginTask->ReadyForActivation();
		}
	}
	else
	{
		OnGroundBreakerBegin(FGameplayEventData());
	}

	UAnimMontage* MontageToPlay = RPGGladiatorAbility::ResolveMeleeMontage(GroundBreakerMontage);
	if (!MontageToPlay)
	{
		K2_EndAbility();
		return;
	}

	UAbilityTask_PlayMontageAndWait* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, TEXT("GroundBreaker"), MontageToPlay);
	Task->OnCompleted.AddDynamic(this, &ThisClass::OnMontageFinished);
	Task->OnBlendOut.AddDynamic(this, &ThisClass::OnMontageFinished);
	Task->OnInterrupted.AddDynamic(this, &ThisClass::OnMontageFinished);
	Task->OnCancelled.AddDynamic(this, &ThisClass::OnMontageFinished);
	Task->ReadyForActivation();
}

void URPGGameplayAbility_Skill_GroundBreaker::OnGroundBreakerBegin(FGameplayEventData Payload)
{
	ACharacter* SourceCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!SourceCharacter || !SourceCharacter->HasAuthority())
	{
		return;
	}

	UCapsuleComponent* Capsule = SourceCharacter->GetCapsuleComponent();
	if (!Capsule)
	{
		return;
	}

	const FVector OverlapLocation = SourceCharacter->GetActorLocation() +
		SourceCharacter->GetActorForwardVector() * DistanceOffset;
	const FVector HalfSize(
		Capsule->GetScaledCapsuleRadius() * 3.0f,
		Capsule->GetScaledCapsuleRadius() * 3.0f,
		Capsule->GetScaledCapsuleHalfHeight());
	const TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes = {
		UEngineTypes::ConvertToObjectType(ECC_Pawn)
	};
	const TArray<AActor*> ActorsToIgnore = { SourceCharacter };
	TArray<AActor*> OverlappedActors;
	if (!UKismetSystemLibrary::BoxOverlapActors(
		this, OverlapLocation, HalfSize, ObjectTypes,
		APawn::StaticClass(), ActorsToIgnore, OverlappedActors))
	{
		return;
	}

	for (AActor* TargetActor : OverlappedActors)
	{
		const FVector ImpactPoint = TargetActor->GetActorLocation();
		const FHitResult HitResult(
			TargetActor, nullptr, ImpactPoint,
			(TargetActor->GetActorLocation() - SourceCharacter->GetActorLocation()).GetSafeNormal());
		if (TryProcessHitResult(
			HitResult, Damage, IsCharacterBlockingHit(TargetActor), nullptr,
			GetFirstEquipmentActor()))
		{
			RPGGladiatorAbility::TriggerStun(SourceCharacter, TargetActor, StunDruation);
		}
	}
}

void URPGGameplayAbility_Skill_GroundBreaker::OnMontageFinished()
{
	K2_EndAbility();
}

void URPGGameplayAbility_Skill_WhirlwindSlash::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive())
	{
		return;
	}
	if (!RPGGladiatorAbility::CommitIfGrounded(this))
	{
		K2_EndAbility();
		return;
	}

	const bool bUsesImportedMontage = WhirlwindSlashMontage != nullptr;
	if (bUsesImportedMontage)
	{
		if (UAbilityTask_WaitGameplayEvent* TraceTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, RPGGladiatorAbility::FindTag(TEXT("GameplayEvent.Trace")), nullptr, false, true))
		{
			TraceTask->EventReceived.AddDynamic(this, &ThisClass::OnTrace);
			TraceTask->ReadyForActivation();
		}
		if (UAbilityTask_WaitGameplayEvent* ResetTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, RPGGladiatorAbility::FindTag(TEXT("GameplayEvent.Reset")), nullptr, false, true))
		{
			ResetTask->EventReceived.AddDynamic(this, &ThisClass::OnReset);
			ResetTask->ReadyForActivation();
		}
		if (UAbilityTask_WaitGameplayEvent* BeginTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, RPGGladiatorAbility::FindTag(TEXT("GameplayEvent.Montage.Begin")), nullptr, true, true))
		{
			BeginTask->EventReceived.AddDynamic(this, &ThisClass::OnWhirlwindSlashBegin);
			BeginTask->ReadyForActivation();
		}
		if (UAbilityTask_WaitGameplayEvent* EndTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, RPGGladiatorAbility::FindTag(TEXT("GameplayEvent.Montage.End")), nullptr, true, true))
		{
			EndTask->EventReceived.AddDynamic(this, &ThisClass::OnWhirlwindSlashEnd);
			EndTask->ReadyForActivation();
		}
	}
	else
	{
		OnTrace(FGameplayEventData());
	}

	UAnimMontage* MontageToPlay = RPGGladiatorAbility::ResolveMeleeMontage(WhirlwindSlashMontage);
	if (!MontageToPlay)
	{
		K2_EndAbility();
		return;
	}

	UAbilityTask_PlayMontageAndWait* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, TEXT("WhirlwindSlash"), MontageToPlay);
	Task->OnCompleted.AddDynamic(this, &ThisClass::OnMontageFinished);
	Task->OnBlendOut.AddDynamic(this, &ThisClass::OnMontageFinished);
	Task->OnInterrupted.AddDynamic(this, &ThisClass::OnMontageFinished);
	Task->OnCancelled.AddDynamic(this, &ThisClass::OnMontageFinished);
	Task->ReadyForActivation();
}

void URPGGameplayAbility_Skill_WhirlwindSlash::OnTrace(FGameplayEventData Payload)
{
	ACharacter* SourceCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!SourceCharacter || !SourceCharacter->HasAuthority())
	{
		return;
	}

	TArray<int32> CharacterHitIndexes;
	TArray<int32> BlockHitIndexes;
	ParseTargetData(Payload.TargetData, CharacterHitIndexes, BlockHitIndexes);
	AActor* TraceWeaponActor = const_cast<AActor*>(Payload.Instigator.Get());
	if (!TraceWeaponActor)
	{
		TraceWeaponActor = GetFirstEquipmentActor();
	}
	for (const int32 Index : BlockHitIndexes)
	{
		if (const FGameplayAbilityTargetData* TargetData = Payload.TargetData.Get(Index))
		{
			if (const FHitResult* HitResult = TargetData->GetHitResult())
			{
				ProcessHitResult(*HitResult, Damage, true, nullptr, TraceWeaponActor);
			}
		}
	}
	for (const int32 Index : CharacterHitIndexes)
	{
		if (const FGameplayAbilityTargetData* TargetData = Payload.TargetData.Get(Index))
		{
			if (const FHitResult* HitResult = TargetData->GetHitResult())
			{
				ProcessHitResult(*HitResult, Damage, false, nullptr, TraceWeaponActor);
			}
		}
	}

}

void URPGGameplayAbility_Skill_WhirlwindSlash::OnReset(FGameplayEventData Payload)
{
	ResetHitActors();
}

void URPGGameplayAbility_Skill_WhirlwindSlash::OnWhirlwindSlashBegin(FGameplayEventData Payload)
{
}

void URPGGameplayAbility_Skill_WhirlwindSlash::OnWhirlwindSlashEnd(FGameplayEventData Payload)
{
}

void URPGGameplayAbility_Skill_WhirlwindSlash::OnMontageFinished()
{
	K2_EndAbility();
}

void URPGGameplayAbility_Skill_PiercingShot::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive())
	{
		return;
	}
	if (!RPGGladiatorAbility::CommitIfGrounded(this))
	{
		K2_EndAbility();
		return;
	}

	ACharacter* SourceCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (HasAuthority(&CurrentActivationInfo) && ProjectileClass && SourceCharacter)
	{
		FVector SpawnLocation = SourceCharacter->GetActorLocation() +
			SourceCharacter->GetActorForwardVector() * 100.0f + FVector::UpVector * 50.0f;
		if (USkeletalMeshComponent* CharacterMesh = SourceCharacter->GetMesh();
			CharacterMesh && !SpawnSocketName.IsNone() && CharacterMesh->DoesSocketExist(SpawnSocketName))
		{
			SpawnLocation = CharacterMesh->GetSocketLocation(SpawnSocketName);
		}

		FVector ViewLocation = SpawnLocation;
		FRotator ViewRotation = SourceCharacter->GetActorRotation();
		if (APlayerController* PlayerController = ActorInfo
			? Cast<APlayerController>(ActorInfo->PlayerController.Get())
			: nullptr)
		{
			PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
		}

		FVector AimPoint = SpawnLocation + ViewRotation.Vector() * AimAssistMaxDistance;
		if (bApplyAimAssist)
		{
			const float CameraToSocketDistance =
				(SpawnLocation - ViewLocation).Dot(ViewRotation.Vector());
			const FVector TraceStart = ViewLocation + ViewRotation.Vector() *
				FMath::Max(CameraToSocketDistance + AimAssistMinDistance, AimAssistMinDistance);
			const FVector TraceEnd = TraceStart + ViewRotation.Vector() * AimAssistMaxDistance;
			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PiercingShotAim), false, SourceCharacter);
			FHitResult AimHit;
			AimPoint = GetWorld()->LineTraceSingleByChannel(
				AimHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams)
				? AimHit.ImpactPoint
				: TraceEnd;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = SourceCharacter;
		SpawnParameters.Instigator = SourceCharacter;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<AActor>(
			ProjectileClass,
			SpawnLocation,
			(AimPoint - SpawnLocation).Rotation(),
			SpawnParameters);
	}

	if (UAnimMontage* MontageToPlay = RPGGladiatorAbility::ResolveMeleeMontage(ReleaseMontage))
	{
		UAbilityTask_PlayMontageAndWait* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, TEXT("PiercingShot"), MontageToPlay);
		Task->OnCompleted.AddDynamic(this, &ThisClass::OnMontageFinished);
		Task->OnBlendOut.AddDynamic(this, &ThisClass::OnMontageFinished);
		Task->OnInterrupted.AddDynamic(this, &ThisClass::OnMontageFinished);
		Task->OnCancelled.AddDynamic(this, &ThisClass::OnMontageFinished);
		Task->ReadyForActivation();
		return;
	}

	K2_EndAbility();
}

void URPGGameplayAbility_Skill_PiercingShot::OnPiercingShotBegin(FGameplayEventData Payload)
{
}

void URPGGameplayAbility_Skill_PiercingShot::OnInputConfirm()
{
}

void URPGGameplayAbility_Skill_PiercingShot::OnInputCancel()
{
	K2_EndAbility();
}

void URPGGameplayAbility_Skill_PiercingShot::OnMontageFinished()
{
	K2_EndAbility();
}

void URPGGameplayAbility_Skill_Targeting::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive())
	{
		return;
	}
	WaitTargetData();
}

void URPGGameplayAbility_Skill_Targeting::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	ResetSkill();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void URPGGameplayAbility_Skill_Targeting::ConfirmSkill()
{
	if (!RPGGladiatorAbility::CommitIfGrounded(this))
	{
		CancelSkill();
		return;
	}

	if (HasAuthority(&CurrentActivationInfo))
	{
		FGameplayAbilityTargetDataHandle ValidatedTargetData;
		if (!RPGGladiatorAbility::ValidateTargetedEffectData(
			GetAvatarActorFromActorInfo(),
			TargetDataHandle,
			MaxRange,
			ValidatedTargetData))
		{
			CancelSkill();
			return;
		}
		for (const TSubclassOf<UGameplayEffect>& EffectClass : GameplayEffectClasses)
		{
			if (EffectClass)
			{
				const FGameplayEffectSpecHandle EffectSpec = MakeOutgoingGameplayEffectSpec(EffectClass);
				if (EffectSpec.IsValid())
				{
					ApplyGameplayEffectSpecToTarget(
						CurrentSpecHandle,
						CurrentActorInfo,
						CurrentActivationInfo,
						EffectSpec,
						ValidatedTargetData);
				}
			}
		}
	}

	if (SpellMontage)
	{
		UAbilityTask_PlayMontageAndWait* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, TEXT("TargetingSpell"), SpellMontage);
		Task->OnCompleted.AddDynamic(this, &ThisClass::OnMontageFinished);
		Task->OnBlendOut.AddDynamic(this, &ThisClass::OnMontageFinished);
		Task->OnInterrupted.AddDynamic(this, &ThisClass::OnMontageFinished);
		Task->OnCancelled.AddDynamic(this, &ThisClass::OnMontageFinished);
		Task->ReadyForActivation();
		return;
	}

	K2_EndAbility();
}

void URPGGameplayAbility_Skill_Targeting::CancelSkill()
{
	K2_EndAbility();
}

void URPGGameplayAbility_Skill_Targeting::ResetSkill()
{
	TargetDataHandle.Clear();
}

void URPGGameplayAbility_Skill_Targeting::OnMontageFinished()
{
	K2_EndAbility();
}

void URPGGameplayAbility_Skill_AOE::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive())
	{
		return;
	}
	WaitTargetData();
}

void URPGGameplayAbility_Skill_AOE::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	ResetSkill();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void URPGGameplayAbility_Skill_AOE::ConfirmSkill()
{
	if (!RPGGladiatorAbility::CommitIfGrounded(this))
	{
		CancelSkill();
		return;
	}

	if (HasAuthority(&CurrentActivationInfo) && AOESpawnerClass && GetAvatarActorFromActorInfo())
	{
		AActor* Avatar = GetAvatarActorFromActorInfo();
		FVector SpawnLocation = Avatar->GetActorLocation();
		if (!RPGGladiatorAbility::ResolveGroundTargetLocation(
			Avatar,
			TargetDataHandle,
			MaxRange,
			AcceptanceMultiplier,
			CollisionHeight,
			TraceProfile,
			SpawnLocation))
		{
			CancelSkill();
			return;
		}
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = Avatar;
		SpawnParameters.Instigator = Cast<APawn>(Avatar);
		GetWorld()->SpawnActor<AActor>(
			AOESpawnerClass, SpawnLocation, Avatar->GetActorRotation(), SpawnParameters);
	}

	if (SpellMontage)
	{
		UAbilityTask_PlayMontageAndWait* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, TEXT("AOESpell"), SpellMontage);
		Task->OnCompleted.AddDynamic(this, &ThisClass::OnMontageFinished);
		Task->OnBlendOut.AddDynamic(this, &ThisClass::OnMontageFinished);
		Task->OnInterrupted.AddDynamic(this, &ThisClass::OnMontageFinished);
		Task->OnCancelled.AddDynamic(this, &ThisClass::OnMontageFinished);
		Task->ReadyForActivation();
		return;
	}

	K2_EndAbility();
}

void URPGGameplayAbility_Skill_AOE::CancelSkill()
{
	K2_EndAbility();
}

void URPGGameplayAbility_Skill_AOE::ResetSkill()
{
	TargetDataHandle.Clear();
}

void URPGGameplayAbility_Skill_AOE::OnMontageFinished()
{
	K2_EndAbility();
}
