// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/RPGNonPlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Component/Combat/NPCCombatComponent.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Combat/RPGCombatControlTypes.h"
#include "Component/Combat/RPGCombatContributionComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "DataAsset/StartUpData/DataAsset_NPCStartUpData.h"
#include "Component/UI/NPCUIComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "UI/RPGWidgetBase.h"
#include "FunctionLibrary/RPGCombatFunctionLibrary.h"
#include "GameMode/RPGGameModeBase.h"
#include "Attribute/RPGAttributeSet.h"
#include "RPGGameplayTags.h"
#include "AIController.h"
#include "Engine/World.h"

#include "RPGDebugHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogRPGNPCStartup, Log, All);

ARPGNonPlayerCharacter::ARPGNonPlayerCharacter()
{
	bReplicateUsingRegisteredSubObjectList = true;
	RPGAbilitySystemComponent->SetIsReplicated(true);
	RPGAbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	bUseControllerRotationYaw = false;

	GetCharacterMovement()->bUseControllerDesiredRotation = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 180.f, 0.f);
	GetCharacterMovement()->MaxWalkSpeed = 300.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 1000.f;

	NPCCombatComponent = CreateDefaultSubobject<UNPCCombatComponent>(TEXT("NPCCombatComponent"));
	CombatContributionComponent = CreateDefaultSubobject<URPGCombatContributionComponent>(TEXT("CombatContributionComponent"));

	NPCUIComponent = CreateDefaultSubobject<UNPCUIComponent>(TEXT("NPCUIComponent"));

	NPCHealthWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("NPCHealthWidgetComponent"));
	NPCHealthWidgetComponent->SetupAttachment(GetMesh());

	LeftHandCollisionBox = CreateDefaultSubobject<UBoxComponent>("LeftHandCollisionBox");
	LeftHandCollisionBox->SetupAttachment(GetMesh());
	LeftHandCollisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LeftHandCollisionBox->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::OnBodyCollisionBoxBeginOverlap);

	RightHandCollisionBox = CreateDefaultSubobject<UBoxComponent>("RightHandCollisionBox");
	RightHandCollisionBox->SetupAttachment(GetMesh());
	RightHandCollisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RightHandCollisionBox->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::OnBodyCollisionBoxBeginOverlap);

	BodyCollisionBox = CreateDefaultSubobject<UBoxComponent>("BodyCollisionBox");
	BodyCollisionBox->SetupAttachment(GetMesh());
	BodyCollisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyCollisionBox->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::OnBodyCollisionBoxBeginOverlap);
}

void ARPGNonPlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARPGNonPlayerCharacter, EncounterSummonTeam);
}

FGenericTeamId ARPGNonPlayerCharacter::GetGenericTeamId() const
{
	if (EncounterSummonTeam != FGenericTeamId::NoTeam.GetId()) { return FGenericTeamId(EncounterSummonTeam); }
	// Preserve the existing TeamManager lookup order for ordinary NPCs. AI
	// controllers are server-only; do not guess an enemy team on the client.
	const UObject* Candidates[] = { GetInstigator(), GetOwner(), GetController(), GetPlayerState() };
	for (const UObject* Candidate : Candidates)
	{
		if (Candidate == this) { continue; }
		if (const auto* Agent = Cast<IGenericTeamAgentInterface>(Candidate)) { return Agent->GetGenericTeamId(); }
	}
	return FGenericTeamId::NoTeam;
}

void ARPGNonPlayerCharacter::ConfigureEncounterSummon(uint8 TeamId)
{
	if (!HasAuthority() || TeamId == FGenericTeamId::NoTeam.GetId()) { return; }
	SuppressDeathItemDrops();
	EncounterSummonTeam = TeamId;
}

void ARPGNonPlayerCharacter::SpawnDefaultController()
{
	if (EncounterSummonTeam == FGenericTeamId::NoTeam.GetId()) { Super::SpawnDefaultController(); return; }
	if (!HasAuthority() || GetController() || bNPCStartUpEndingPlay || IsActorBeingDestroyed() ||
		EncounterSummonController.IsValid() || !AIControllerClass || !GetWorld() ||
		!AIControllerClass->IsChildOf(AAIController::StaticClass()) ||
		AIControllerClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) { return; }
	FActorSpawnParameters Parameters;
	Parameters.Instigator = GetInstigator();
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.OverrideLevel = GetLevel();
	Parameters.ObjectFlags |= RF_Transient;
	AAIController* AI = GetWorld()->SpawnActor<AAIController>(AIControllerClass, GetActorLocation(), GetActorRotation(), Parameters);
	if (!IsValid(AI)) { return; }
	// A spawn callback can reset the source before the registry has seen this controller.
	if (bNPCStartUpEndingPlay || IsActorBeingDestroyed()) { AI->Destroy(); return; }
	EncounterSummonController = AI;
	AI->SetGenericTeamId(FGenericTeamId(EncounterSummonTeam));
	AI->Possess(this);
}

