#include "AI/RPGBossTelegraphPresentationComponent.h"

#include "Components/DecalComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

URPGBossTelegraphPresentationComponent::URPGBossTelegraphPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 1.f / 30.f;
}

bool URPGBossTelegraphPresentationComponent::EvaluateProgress(const FRPGBossState& BossState,
	const FRPGBossTelegraphState& Telegraph, double ServerNow, float& OutProgress)
{
	OutProgress = 0.f;
	FRPGBossTelegraphFootprint Footprint;
	const double Duration = Telegraph.EndServerTime - Telegraph.StartServerTime;
	if (BossState.EncounterState != ERPGBossEncounterState::InCombat || !Telegraph.bActive || !Telegraph.bShowIndicator ||
		Telegraph.EncounterId <= 0 || Telegraph.EncounterId != BossState.EncounterId || Telegraph.Sequence <= 0 ||
		Telegraph.PhaseIndex != BossState.PhaseIndex ||
		!Telegraph.ActionTag.IsValid() || Telegraph.AimLocation.ContainsNaN() ||
		!FMath::IsFinite(ServerNow) || !FMath::IsFinite(Telegraph.StartServerTime) ||
		!FMath::IsFinite(Telegraph.EndServerTime) || !FMath::IsFinite(Duration) || Duration <= 0.0 ||
		Duration > 120.0 || ServerNow >= Telegraph.EndServerTime ||
		(Telegraph.Geometry.bEnabled && !FRPGBossTelegraphFootprint::Build(Telegraph.Geometry, Footprint)))
	{
		return false;
	}
	OutProgress = static_cast<float>(FMath::Clamp((ServerNow - Telegraph.StartServerTime) / Duration, 0.0, 1.0));
	return true;
}

void URPGBossTelegraphPresentationComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer) { return; }
	Boss = GetOwner()->FindComponentByClass<URPGBossAIComponent>();
	if (!Boss) { return; }
	Boss->OnTelegraphChanged.AddUniqueDynamic(this, &ThisClass::OnTelegraphChanged);
	Boss->OnBossStateChanged.AddUniqueDynamic(this, &ThisClass::OnBossStateChanged);
	TArray<FSoftObjectPath> Paths;
	for (const FRPGBossTelegraphStyle& Style : Styles)
	{
		if (!Style.Material.IsNull()) { Paths.AddUnique(Style.Material.ToSoftObjectPath()); }
	}
	if (!Paths.IsEmpty())
	{
		// Completion reads current state, never a captured old attack. Keep the
		// handle alive so material assets remain resident for subsequent swings.
		MaterialLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
			FStreamableDelegate::CreateUObject(this, &ThisClass::RefreshPresentation));
	}
	RefreshPresentation();
}

