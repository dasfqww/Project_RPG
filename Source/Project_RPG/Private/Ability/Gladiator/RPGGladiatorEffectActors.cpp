#include "Ability/Gladiator/RPGGladiatorEffectActors.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Attribute/RPGAttributeSet.h"
#include "Camera/CameraShakeBase.h"
#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TimelineComponent.h"
#include "Curves/CurveFloat.h"
#include "DataAsset/Definition/RPGGameData.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "FunctionLibrary/RPGCombatFunctionLibrary.h"
#include "FunctionLibrary/RPGSecurityBlueprintLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGGladiatorEffectActors)

namespace RPGGladiatorEffectActors
{
	UAbilitySystemComponent* FindAbilitySystem(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}

		if (UAbilitySystemComponent* AbilitySystem =
			UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor))
		{
			return AbilitySystem;
		}

		return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor->GetOwner());
	}

	bool ApplyDamage(AActor* SourceActor, AActor* TargetActor, const FHitResult& HitResult,
		const float Damage, const TSubclassOf<UGameplayEffect> AdditionalEffect,
		AActor* EffectCauser,
		const FRPGSkillSecurityProfile& SecurityProfile)
	{
		UAbilitySystemComponent* SourceASC = FindAbilitySystem(SourceActor);
		UAbilitySystemComponent* TargetASC = FindAbilitySystem(TargetActor);
		if (!SourceASC || !TargetASC || SourceASC == TargetASC)
		{
			return false;
		}

		AActor* SourceAvatar = SourceASC->GetAvatarActor();
		AActor* TargetAvatar = TargetASC->GetAvatarActor();
		if (!SourceAvatar || !TargetAvatar || !SourceAvatar->HasAuthority())
		{
			return false;
		}

		FVector ImpactPoint(HitResult.ImpactPoint);
		if (ImpactPoint.ContainsNaN() ||
			(ImpactPoint.IsNearlyZero() &&
			 !TargetAvatar->GetActorLocation().IsNearlyZero()))
		{
			ImpactPoint = TargetAvatar->GetActorLocation();
		}
		FVector ImpactNormal(HitResult.ImpactNormal);
		if (ImpactNormal.ContainsNaN())
		{
			ImpactNormal =
				(ImpactPoint - SourceAvatar->GetActorLocation()).GetSafeNormal();
		}
		const FHitResult SecurityHit(
			TargetAvatar,
			nullptr,
			ImpactPoint,
			ImpactNormal);
		FText RejectionReason;
		if (!URPGSecurityBlueprintLibrary::ValidateAuthorizedServerHit(
			SourceAvatar,
			SecurityHit,
			Damage,
			SecurityProfile,
			RejectionReason))
		{
			return false;
		}

		FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
		Context.AddHitResult(HitResult);
		Context.AddInstigator(SourceASC->GetAvatarActor(), EffectCauser ? EffectCauser : SourceActor);

		const URPGGameData* GameData = LoadObject<URPGGameData>(
			nullptr, TEXT("/GladiatorCore/Data/GameData_GladiatorGame.GameData_GladiatorGame"));
		const TSubclassOf<UGameplayEffect> DamageEffect = GameData
			? GameData->DamageGameplayEffect_SetByCaller.LoadSynchronous()
			: nullptr;

		if (DamageEffect)
		{
			FGameplayEffectSpecHandle DamageSpec = SourceASC->MakeOutgoingSpec(DamageEffect, 1.0f, Context);
			if (!DamageSpec.IsValid())
			{
				return false;
			}

			const FGameplayTag D1DamageTag = FGameplayTag::RequestGameplayTag(
				TEXT("SetByCaller.BaseDamage"), false);
			const FGameplayTag RPGDamageTag = FGameplayTag::RequestGameplayTag(
				TEXT("Shared.SetByCaller.BaseDamage"), false);
			if (D1DamageTag.IsValid())
			{
				DamageSpec.Data->SetSetByCallerMagnitude(D1DamageTag, Damage);
			}
			if (RPGDamageTag.IsValid())
			{
				DamageSpec.Data->SetSetByCallerMagnitude(RPGDamageTag, Damage);
			}
			const FActiveGameplayEffectHandle DamageHandle =
				SourceASC->ApplyGameplayEffectSpecToTarget(
					*DamageSpec.Data.Get(), TargetASC);
			if (!DamageHandle.WasSuccessfullyApplied())
			{
				// Never bypass target immunity or application requirements with a
				// direct attribute mutation when the authored GE rejects the hit.
				return false;
			}
		}
		else
		{
			// Compatibility fallback for imported D1 content without GameData.
			TargetASC->ApplyModToAttribute(
				URPGAttributeSet::GetCurrentHealthAttribute(),
				EGameplayModOp::Additive,
				-Damage);
		}

		if (AdditionalEffect)
		{
			const FGameplayEffectSpecHandle AdditionalSpec =
				SourceASC->MakeOutgoingSpec(AdditionalEffect, 1.0f, Context);
			if (AdditionalSpec.IsValid())
			{
				TargetASC->ApplyGameplayEffectSpecToSelf(*AdditionalSpec.Data.Get());
			}
		}

		return true;
	}
}