UPawnCombatComponent* ARPGNonPlayerCharacter::GetPawnCombatComponent() const
{
	return NPCCombatComponent;
}

UPawnUIComponent* ARPGNonPlayerCharacter::GetPawnUIComponent() const
{
	return NPCUIComponent;
}

UNPCUIComponent* ARPGNonPlayerCharacter::GetNPCUIComponent() const
{
	return NPCUIComponent;
}

float ARPGNonPlayerCharacter::PlayAnimMontage(UAnimMontage* AnimMontage, float InPlayRate, FName StartSectionName)
{
	const auto* Control = GetCombatControlComponent();
	if (!HasAuthority() && IsActorBeginningPlay() && Control
		&& (Control->GetAirborneState().IsActive() || Control->GetKnockbackState().bActive
			|| Control->GetStunPresentationState().bActive || Control->GetStaggerState().bActive
			|| RPGCombatControl::IsActionBlocked(this)))
		return 0.f;
	return Super::PlayAnimMontage(AnimMontage, InPlayRate, StartSectionName);
}

void ARPGNonPlayerCharacter::BeginPlay()
{
	// AI Pawns have no replicated PlayerState callback. Simulated NPCs still
	// need actor info so GAS can consume the server's replicated montage.
	if (RPGAbilitySystemComponent && RPGAbilitySystemComponent->GetAvatarActor() != this)
	{
		RPGAbilitySystemComponent->InitAbilityActorInfo(this, this);
	}
	Super::BeginPlay();

	if (URPGWidgetBase* HealthWidget=Cast<URPGWidgetBase>(NPCHealthWidgetComponent->GetUserWidgetObject()))
	{
		HealthWidget->InitNPCCreatedWidget(this);
	}
}

void ARPGNonPlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	InitNPCStartUpData();
}

#if WITH_EDITOR
void ARPGNonPlayerCharacter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetMemberPropertyName() ==
		GET_MEMBER_NAME_CHECKED(ThisClass, LeftHandCollisionBoxAttachBoneName))
	{
		LeftHandCollisionBox->AttachToComponent(GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale, LeftHandCollisionBoxAttachBoneName);
	}

	if (PropertyChangedEvent.GetMemberPropertyName() ==
		GET_MEMBER_NAME_CHECKED(ThisClass, RightHandCollisionBoxAttachBoneName))
	{
		RightHandCollisionBox->AttachToComponent(GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale, RightHandCollisionBoxAttachBoneName);
	}
}
#endif

void ARPGNonPlayerCharacter::OnBodyCollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, 
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (APawn* HitPawn = Cast<APawn>(OtherActor))
	{
		if (URPGCombatFunctionLibrary::IsTargetPawnHostile(this, HitPawn))
		{
			NPCCombatComponent->OnHitTargetActor(HitPawn);
		}
	}
}

void ARPGNonPlayerCharacter::InitNPCStartUpData()
{
	if (!HasAuthority() || bNPCStartUpEndingPlay || bNPCStartUpGrantStarted || bNPCStartUpLoading ||
		CharacterStartUpData.IsNull() || !IsValid(GetController()))
	{
		return;
	}

	int32 AbilityApplyLevel = 1;

	if (ARPGGameModeBase* BaseGameMode = GetWorld()->GetAuthGameMode<ARPGGameModeBase>())
	{
		switch (BaseGameMode->GetCurrentGameDifficulty())
		{
		case ERPGGameDifficulty::Easy:
			AbilityApplyLevel = 1;
			break;

		case ERPGGameDifficulty::Normal:
			AbilityApplyLevel = 2;
			break;

		case ERPGGameDifficulty::Hard:
			AbilityApplyLevel = 3;
			break;

		case ERPGGameDifficulty::Hell:
			AbilityApplyLevel = 4;
			break;

		default:
			break;
		}
	}

	const uint32 RequestId = ++NPCStartUpRequestId;
	bNPCStartUpLoading = true;
	const FSoftObjectPath RequestedPath = CharacterStartUpData.ToSoftObjectPath();
	NPCStartUpLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(RequestedPath,
		FStreamableDelegate::CreateWeakLambda(this, [this, RequestId, RequestedPath, AbilityApplyLevel]()
		{
			CompleteNPCStartUpData(RequestId, Cast<UDataAsset_StartUpDataBase>(RequestedPath.ResolveObject()), AbilityApplyLevel);
		}));
	if (!NPCStartUpLoadHandle.IsValid())
	{
		bNPCStartUpLoading = false;
		UE_LOG(LogRPGNPCStartup, Warning, TEXT("Could not request startup data for %s"), *GetName());
	}
}

