#include "Tests/RPGBossStartupTestFixture.h"

#include "Tests/RPGBossTestFixture.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "AI/RPGBossTelegraphPresentationComponent.h"
#include "AIController.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Controller/RPGAIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "Components/DecalComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void URPGBossStartupTestData::GiveToAbilitySystemComponent(URPGAbilitySystemComponent* ASC, int32 ApplyLevel)
{
	++GrantCount;
	LastApplyLevel = ApplyLevel;
	ARPGBossStartupTestCharacter* Boss = CastChecked<ARPGBossStartupTestCharacter>(ASC->GetAvatarActor());
	bEarlyStartDenied = !Boss->IsNPCStartUpDataReady() && !Boss->BossAIComponent->StartEncounter();
	if (bRepossessDuringGrant)
	{
		AController* Controller = Boss->GetController();
		Controller->UnPossess();
		Controller->Possess(Boss);
	}
	Super::GiveToAbilitySystemComponent(ASC, ApplyLevel);
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 100.0f);
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100.0f);
	// Fixed-damage network assertions must not depend on a random critical hit.
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCriticalChanceAttribute(), 0.f);
}

ARPGBossStartupTestCharacter::ARPGBossStartupTestCharacter()
{
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = ARPGAIController::StaticClass();
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	GetCharacterMovement()->DefaultLandMovementMode = MOVE_None;
	SetActorEnableCollision(false);
}

void ARPGBossStartupTestCharacter::BeginPlay()
{
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossNetworkTest")))
	{
		FRPGBossTelegraphStyle Style;
		Style.ActionTag = FGameplayTag::RequestGameplayTag(TEXT("Tests.RaidBoss.Network.Telegraph"));
		Style.Radius = 75.f;
		Style.Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
			TEXT("/Game/Blueprints/Character/NPC/SwordMan/Presentation/M_RaidSwordManTelegraph.M_RaidSwordManTelegraph")));
		BossTelegraphPresentation->Styles = {Style};
	}
#endif
	Super::BeginPlay();
	if (HasAuthority())
	{
		TestData = NewObject<URPGBossStartupTestData>(this);
		CharacterStartUpData = TestData.Get();
		BossAIComponent->Definition = ARPGBossTestPawn::MakeDefinition(this);
#if !UE_BUILD_SHIPPING
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossNetworkTest")))
		{
			bStartEncounterOnReady = true;
			SpawnDefaultController();
			// Network threat scores are injected by the scenario, independent of sight/level geometry.
			CastChecked<AAIController>(GetController())->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
		}
#endif
	}
}

void ARPGBossStartupTestCharacter::HandleNPCStartUpDataReady()
{
	++ReadyCount;
	Super::HandleNPCStartUpDataReady();
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossNetworkTest")) && TestData->GrantCount == 1 &&
		TestData->bEarlyStartDenied && IsNPCStartUpDataReady() &&
		BossAIComponent->GetBossState().EncounterState == ERPGBossEncounterState::InCombat)
	{
		UE_LOG(LogTemp, Display, TEXT("RAID_STARTUP_READY Applied=1 EarlyStartDenied=1"));
		if (GetNetMode() == NM_DedicatedServer && !BossTelegraphPresentation->GetIndicator())
		{
			UE_LOG(LogTemp, Display, TEXT("RAID_PRESENTATION_DEDICATED Skipped=1"));
		}
	}
#endif
}

void ARPGBossStartupTestCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	TickNetworkDash();
	TickNetworkSummon();
	if (!HasAuthority() && !bReportedClientState && FParse::Param(FCommandLine::Get(), TEXT("RPGBossNetworkTest")))
	{
		const FRPGBossState State = BossAIComponent->GetBossState();
		if (State.EncounterState == ERPGBossEncounterState::InCombat && State.PhaseIndex == 0 && State.PhaseTag.IsValid() &&
			GetRPGAttributeSet()->GetCurrentHealth() == 100.0f && GetRPGAttributeSet()->GetMaxHealth() == 100.0f &&
			GetAbilitySystemComponent()->GetAvatarActor() == this && !IsNPCStartUpDataReady() &&
			!BossAIComponent->StartEncounter() && !BossThreatComponent->GetCurrentTarget() &&
			!BossThreatComponent->AddThreat(this, 1000.f, State.EncounterId) && !BossThreatComponent->ClearThreat())
		{
			bReportedClientState = true;
			UE_LOG(LogTemp, Display, TEXT("RAID_STARTUP_REPLICATED Health=100 AvatarReady=1 AuthorityDenied=1"));
		}
	}
	if (!HasAuthority() && FParse::Param(FCommandLine::Get(), TEXT("RPGBossNetworkTest")))
	{
		const FRPGBossState State = BossAIComponent->GetBossState();
		const FRPGBossTelegraphState Telegraph = BossAIComponent->GetTelegraphState();
		if (!bSawIndicator && BossTelegraphPresentation->IsIndicatorVisible() &&
			BossTelegraphPresentation->GetProgress() > 0.f && Telegraph.bActive)
		{
			bSawIndicator = true;
			UE_LOG(LogTemp, Display, TEXT("RAID_PRESENTATION_VISIBLE Visible=1 ProgressFromServer=1"));
		}
		if (bSawIndicator && !bSawIndicatorCleared && !Telegraph.bActive &&
			!BossTelegraphPresentation->IsIndicatorVisible())
		{
			bSawIndicatorCleared = true;
			UE_LOG(LogTemp, Display, TEXT("RAID_PRESENTATION_CLEARED Visible=0"));
		}
		if (!bSawTelegraph && Telegraph.bActive && State.EncounterState == ERPGBossEncounterState::InCombat &&
			Telegraph.EncounterId == State.EncounterId && Telegraph.ActionTag.IsValid() &&
			Telegraph.Sequence > 0 && Telegraph.EndServerTime > Telegraph.StartServerTime)
		{
			if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossTelegraphShapeNetwork")))
			{
				const auto& Geometry = Telegraph.Geometry;
				if (!Geometry.bEnabled || Geometry.Shape.Type != ERPGHitQueryShape::RingSector ||
					Geometry.Shape.Radius != 300.f || Geometry.Shape.InnerRadius != 150.f || Geometry.Shape.AngleDegrees != 270.f ||
					!FMath::IsNearlyEqual(Geometry.QueryTransform.Rotator().Yaw, 90.f))
				{
					UE_LOG(LogTemp, Error, TEXT("RAID_TELEGRAPH_SHAPE_FAIL Snapshot")); return;
				}
				if (!BossTelegraphPresentation->IsIndicatorVisible()) return;
				const UMaterialInterface* Material = BossTelegraphPresentation->GetIndicator()->GetDecalMaterial();
				float Inner = 0.f, Outer = 0.f, ShapeType = 0.f;
				if (!Material || !Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("TelegraphInnerRadius")), Inner) ||
					!Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("TelegraphRadius")), Outer) ||
					!Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("TelegraphShape")), ShapeType) ||
					Inner != 150.f || Outer != 300.f || ShapeType != static_cast<float>(ERPGHitQueryShape::RingSector) ||
					BossAIComponent->BeginTelegraphWithShape(Telegraph.ActionTag, Geometry.QueryTransform, Geometry.Shape, 1.f) != 0)
				{
					UE_LOG(LogTemp, Error, TEXT("RAID_TELEGRAPH_SHAPE_FAIL MaterialOrAuthority")); return;
				}
				UE_LOG(LogTemp, Display, TEXT("RAID_TELEGRAPH_SHAPE_REPLICATED RingSector=1 Inner=150 Outer=300 Angle=270 Yaw=90 Material=1 AuthorityDenied=1"));
			}
			bSawTelegraph = true;
			UE_LOG(LogTemp, Display, TEXT("RAID_BOSS_TELEGRAPH_REPLICATED Active=1"));
		}
		if (bSawTelegraph && !bSawTelegraphCleared && !Telegraph.bActive)
		{
			bSawTelegraphCleared = true;
			UE_LOG(LogTemp, Display, TEXT("RAID_BOSS_TELEGRAPH_CLEARED Active=0"));
		}
		if (!bSawTimedStrikeTelegraph && Telegraph.bActive && Telegraph.Sequence >= 3 &&
			Telegraph.EncounterId == State.EncounterId && Telegraph.ActionTag.IsValid())
		{
			bSawTimedStrikeTelegraph = true;
			UE_LOG(LogTemp, Display, TEXT("RAID_BOSS_TIMED_STRIKE_TELEGRAPH_REPLICATED Active=1"));
		}
	}
#endif
}