ARPGGladiatorProjectileBase::ARPGGladiatorProjectileBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicateMovement(true);
	bNetLoadOnClient = false;
	bReplicates = true;
	InitialLifeSpan = 5.0f;

	SphereCollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("SphereCollisionComponent"));
	SphereCollisionComponent->SetCollisionProfileName(TEXT("Projectile"));
	SphereCollisionComponent->bReturnMaterialOnMove = true;
	SphereCollisionComponent->SetCanEverAffectNavigation(false);
	SetRootComponent(SphereCollisionComponent);

	ProjectileMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileMeshComponent"));
	ProjectileMeshComponent->SetCollisionProfileName(TEXT("NoCollision"));
	ProjectileMeshComponent->SetupAttachment(SphereCollisionComponent);
	ProjectileMeshComponent->SetCanEverAffectNavigation(false);

	TrailNiagaraComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("TrailNiagaraComponent"));
	TrailNiagaraComponent->SetupAttachment(ProjectileMeshComponent);

	ProjectileMovementComponent = CreateDefaultSubobject<UProjectileMovementComponent>(
		TEXT("ProjectileMovementComponent"));
	ProjectileMovementComponent->bRotationFollowsVelocity = true;
}

void ARPGGladiatorProjectileBase::BeginPlay()
{
	Super::BeginPlay();
	SphereCollisionComponent->IgnoreActorWhenMoving(GetInstigator(), true);

	if (CollisionDetectionType == ERPGGladiatorCollisionDetectionType::Hit)
	{
		SphereCollisionComponent->SetGenerateOverlapEvents(false);
		SphereCollisionComponent->OnComponentHit.AddUniqueDynamic(this, &ThisClass::HandleComponentHit);
	}
	else if (CollisionDetectionType == ERPGGladiatorCollisionDetectionType::Overlap)
	{
		SphereCollisionComponent->SetGenerateOverlapEvents(true);
		SphereCollisionComponent->OnComponentBeginOverlap.AddUniqueDynamic(
			this, &ThisClass::HandleComponentOverlap);
	}
}

void ARPGGladiatorProjectileBase::Destroyed()
{
	if (HitActors.IsEmpty() && HitEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HitEffect, GetActorLocation());
	}

	if (AttachingComponent.IsValid())
	{
		AttachingComponent->OnComponentDeactivated.RemoveDynamic(
			this, &ThisClass::HandleOtherComponentDeactivated);
	}
	Super::Destroyed();
}

void ARPGGladiatorProjectileBase::SetSpeed(const float Speed)
{
	const FVector Direction = ProjectileMovementComponent->Velocity.IsNearlyZero()
		? GetActorForwardVector()
		: ProjectileMovementComponent->Velocity.GetSafeNormal();
	ProjectileMovementComponent->Velocity = Direction * Speed;
}

void ARPGGladiatorProjectileBase::HandleComponentHit(UPrimitiveComponent* HitComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse,
	const FHitResult& HitResult)
{
	if (!OtherActor || HitActors.Num() > 0)
	{
		return;
	}

	HitActors.Add(OtherActor);
	SphereCollisionComponent->Deactivate();
	TrailNiagaraComponent->Deactivate();
	ProjectileMovementComponent->Deactivate();

	if (HasAuthority() && bAttachToHitComponent && OtherComponent)
	{
		AttachingComponent = OtherComponent;
		OtherComponent->OnComponentDeactivated.AddUniqueDynamic(
			this, &ThisClass::HandleOtherComponentDeactivated);
		AttachToComponent(OtherComponent, FAttachmentTransformRules::KeepWorldTransform,
			HitResult.BoneName);
	}
	else
	{
		SetLifeSpan(2.0f);
	}

	HandleCollisionDetection(OtherActor, OtherComponent, HitResult);
}

