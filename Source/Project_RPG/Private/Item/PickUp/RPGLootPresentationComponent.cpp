#include "Item/PickUp/RPGLootPresentationComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"

namespace RPGLootPresentation
{
	constexpr float SettleDuration = 0.18f;
	constexpr double SoundGrace = 0.25;
	const TCHAR* LandingCue(ERPGLootRarity Rarity)
	{
		switch (Rarity)
		{
		case ERPGLootRarity::Uncommon: return TEXT("Abstract_Pop_03_Cue");
		case ERPGLootRarity::Rare: return TEXT("Special_Musical_01_Cue");
		case ERPGLootRarity::Epic: return TEXT("Special_Powerup_04_Cue");
		case ERPGLootRarity::Legendary: return TEXT("Special_Powerup_10_Cue");
		default: return TEXT("Pop_02_Cue");
		}
	}
}

URPGLootPresentationComponent::URPGLootPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
	SoundConcurrency = TSoftObjectPtr<USoundConcurrency>(FSoftObjectPath(
		TEXT("/Game/DrapEffet/Integration/SC_LootDrops.SC_LootDrops")));
	CollectedSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(
		TEXT("/Game/Interface_And_Item_Sounds/Cues/Abstract_Pop_01_Cue.Abstract_Pop_01_Cue")));
}

void URPGLootPresentationComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, Flight);
}

double URPGLootPresentationComponent::ServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World) return 0;
	const AGameStateBase* State = World->GetGameState();
	return State ? State->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

FVector URPGLootPresentationComponent::EvaluateArc(const FVector& Start, const FVector& End,
	const float Alpha, const float Height)
{
	const float T = FMath::Clamp(Alpha, 0.f, 1.f);
	return FMath::Lerp(Start, End, T) + FVector::UpVector * (4.f * FMath::Max(0.f, Height) * T * (1.f - T));
}

float URPGLootPresentationComponent::EvaluateSettle(const float SecondsAfterLanding)
{
	const float T = FMath::Clamp(SecondsAfterLanding / RPGLootPresentation::SettleDuration, 0.f, 1.f);
	return 7.f * FMath::Sin(PI * T) * (1.f - T);
}

FName URPGLootPresentationComponent::DefaultEffectName(const ERPGLootRarity InRarity)
{
	switch (InRarity)
	{
	case ERPGLootRarity::Uncommon: return TEXT("NE_drop_effects06");
	case ERPGLootRarity::Rare: return TEXT("NE_drop_effects03");
	case ERPGLootRarity::Epic: return TEXT("NE_drop_effects04");
	case ERPGLootRarity::Legendary: return TEXT("NE_drop_effects01");
	default: return TEXT("NE_drop_effect05");
	}
}

void URPGLootPresentationComponent::InitializeRestingDrop(const FGuid& DropId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !DropId.IsValid() || Flight.DropId == DropId) return;
	Flight = {};
	Flight.DropId = DropId;
	GetOwner()->ForceNetUpdate();
	if (HasBegunPlay()) RefreshDrop();
}

bool URPGLootPresentationComponent::BeginDropFlight(const FGuid& DropId, const FVector& Origin, const int32 Seed)
{
	if (!bEnabled || !GetOwner() || !GetOwner()->HasAuthority() || !DropId.IsValid() || Origin.ContainsNaN()) return false;
	if (Flight.DropId == DropId && Flight.Duration > 0) return false;
	if (HasBegunPlay()) ResetPresentation();
	Flight.DropId = DropId;
	Flight.Origin = Origin;
	Flight.StartedAt = ServerTime() + FMath::Clamp(Seed, 0, 12) * 0.025;
	Flight.Duration = FMath::Clamp(FlightDuration, 0.2f, 2.f);
	Flight.Height = FMath::Clamp(ArcHeight, 0.f, 300.f);
	Flight.Seed = Seed;
	GetOwner()->ForceNetUpdate();
	if (HasBegunPlay()) RefreshDrop();
	return true;
}

bool URPGLootPresentationComponent::IsInFlight() const
{
	return bEnabled && Flight.DropId.IsValid() && Flight.Duration > 0 &&
		ServerTime() < Flight.StartedAt + Flight.Duration;
}

void URPGLootPresentationComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!bEnabled || GetNetMode() == NM_DedicatedServer) return;
	SourceMesh = Cast<UStaticMeshComponent>(GetOwner()->GetRootComponent());
	if (!SourceMesh || !SourceMesh->GetStaticMesh()) return;
	bSourceWasVisible = SourceMesh->IsVisible();
	VisualMesh = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("LootVisualMesh"));
	VisualMesh->SetupAttachment(SourceMesh);
	VisualMesh->SetStaticMesh(SourceMesh->GetStaticMesh());
	for (int32 Index = 0; Index < SourceMesh->GetNumMaterials(); ++Index)
		VisualMesh->SetMaterial(Index, SourceMesh->GetMaterial(Index));
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetCastShadow(SourceMesh->CastShadow);
	VisualMesh->SetRenderCustomDepth(SourceMesh->bRenderCustomDepth);
	VisualMesh->SetCustomDepthStencilValue(SourceMesh->CustomDepthStencilValue);
	VisualMesh->SetCanEverAffectNavigation(false);
	VisualMesh->RegisterComponent();
	// Keep the real collision/interaction shape and replicated transform stationary.
	SourceMesh->SetVisibility(false, false);
	LoadPresentationAssets();
	RefreshDrop();
}

void URPGLootPresentationComponent::LoadPresentationAssets()
{
	const FString EffectName = DefaultEffectName(Rarity).ToString();
	ResolvedEffect = GroundEffect.IsNull() ? TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(
		FString::Printf(TEXT("/Game/DrapEffet/VFX/%s.%s"), *EffectName, *EffectName))) : GroundEffect;
	const FString Cue = RPGLootPresentation::LandingCue(Rarity);
	ResolvedLandingSound = LandingSound.IsNull() ? TSoftObjectPtr<USoundBase>(FSoftObjectPath(
		FString::Printf(TEXT("/Game/Interface_And_Item_Sounds/Cues/%s.%s"), *Cue, *Cue))) : LandingSound;
	TArray<FSoftObjectPath> Paths = {ResolvedEffect.ToSoftObjectPath(), ResolvedLandingSound.ToSoftObjectPath()};
	if (!CollectedSound.IsNull()) Paths.Add(CollectedSound.ToSoftObjectPath());
	if (!SoundConcurrency.IsNull()) Paths.Add(SoundConcurrency.ToSoftObjectPath());
	AssetLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
		FStreamableDelegate::CreateWeakLambda(this, [this]()
		{
			if (!IsValid(GetOwner()) || !VisualMesh || GetOwner()->IsActorBeingDestroyed()) return;
			if (bLanded && PresentedDropId != CollectedDropId) Land(false);
		}));
}

void URPGLootPresentationComponent::OnRep_Flight()
{
	if (HasBegunPlay()) RefreshDrop();
}

void URPGLootPresentationComponent::ResetPresentation()
{
	if (Beam) { Beam->DestroyComponent(); Beam = nullptr; }
	bLanded = false;
	SetComponentTickEnabled(false);
	if (VisualMesh)
	{
		VisualMesh->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		VisualMesh->SetVisibility(bSourceWasVisible);
	}
}

void URPGLootPresentationComponent::RefreshDrop()
{
	if (!VisualMesh || GetNetMode() == NM_DedicatedServer || !bEnabled) return;
	if (Flight.DropId != PresentedDropId)
	{
		ResetPresentation();
		PresentedDropId = Flight.DropId;
	}
	if (Flight.Duration > 0 && SourceMesh) SourceMesh->SetSimulatePhysics(false);
	if (PresentedDropId.IsValid() && PresentedDropId == CollectedDropId) return;
	UpdatePresentation();
}

void URPGLootPresentationComponent::UpdatePresentation()
{
	if (!VisualMesh || !GetOwner()) return;
	const double Age = ServerTime() - Flight.StartedAt;
	if (Flight.Duration > 0 && Age < Flight.Duration)
	{
		const float Alpha = FMath::Clamp(static_cast<float>(Age / Flight.Duration), 0.f, 1.f);
		const FVector Position = EvaluateArc(Flight.Origin, GetOwner()->GetActorLocation(), Alpha, Flight.Height);
		VisualMesh->SetWorldLocation(Position);
		const float Turn = ((Flight.Seed & 1) ? 1.f : -1.f) * 360.f * (1.f - Alpha);
		VisualMesh->SetRelativeRotation(FRotator(0.f, Turn, 25.f * FMath::Sin(PI * Alpha)));
		SetComponentTickEnabled(true);
		return;
	}
	if (!bLanded)
		Land(Flight.Duration > 0 && Age <= Flight.Duration + RPGLootPresentation::SoundGrace);
	const float SinceLanding = Flight.Duration > 0 ? static_cast<float>(Age - Flight.Duration) : 1.f;
	VisualMesh->SetWorldLocation(GetOwner()->GetActorLocation() + FVector::UpVector * EvaluateSettle(SinceLanding));
	VisualMesh->SetRelativeRotation(FRotator::ZeroRotator);
	SetComponentTickEnabled(SinceLanding < RPGLootPresentation::SettleDuration);
}