bool ARPGNonPlayerCharacter::IsNPCStartUpDataReady() const
{
	return HasAuthority() && !bNPCStartUpEndingPlay && bNPCStartUpReady &&
		IsValid(RPGAbilitySystemComponent) && RPGAbilitySystemComponent->GetAvatarActor() == this;
}

bool ARPGNonPlayerCharacter::CanProcessDeathItemDrop() const
{
	return HasAuthority() && !bNPCStartUpEndingPlay && !IsActorBeingDestroyed() &&
		!bDeathItemDropStarted && !bDeathItemDropsSuppressed && IsValid(RPGAbilitySystemComponent) &&
		RPGAbilitySystemComponent->GetAvatarActor() == this && IsValid(RPGAttributeSet) &&
		FMath::IsFinite(RPGAttributeSet->GetCurrentHealth()) && RPGAttributeSet->GetCurrentHealth() == 0.f &&
		RPGAbilitySystemComponent->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death);
}

bool ARPGNonPlayerCharacter::TryBeginDeathItemDrop()
{
	if (!CanProcessDeathItemDrop()) return false;
	// Reserve before SpawnActor/Blueprint callbacks can re-enter the drop ability.
	bDeathItemDropStarted = true;
	return true;
}

void ARPGNonPlayerCharacter::SuppressDeathItemDrops()
{
	if (HasAuthority()) { bDeathItemDropsSuppressed = true; }
}

void ARPGNonPlayerCharacter::RetireEncounterSummon()
{
	if (!HasAuthority() || !bDeathItemDropsSuppressed) { return; }
	bNPCStartUpEndingPlay = true;
	bNPCStartUpReady = false;
	CancelNPCStartUpLoad();
}

void ARPGNonPlayerCharacter::CompleteNPCStartUpData(uint32 RequestId, UDataAsset_StartUpDataBase* LoadedData, int32 ApplyLevel)
{
	if (RequestId != NPCStartUpRequestId || !HasAuthority() || bNPCStartUpEndingPlay || bNPCStartUpGrantStarted)
	{
		return;
	}
	bNPCStartUpLoading = false;
	// Hold the load handle through the grant so the loaded asset cannot be released mid-call.
	TSharedPtr<FStreamableHandle> CompletedLoad = MoveTemp(NPCStartUpLoadHandle);
	if (!IsValid(LoadedData) || !IsValid(GetController()) || !IsValid(RPGAbilitySystemComponent) ||
		RPGAbilitySystemComponent->GetAvatarActor() != this)
	{
		UE_LOG(LogRPGNPCStartup, Warning, TEXT("Startup failed for %s: loaded data, controller and initialized ASC required"), *GetName());
		return;
	}
	// On-give abilities/effects may re-enter possession. Never grant twice on the same Pawn.
	bNPCStartUpGrantStarted = true;
	LoadedData->GiveToAbilitySystemComponent(RPGAbilitySystemComponent, ApplyLevel);
	if (!IsValid(this) || bNPCStartUpEndingPlay || RequestId != NPCStartUpRequestId || !HasAuthority() ||
		!IsValid(RPGAbilitySystemComponent) || RPGAbilitySystemComponent->GetAvatarActor() != this)
	{
		return;
	}
	bNPCStartUpReady = true;
	HandleNPCStartUpDataReady();
	if (IsValid(this) && !bNPCStartUpEndingPlay)
	{
		OnNPCStartUpDataReady.Broadcast();
	}
}

void ARPGNonPlayerCharacter::HandleNPCStartUpDataReady()
{
}

void ARPGNonPlayerCharacter::CancelNPCStartUpLoad()
{
	++NPCStartUpRequestId;
	bNPCStartUpLoading = false;
	if (NPCStartUpLoadHandle.IsValid())
	{
		NPCStartUpLoadHandle->CancelHandle();
		NPCStartUpLoadHandle.Reset();
	}
}

void ARPGNonPlayerCharacter::UnPossessed()
{
	if (bNPCStartUpLoading)
	{
		CancelNPCStartUpLoad();
	}
	Super::UnPossessed();
}

void ARPGNonPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bNPCStartUpEndingPlay = true;
	bNPCStartUpReady = false;
	CancelNPCStartUpLoad();
	Super::EndPlay(EndPlayReason);
}