void ARPGGladiatorProjectileBase::HandleComponentOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (!OtherActor || HitActors.Contains(OtherActor))
	{
		return;
	}

	HitActors.Add(OtherActor);
	FHitResult HitResult = SweepResult;
	HitResult.bBlockingHit = bFromSweep;
	HandleCollisionDetection(OtherActor, OtherComponent, HitResult);
}

void ARPGGladiatorProjectileBase::HandleOtherComponentDeactivated(UActorComponent* OtherComponent)
{
	if (HasAuthority())
	{
		Destroy();
	}
}

void ARPGGladiatorProjectileBase::HandleCollisionDetection(AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, const FHitResult& HitResult)
{
	if (!OtherActor || OtherActor == GetOwner() || OtherActor == GetInstigator())
	{
		return;
	}

	if (HasAuthority())
	{
		if (ServerAppliedHitCount >= FMath::Max(
			1,
			SecurityProfile.MaximumHitsPerActivation))
		{
			return;
		}
		if (RPGGladiatorEffectActors::ApplyDamage(
			GetOwner(),
			OtherActor,
			HitResult,
			Damage,
			nullptr,
			this,
			SecurityProfile))
		{
			++ServerAppliedHitCount;
		}
	}
	else if (HitEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HitEffect, HitResult.ImpactPoint);
	}

	if (HitGameplayCueTag.IsValid())
	{
		if (UAbilitySystemComponent* SourceASC =
			RPGGladiatorEffectActors::FindAbilitySystem(GetOwner()))
		{
			FGameplayCueParameters CueParameters;
			CueParameters.Location = HitResult.ImpactPoint;
			CueParameters.Normal = HitResult.ImpactNormal;
			CueParameters.PhysicalMaterial = HitResult.PhysMaterial;
			SourceASC->ExecuteGameplayCue(HitGameplayCueTag, CueParameters);
		}
	}
}

ARPGGladiatorAOEBase::ARPGGladiatorAOEBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;

	ArrowComponent = CreateDefaultSubobject<UArrowComponent>(TEXT("ArrowComponent"));
	SetRootComponent(ArrowComponent);

	SphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("SphereComponent"));
	SphereComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SphereComponent->SetupAttachment(ArrowComponent);
}

void ARPGGladiatorAOEBase::BeginPlay()
{
	Super::BeginPlay();
	AttackIntervalTime = TargetAttackCount > 0 ? AttackTotalTime / TargetAttackCount : 0.0f;
	if (AttackIntervalTime <= 0.0f || !AOEElementClass)
	{
		Destroy();
		return;
	}

	GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::StartAOE);
}

void ARPGGladiatorAOEBase::StartAOE()
{
	GetWorldTimerManager().SetTimer(
		AOETimerHandle, this, &ThisClass::TickAOE, AttackIntervalTime, true, StartDelay);
}

void ARPGGladiatorAOEBase::TickAOE()
{
	if (HasAuthority())
	{
		FVector RandomDirection = FMath::VRand();
		RandomDirection.Z = 0.0f;
		RandomDirection.Normalize();
		const FVector SpawnLocation = SphereComponent->GetComponentLocation()
			+ RandomDirection * FMath::RandRange(0.0f, SphereComponent->GetScaledSphereRadius());

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = GetOwner();
		SpawnParameters.Instigator = Cast<APawn>(GetOwner());
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<AActor>(AOEElementClass, SpawnLocation, FRotator::ZeroRotator,
			SpawnParameters);

		if (++CurrentAttackCount >= TargetAttackCount)
		{
			GetWorldTimerManager().ClearTimer(AOETimerHandle);
			Destroy();
		}
	}
	else if (CameraShakeClass)
	{
		const APawn* OwnerPawn = Cast<APawn>(GetOwner());
		if (APlayerController* PlayerController = OwnerPawn
			? Cast<APlayerController>(OwnerPawn->GetController())
			: nullptr)
		{
			PlayerController->ClientStartCameraShake(CameraShakeClass);
		}
	}
}

ARPGGladiatorAOEElementBase::ARPGGladiatorAOEElementBase(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	InitialLifeSpan = 2.0f;

	ArrowComponent = CreateDefaultSubobject<UArrowComponent>(TEXT("ArrowComponent"));
	SetRootComponent(ArrowComponent);

	CollisionComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionComponent"));
	CollisionComponent->SetCapsuleHalfHeight(88.0f);
	CollisionComponent->SetCapsuleRadius(44.0f);
	CollisionComponent->SetCollisionProfileName(TEXT("NoCollision"));
	CollisionComponent->bReturnMaterialOnMove = true;
	CollisionComponent->SetupAttachment(ArrowComponent);

	NiagaraComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("NiagaraComponent"));
	NiagaraComponent->SetupAttachment(CollisionComponent);

	TimelineComponent = CreateDefaultSubobject<UTimelineComponent>(TEXT("TimelineComponent"));
}