void URPGLootPresentationComponent::Land(const bool bPlaySound)
{
	bLanded = true;
	if (ResolvedEffect.IsValid() && !Beam && GetOwner() && PresentedDropId != CollectedDropId)
	{
		Beam = NewObject<UNiagaraComponent>(GetOwner(), TEXT("LootGroundBeam"));
		Beam->SetupAttachment(GetOwner()->GetRootComponent());
		// Ground effect stays upright and independent of scaled weapon/potion meshes.
		Beam->SetAbsolute(false, true, true);
		Beam->SetWorldRotation(FRotator::ZeroRotator);
		Beam->SetWorldScale3D(FVector(FMath::Clamp(EffectScale, 0.1f, 3.f)));
		Beam->SetAsset(ResolvedEffect.Get());
		Beam->SetAutoActivate(false);
		Beam->RegisterComponent();
		Beam->SetVisibility(bAvailable);
		Beam->Activate(true);
	}
	if (bPlaySound && bAvailable) PlayCue(ResolvedLandingSound.Get(), SoundVolume);
}

void URPGLootPresentationComponent::SetPickupAvailable(const bool bInAvailable)
{
	bAvailable = bInAvailable;
	if (Beam) Beam->SetVisibility(bAvailable && PresentedDropId != CollectedDropId);
}

void URPGLootPresentationComponent::SetFocused(const bool bFocused)
{
	if (VisualMesh) VisualMesh->SetRenderCustomDepth(bFocused);
}

void URPGLootPresentationComponent::PlayCue(USoundBase* Sound, const float Volume)
{
	if (!Sound || !GetOwner() || GetNetMode() == NM_DedicatedServer) return;
	if (!SpatialAttenuation)
	{
		SpatialAttenuation = NewObject<USoundAttenuation>(this);
		SpatialAttenuation->Attenuation.bAttenuate = true;
		SpatialAttenuation->Attenuation.bSpatialize = true;
		SpatialAttenuation->Attenuation.AttenuationShapeExtents = FVector(120.f);
		SpatialAttenuation->Attenuation.FalloffDistance = 1500.f;
	}
	UGameplayStatics::PlaySoundAtLocation(this, Sound, GetOwner()->GetActorLocation(),
		FMath::Clamp(Volume, 0.f, 1.f), 1.f, 0.f, SpatialAttenuation, SoundConcurrency.Get());
}

void URPGLootPresentationComponent::NotifyCollected()
{
	if (GetOwner() && GetOwner()->HasAuthority() && Flight.DropId.IsValid()) MulticastCollected(Flight.DropId);
}

void URPGLootPresentationComponent::MulticastCollected_Implementation(const FGuid DropId)
{
	if (!DropId.IsValid() || DropId != Flight.DropId || CollectedDropId == DropId) return;
	CollectedDropId = DropId;
	SetComponentTickEnabled(false);
	if (Beam) { Beam->DestroyComponent(); Beam = nullptr; }
	if (VisualMesh) VisualMesh->SetVisibility(false);
	PlayCue(CollectedSound.Get(), SoundVolume * 0.7f);
}

void URPGLootPresentationComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (!IsValid(GetOwner()) || GetOwner()->IsHidden()) { SetComponentTickEnabled(false); return; }
	UpdatePresentation();
}

void URPGLootPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (AssetLoad) { AssetLoad->CancelHandle(); AssetLoad.Reset(); }
	ResetPresentation();
	if (VisualMesh) { VisualMesh->DestroyComponent(); VisualMesh = nullptr; }
	if (SourceMesh) SourceMesh->SetVisibility(bSourceWasVisible, false);
	Super::EndPlay(Reason);
}