void URPGBossTelegraphPresentationComponent::RefreshPresentation()
{
	if (bEndingPlay || GetNetMode() == NM_DedicatedServer || !IsValid(Boss) || !GetWorld())
	{
		HideIndicator();
		SetComponentTickEnabled(false);
		return;
	}
	const FRPGBossTelegraphState Telegraph = Boss->GetTelegraphState();
	const FRPGBossState BossState = Boss->GetBossState();
	// State and telegraph are separate properties and may arrive in either order.
	SetComponentTickEnabled(Telegraph.bActive && Telegraph.bShowIndicator);
	if (!Telegraph.bShowIndicator) { HideIndicator(); return; }
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (GetNetMode() == NM_Client && !GameState) { HideIndicator(); return; }
	const double ServerNow = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	float NewProgress = 0.f;
	FRPGBossTelegraphFootprint Footprint;
	const bool bShaped = Telegraph.Geometry.bEnabled;
	if (bShaped && !FRPGBossTelegraphFootprint::Build(Telegraph.Geometry, Footprint))
	{
		HideIndicator(); return;
	}
	const FRPGBossTelegraphStyle* Style = nullptr;
	for (const FRPGBossTelegraphStyle& Candidate : Styles)
	{
		if (Candidate.ActionTag == Telegraph.ActionTag)
		{
			// Ambiguous authoring must not silently render the wrong attack radius.
			if (Style) { HideIndicator(); return; }
			Style = &Candidate;
		}
	}
	if (!EvaluateProgress(BossState, Telegraph, ServerNow, NewProgress) || !Style ||
		(!bShaped && (!FMath::IsFinite(Style->Radius) || Style->Radius <= 0.f || Style->Radius > 2500.f)) ||
		!FMath::IsFinite(Style->Color.R) || !FMath::IsFinite(Style->Color.G) ||
		!FMath::IsFinite(Style->Color.B) || !FMath::IsFinite(Style->Color.A))
	{
		HideIndicator(); return;
	}
	UMaterialInterface* Material = Style->Material.Get();
	if (!Material || Material->GetMaterial()->MaterialDomain != MD_DeferredDecal) { HideIndicator(); return; }
	float ShapeSupport = 0.f;
	if (bShaped && !Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("TelegraphUseShape")), ShapeSupport))
	{
		HideIndicator(); return; // An old/custom circular material must not misrepresent a shaped attack.
	}
	if (!Indicator)
	{
		Indicator = NewObject<UDecalComponent>(GetOwner(), TEXT("BossTelegraphIndicator"));
		GetOwner()->AddInstanceComponent(Indicator);
		Indicator->SetVisibility(false);
		Indicator->FadeScreenSize = 0.f;
		Indicator->RegisterComponent();
	}
	if (DisplayedEncounter != Telegraph.EncounterId || DisplayedSequence != Telegraph.Sequence || !DynamicMaterial)
	{
		DynamicMaterial = UMaterialInstanceDynamic::Create(Material, this);
		Indicator->SetDecalMaterial(DynamicMaterial);
		Indicator->DecalSize = bShaped ? Footprint.DecalSize : FVector(100.f, Style->Radius, Style->Radius);
		FVector Location = bShaped ? Footprint.Center : Telegraph.AimLocation;
		FHitResult Ground;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(BossTelegraphGround), false, GetOwner());
		if (GetWorld()->LineTraceSingleByObjectType(Ground, Location + FVector(0.f, 0.f, 100.f),
			Location - FVector(0.f, 0.f, 500.f), FCollisionObjectQueryParams(ECC_WorldStatic), Query))
		{
			Location = Ground.ImpactPoint;
		}
		// Deliberately unattached: the locked aim must not follow boss movement.
		Indicator->SetWorldLocationAndRotation(Location, bShaped ? Footprint.DecalRotation : FRotator(-90.f, 0.f, 0.f));
		DynamicMaterial->SetVectorParameterValue(TEXT("TelegraphColor"), Style->Color);
		if (bShaped) Footprint.ApplyMaterialParameters(*DynamicMaterial);
		else DynamicMaterial->SetScalarParameterValue(TEXT("TelegraphUseShape"), 0.f);
		DisplayedEncounter = Telegraph.EncounterId;
		DisplayedSequence = Telegraph.Sequence;
	}
	Progress = NewProgress;
	DynamicMaterial->SetScalarParameterValue(TEXT("TelegraphProgress"), Progress);
	Indicator->SetVisibility(true);
}

bool URPGBossTelegraphPresentationComponent::IsIndicatorVisible() const
{
	return IsValid(Indicator) && Indicator->IsVisible();
}

void URPGBossTelegraphPresentationComponent::HideIndicator()
{
	if (Indicator) { Indicator->SetVisibility(false); }
	Progress = 0.f;
	DisplayedEncounter = DisplayedSequence = 0;
}

void URPGBossTelegraphPresentationComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshPresentation();
}

void URPGBossTelegraphPresentationComponent::OnTelegraphChanged(const FRPGBossTelegraphState& Previous,
	const FRPGBossTelegraphState& Current)
{
	RefreshPresentation();
}

void URPGBossTelegraphPresentationComponent::OnBossStateChanged(const FRPGBossState& Previous,
	const FRPGBossState& Current)
{
	RefreshPresentation();
}

void URPGBossTelegraphPresentationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	SetComponentTickEnabled(false);
	if (Boss)
	{
		Boss->OnTelegraphChanged.RemoveDynamic(this, &ThisClass::OnTelegraphChanged);
		Boss->OnBossStateChanged.RemoveDynamic(this, &ThisClass::OnBossStateChanged);
	}
	if (MaterialLoad) { MaterialLoad->CancelHandle(); MaterialLoad.Reset(); }
	if (Indicator) { Indicator->DestroyComponent(); Indicator = nullptr; }
	DynamicMaterial = nullptr;
	Boss = nullptr;
	Super::EndPlay(EndPlayReason);
}