void ARPGGladiatorAOEElementBase::BeginPlay()
{
	Super::BeginPlay();

	if (ElementType == ERPGGladiatorAOEElementType::Projectile && CurveData)
	{
		FVector Location = CollisionComponent->GetRelativeLocation();
		Location.Z = CurveData->GetFloatValue(0.0f);
		CollisionComponent->SetRelativeLocation(Location);
		CollisionComponent->SetCollisionProfileName(TEXT("Projectile"));

		FOnTimelineFloat TickCallback;
		TickCallback.BindDynamic(this, &ThisClass::OnTimelineTick);
		FOnTimelineEvent FinishedCallback;
		FinishedCallback.BindDynamic(this, &ThisClass::OnTimelineFinished);
		TimelineComponent->AddInterpFloat(CurveData, TickCallback);
		TimelineComponent->SetTimelineFinishedFunc(FinishedCallback);
		TimelineComponent->SetLooping(false);
		TimelineComponent->PlayFromStart();
	}
	else if (ElementType == ERPGGladiatorAOEElementType::Explosion)
	{
		if (HasAuthority())
		{
			TArray<FHitResult> HitResults;
			const FVector TraceLocation = CollisionComponent->GetComponentLocation();
			if (PerformTrace(TraceLocation, TraceLocation, HitResults))
			{
				TSet<AActor*> UniqueActors;
				for (const FHitResult& HitResult : HitResults)
				{
					if (AActor* HitActor = HitResult.GetActor();
						HitActor && !UniqueActors.Contains(HitActor))
					{
						UniqueActors.Add(HitActor);
						HandleCollisionDetection(HitResult);
					}
				}
			}
		}
		CollisionComponent->Deactivate();
	}
}

void ARPGGladiatorAOEElementBase::OnTimelineTick(const float Value)
{
	FHitResult HitResult;
	FVector Location = CollisionComponent->GetRelativeLocation();
	Location.Z = Value;
	CollisionComponent->SetRelativeLocation(Location, true, &HitResult);
	if (!HitResult.GetActor())
	{
		return;
	}

	if (HasAuthority())
	{
		HandleCollisionDetection(HitResult);
	}
	if (HitNiagaraEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, HitNiagaraEffect, NiagaraComponent->GetComponentLocation());
	}
	TimelineComponent->Stop();
	CollisionComponent->Deactivate();
	NiagaraComponent->Deactivate();
}

void ARPGGladiatorAOEElementBase::OnTimelineFinished()
{
	CollisionComponent->Deactivate();
	NiagaraComponent->Deactivate();
	if (HitNiagaraEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, HitNiagaraEffect, NiagaraComponent->GetComponentLocation());
	}
}

bool ARPGGladiatorAOEElementBase::PerformTrace(const FVector& StartLocation,
	const FVector& EndLocation, TArray<FHitResult>& OutHitResults)
{
	const TArray<AActor*> IgnoredActors = {GetOwner()};
	return UKismetSystemLibrary::SphereTraceMultiByProfile(
		this,
		StartLocation + FVector(0.0f, 0.0f, CollisionComponent->GetScaledCapsuleHalfHeight()),
		EndLocation - FVector(0.0f, 0.0f, CollisionComponent->GetScaledCapsuleHalfHeight()),
		CollisionComponent->GetScaledCapsuleRadius(),
		TEXT("Projectile"),
		false,
		IgnoredActors,
		EDrawDebugTrace::None,
		OutHitResults,
		true);
}

void ARPGGladiatorAOEElementBase::HandleCollisionDetection(const FHitResult& HitResult)
{
	AActor* HitActor = HitResult.GetActor();
	if (!HasAuthority() || !HitActor || HitActor == GetOwner())
	{
		return;
	}
	if (ServerAppliedActors.Contains(HitActor) ||
		ServerAppliedActors.Num() >= FMath::Max(
			1,
			SecurityProfile.MaximumHitsPerActivation))
	{
		return;
	}

	if (RPGGladiatorEffectActors::ApplyDamage(
		GetOwner(),
		HitActor,
		HitResult,
		Damage,
		AdditionalEffect,
		this,
		SecurityProfile))
	{
		ServerAppliedActors.Add(HitActor);
	}
}
