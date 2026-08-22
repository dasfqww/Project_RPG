// Fill out your copyright notice in the Description page of Project Settings.


#include "Item/RPGProjectileBase.h"
#include "Components/BoxComponent.h"
#include "NiagaraComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
#include "FunctionLibrary/RPGCombatFunctionLibrary.h"
#include "RPGGameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"

#include "RPGDebugHelper.h"

// Sets default values
ARPGProjectileBase::ARPGProjectileBase()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	PrimaryActorTick.bCanEverTick = false;

	ProjectileCollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ProjectileCollisionBox"));
	SetRootComponent(ProjectileCollisionBox);
	ProjectileCollisionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ProjectileCollisionBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	ProjectileCollisionBox->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	ProjectileCollisionBox->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	ProjectileCollisionBox->OnComponentHit.AddUniqueDynamic(this, &ThisClass::OnProjectileHit);
	ProjectileCollisionBox->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::OnProjectileBeginOverlap);

	ProjectileNiagaraComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ProjectileNiagaraComponent"));
	ProjectileNiagaraComponent->SetupAttachment(GetRootComponent());

	ProjectileMovementComp = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovementComp"));
	ProjectileMovementComp->InitialSpeed = 700.f;
	ProjectileMovementComp->MaxSpeed = 900.f;
	ProjectileMovementComp->Velocity = FVector(1.f, 0.f, 0.f);
	ProjectileMovementComp->ProjectileGravityScale = 0.f;

	InitialLifeSpan = 4.f;
}

// Called when the game starts or when spawned
void ARPGProjectileBase::BeginPlay()
{
	Super::BeginPlay();
	
	if (ProjectileDamagePolicy == EProjectileDamagePolicy::OnBeginOverlap)
	{
		ProjectileCollisionBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}

}

void ARPGProjectileBase::OnProjectileHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	ProcessProjectileImpact(OtherActor, OtherComp, Hit);
}

void ARPGProjectileBase::OnProjectileBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (ProjectileDamagePolicy != EProjectileDamagePolicy::OnBeginOverlap)
	{
		return;
	}
	ProcessProjectileImpact(OtherActor, OtherComp, SweepResult);
}

void ARPGProjectileBase::ProcessProjectileImpact(
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	const FHitResult& HitResult)
{
	if (bImpactHandled || !IsValid(OtherActor) || OtherActor == GetInstigator())
	{
		return;
	}
	bImpactHandled = true;

	FVector ImpactPoint(HitResult.ImpactPoint);
	if (ImpactPoint.ContainsNaN() ||
		(ImpactPoint.IsNearlyZero() &&
		 !OtherActor->GetActorLocation().IsNearlyZero()))
	{
		ImpactPoint = OtherActor->GetActorLocation();
	}
	FVector ImpactNormal(HitResult.ImpactNormal);
	if (ImpactNormal.ContainsNaN())
	{
		ImpactNormal = (ImpactPoint - GetActorLocation()).GetSafeNormal();
	}
	const FHitResult ServerHit(
		OtherActor,
		OtherComponent,
		ImpactPoint,
		ImpactNormal);

	BP_OnSpawnProjectileHitFX(ImpactPoint);
	if (!HasAuthority())
	{
		// Predicted projectiles only present impact FX. The server copy owns
		// collision admission, block events, damage, and destruction.
		Destroy();
		return;
	}

	APawn* HitPawn = Cast<APawn>(OtherActor);
	if (!HitPawn ||
		!URPGCombatFunctionLibrary::IsTargetPawnHostile(GetInstigator(), HitPawn))
	{
		Destroy();
		return;
	}

	const bool bIsPlayerBlocking =
		URPGAbilityFunctionLibrary::NativeDoesActorHaveTag(
			HitPawn,
			RPGGameplayTags::Player_Status_Blocking);
	const bool bIsValidBlock = bIsPlayerBlocking &&
		URPGCombatFunctionLibrary::IsValidBlock(this, HitPawn);

	FGameplayEventData Data;
	Data.Instigator = this;
	Data.Target = HitPawn;
	if (bIsValidBlock)
	{
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			HitPawn,
			RPGGameplayTags::Player_Event_SuccessfulBlock,
			Data);
	}
	else
	{
		HandleApplyProjectileDamage(HitPawn, ServerHit, Data);
	}

	Destroy();
}

void ARPGProjectileBase::HandleApplyProjectileDamage(
	APawn* InHitPawn,
	const FHitResult& ServerHit,
	const FGameplayEventData& InPayload)
{
	if (!HasAuthority() || !IsValid(InHitPawn) ||
		!ProjectileDamageEffectSpecHandle.IsValid())
	{
		return;
	}

	const bool bWasApplied =
		URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(
			GetInstigator(),
			ServerHit,
			ProjectileDamageEffectSpecHandle,
			SecurityProfile);

	if (bWasApplied)
	{
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			InHitPawn,
			RPGGameplayTags::Shared_Event_HitReact,
			InPayload
		);
	}
}
