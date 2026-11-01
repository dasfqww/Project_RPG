#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Combat/RPGCombatImmunityTypes.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Tests/RPGMontageImmunityTestSupport.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "DataAsset/StartUpData/DataAsset_StartUpDataBase.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "FunctionLibrary/RPGSkillConfigBlueprintLibrary.h"
#include "GameplayEffect/GE_CoolDown.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PlayerClass/RPGPlayerClassComponent.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "RPGAbilityTags.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGSkillDefinition_Charge.h"
#include "Skill/RPGSkillCatalogSubsystem.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "Skill/RPGSkillExecutionPolicy.h"
#include "Skill/RPGSkillExecutionTypes.h"
#include "Skill/RPGSkillTargetingPolicy.h"
#include "Skill/RPGSkillTargetingTypes.h"
#include "UI/MVVM/RPGSkillViewModel.h"
#include "UI/Skill/RPGSkillLevelAdjuster.h"
#include "UI/Skill/RPGTripodBoard.h"
#include "UI/Skill/RPGTripodButton.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"
#include "CommonTextBlock.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWidget.h"

#if WITH_EDITOR
namespace RPGSkillIntegrationTests
{
// An isolated standalone world with a real ASC, ability tasks and montage.
// Input uses the equipped-slot/ASC boundary: this does not simulate Enhanced
// Input, the authored player Blueprint, damage notifies or network prediction.
struct FFixture
{
	UWorld* World = nullptr;
	ARPGPlayer* Player = nullptr;
	URPGAbilitySystemComponent* ASC = nullptr;
	URPGGameplayAbility_SkillContainer* Ability = nullptr;
	UAnimInstance* AnimInstance = nullptr;
	FGameplayAbilitySpecHandle Handle;
	TStrongObjectPtr<UGameInstance> GameInstance;
	TStrongObjectPtr<URPGSkillDefinition> PreviousDefaultDefinition;
	URPGGameplayAbility_SkillContainer* PatchedAbilityDefault = nullptr;
	FObjectPropertyBase* DefinitionProperty = nullptr;
	uint64 OriginalFrameCounter = GFrameCounter;

	~FFixture()
	{
		if (Player)
			if (auto* ClassOwner = Player->FindComponentByClass<URPGPlayerClassComponent>())
				ClassOwner->ReleaseClassAbilities();
		if (ASC)
		{
			ASC->CancelAllAbilities();
			ASC->ClearAllAbilities();
		}
		if (PatchedAbilityDefault && DefinitionProperty)
			DefinitionProperty->SetObjectPropertyValue_InContainer(PatchedAbilityDefault, PreviousDefaultDefinition.Get());
		if (World)
		{
			if (GameInstance.IsValid()) GameInstance->Shutdown();
			RPGTestWorld::Destroy(World);
		}
		GFrameCounter = OriginalFrameCounter;
	}

	bool Initialize(FAutomationTestBase& Test, URPGSkillDefinition* Definition,
		const FRPGSkillSaveData& SaveData = FRPGSkillSaveData())
	{
		const FName WorldName = MakeUniqueObjectName(
			nullptr,
			UWorld::StaticClass(),
			NAME_None,
			EUniqueObjectNameOptions::GloballyUnique);
		World = UWorld::CreateWorld(
			EWorldType::Game,
			false,
			WorldName,
			GetTransientPackage());
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		// Class ownership and equipped slots are required even in a standalone world.
		ACharacter* Character = World->SpawnActor<ARPGPlayer>();
		Player = Cast<ARPGPlayer>(Character);
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		if (!Test.TestNotNull(TEXT("Fixture character"), Character) ||
			!Test.TestNotNull(TEXT("Fixture controller"), Controller))
		{
			return false;
		}
		Controller->SetAsLocalPlayerController();
		if (Player)
		{
			// Native players require startup data on possession. An empty transient
			// asset keeps this fixture independent of existing D1 skill grants.
			FSoftObjectProperty* StartupProperty = FindFProperty<FSoftObjectProperty>(
				ARPGBaseCharacter::StaticClass(), TEXT("CharacterStartUpData"));
			if (!Test.TestNotNull(TEXT("Native player startup property"), StartupProperty))
			{
				return false;
			}
			*StartupProperty->ContainerPtrToValuePtr<TSoftObjectPtr<UDataAsset_StartUpDataBase>>(Player) =
				NewObject<UDataAsset_StartUpDataBase>(Player);
		}
		Controller->Possess(Character);
		USkeletalMesh* Mesh = Definition->SkillMontage
			? Definition->SkillMontage->GetPreviewMesh(true) : nullptr;
		if (!Mesh)
		{
			UClass* PlayerClass = LoadClass<ACharacter>(nullptr,
				TEXT("/Game/Blueprints/Character/Player/BP_Player.BP_Player_C"));
			const ACharacter* PlayerCDO = PlayerClass
				? PlayerClass->GetDefaultObject<ACharacter>() : nullptr;
			Mesh = PlayerCDO && PlayerCDO->GetMesh()
				? PlayerCDO->GetMesh()->GetSkeletalMeshAsset() : nullptr;
		}
		if (!Test.TestNotNull(TEXT("Player mesh compatible with the authored montage"), Mesh))
		{
			return false;
		}
		Character->GetMesh()->SetSkeletalMesh(Mesh);
		Character->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
		Character->GetMesh()->VisibilityBasedAnimTickOption =
			EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		AnimInstance = Character->GetMesh()->GetAnimInstance();
		if (!Test.TestNotNull(TEXT("Real animation instance"), AnimInstance))
		{
			return false;
		}
		ASC = Player ? Player->GetRPGAbilitySystemComponent()
			: NewObject<URPGAbilitySystemComponent>(Character);
		if (!Player)
		{
			ASC->RegisterComponent();
		}
		URPGAttributeSet* Attributes = Player ? Player->GetRPGAttributeSet()
			: NewObject<URPGAttributeSet>(Character);
		Attributes->InitAttackSpeed(1.0f);
		ASC->AddAttributeSetSubobject(Attributes);
		ASC->InitAbilityActorInfo(Character, Character);
		DefinitionProperty = FindFProperty<FObjectPropertyBase>(
			URPGGameplayAbility_SkillContainer::StaticClass(), TEXT("SkillDefinition"));
		if (!Test.TestNotNull(TEXT("Definition fixture property"), DefinitionProperty)) return false;
		// The class binding validates the ability CDO's exact definition. This worker-local
		// override is restored on every fixture exit; no asset or runtime eligibility changes.
		PatchedAbilityDefault = GetMutableDefault<URPGGameplayAbility_SkillContainer>();
		PreviousDefaultDefinition.Reset(Cast<URPGSkillDefinition>(
			DefinitionProperty->GetObjectPropertyValue_InContainer(PatchedAbilityDefault)));
		DefinitionProperty->SetObjectPropertyValue_InContainer(PatchedAbilityDefault, Definition);
		auto* Class = NewObject<URPGPlayerClassDefinition>(Player);
		Class->ClassId = TEXT("SkillIntegrationFixture");
		FRPGPlayerClassSkill& Binding = Class->Skills.AddDefaulted_GetRef();
		Binding.Definition = Definition;
		Binding.AbilityClass = URPGGameplayAbility_SkillContainer::StaticClass();
		auto* Growth = Player->GetPlayerSkillComponent();
		auto* ClassOwner = Player->FindComponentByClass<URPGPlayerClassComponent>();
		auto* Slots = Player->FindComponentByClass<UQuickSlotComponent>();
		if (!Test.TestNotNull(TEXT("Class progression"), Growth)
			|| !Test.TestNotNull(TEXT("Class grant owner"), ClassOwner)
			|| !Test.TestNotNull(TEXT("Equipped skill slots"), Slots)) return false;
		FRPGPlayerSkillRestoreData Record;
		Record.ClassId = Class->GetPrimaryAssetId();
		Record.TotalSP = 1000;
		Record.bIdentityUnlocked = false;
		auto& Acquired = Record.AcquiredSkills.AddDefaulted_GetRef();
		Acquired.SkillTag = Definition->SkillTag;
		Acquired.SaveData = SaveData;
		const uint32 Restore = Growth->BeginClassSkillRestore(Record.ClassId);
		FText Error;
		if (!Test.TestTrue(TEXT("Restore the acquired fixture skill"),
			Growth->CompleteClassSkillRestore(Restore, Class, Record, Error))) return false;
		ClassOwner->NotifyAbilitySystemReady(*ASC, 1);
		if (!Test.TestTrue(FString::Printf(TEXT("Class fixture applied (state %d, ready %d): %s"),
			static_cast<int32>(ClassOwner->GetApplyState()), ClassOwner->IsAbilitySystemReady(),
			*ClassOwner->GetLastApplyError().ToString()),
			ClassOwner->GetApplyState() == ERPGPlayerClassApplyState::Applied)) return false;
		const auto* Spec = ASC->FindAbilitySpecFromClass(URPGGameplayAbility_SkillContainer::StaticClass());
		Handle = Spec ? Spec->Handle : FGameplayAbilitySpecHandle();
		Ability = Spec ? Cast<URPGGameplayAbility_SkillContainer>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Test.TestNotNull(TEXT("Class-owned skill instance"), Ability))
		{
			return false;
		}
		// Retain the transient definition injection used by this native-class fixture.
		// Native default initialization may have been cached before its CDO override.
		DefinitionProperty->SetObjectPropertyValue_InContainer(Ability, Definition);
		if (!Test.TestTrue(TEXT("Fixture spec retains class ownership and acquisition"),
			ClassOwner->CanUseAbilitySpec(Handle, Ability->GetClass()) && Growth->IsSkillAcquired(Definition->SkillTag))) return false;
		const bool bSlotsRestored = Slots->InitializeSkillSlotsFromRecord(Restore, {Definition->SkillTag}, Error);
		if (!Test.TestTrue(FString::Printf(TEXT("Initialize the equipped fixture skill: %s"), *Error.ToString()),
			bSlotsRestored)) return false;
		return true;
	}

	bool InstallCatalogDefinition(FAutomationTestBase& Test, URPGSkillDefinition* Definition)
	{
		// Attach after actor BeginPlay: no asynchronous catalog request can
		// replace this fixture's transient definition while commands run.
		GameInstance.Reset(NewObject<UGameInstance>(GEngine));
		World->SetGameInstance(GameInstance.Get());
		GameInstance->Init();
		URPGSkillCatalogSubsystem* Catalog = GameInstance->GetSubsystem<URPGSkillCatalogSubsystem>();
		FArrayProperty* Definitions = FindFProperty<FArrayProperty>(
			URPGSkillCatalogSubsystem::StaticClass(), TEXT("CachedDefinitions"));
		if (!Test.TestNotNull(TEXT("Isolated skill catalog"), Catalog)
			|| !Test.TestNotNull(TEXT("Catalog fixture property"), Definitions)) return false;
		*Definitions->ContainerPtrToValuePtr<TArray<TObjectPtr<URPGSkillDefinition>>>(Catalog) = {Definition};
		return true;
	}

	void Advance(const float Duration)
	{
		for (float Remaining = Duration; Remaining > KINDA_SMALL_NUMBER;)
		{
			const float Delta = FMath::Min(Remaining, 1.0f / 60.0f);
			++GFrameCounter; // TimerManager normally receives this from the engine loop.
			World->Tick(LEVELTICK_All, Delta);
			Remaining -= Delta;
		}
	}

	void Press() { Player->FindComponentByClass<UQuickSlotComponent>()->BeginUseSkillSlot(0); }
	void Release() { Player->FindComponentByClass<UQuickSlotComponent>()->EndUseSkillSlot(0); }
};

UBoxComponent* AddGround(UWorld& World)
{
	AActor* Ground = World.SpawnActor<AActor>();
	if (!Ground) return nullptr;
	UBoxComponent* Box = NewObject<UBoxComponent>(Ground);
	Ground->SetRootComponent(Box);
	Box->InitBoxExtent(FVector(3000.0f, 3000.0f, 50.0f));
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->RegisterComponent();
	Ground->SetActorLocation(FVector(0.0f, 0.0f, -50.0f));
	return Box;
}

struct FGroundTargetingHost final : IRPGSkillTargetingHost
{
	UWorld& World;
	AActor& Source;
	FRPGHitQueryFilter Filter;

	FGroundTargetingHost(UWorld& InWorld, AActor& InSource) : World(InWorld), Source(InSource) {}
	virtual UWorld* GetSkillTargetingWorld() const override { return &World; }
	virtual AActor* GetSkillSourceActor() const override { return &Source; }
	virtual AActor* GetSkillLockedTarget() const override { return nullptr; }
	virtual const FRPGHitQueryFilter& GetSkillTargetValidationFilter() const override { return Filter; }
	virtual bool GetSkillCameraAimRay(FVector& Origin, FVector& Direction) const override
	{
		// A back-view camera looking at the ground directly below the source.
		Origin = Source.GetActorLocation() + FVector(-100.0f, 0.0f, 100.0f);
		Direction = (FVector(Source.GetActorLocation().X, Source.GetActorLocation().Y, 0.0f) - Origin).GetSafeNormal();
		return true;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillGroundPointValidationIntegrationTest,
	"ProjectRPG.Skill.Integration.GroundPointServerConsistency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillGroundPointValidationIntegrationTest::RunTest(const FString& Parameters)
{
	using namespace RPGSkillIntegrationTests;
	URPGSkillDefinition* Definition = LoadObject<URPGSkillDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_ChargeSlash.DA_Skill_ChargeSlash"));
	if (!TestNotNull(TEXT("Ground fixture montage definition"), Definition)) return false;
	FFixture Fixture;
	if (!Fixture.Initialize(*this, Definition)) return false;
	AActor* Source = Fixture.ASC->GetAvatarActor();
	Source->SetActorLocation(FVector(0.0f, 0.0f, 100.0f));
	UBoxComponent* Ground = AddGround(*Fixture.World);
	if (!TestNotNull(TEXT("Query-only ground"), Ground)) return false;
	FGroundTargetingHost Host(*Fixture.World, *Source);
	FRPGSkillGroundPointTargetingConfig Config;
	Config.MaxRange = 1200.0f;
	FInstancedStruct ConfigStruct = FInstancedStruct::Make(Config);
	URPGSkillTargetingPolicy_GroundPoint* Policy = NewObject<URPGSkillTargetingPolicy_GroundPoint>(Source);
	Policy->Initialize(Host, ConfigStruct);
	FRPGSkillTargetResult Local, Validated;
	FText Error;
	if (!TestTrue(TEXT("Local ground point resolves"), Policy->ResolveTarget(Local))) return false;
	TestTrue(TEXT("Ground point is directly under the caster"),
		FVector::Dist2D(Local.TargetLocation, Source->GetActorLocation()) < KINDA_SMALL_NUMBER);
	if (!TestTrue(TEXT("The server accepts the same self-centered point"),
		Policy->ValidateReplicatedTarget(Local, Validated, Error))) return false;
	TestTrue(TEXT("Server and local fallback directions match"), Validated.AimDirection.Equals(Local.AimDirection));
	TestTrue(TEXT("Hit query remains centered on the validated ground point"),
		Validated.HitQueryTransform.GetLocation().Equals(Validated.TargetLocation));
	FRPGSkillTargetResult OutOfRange = Local;
	OutOfRange.TargetLocation.X += Config.MaxRange + Config.ServerRangeTolerance + 100.0f;
	TestFalse(TEXT("The fallback does not bypass range validation"),
		Policy->ValidateReplicatedTarget(OutOfRange, Validated, Error));
	Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TestFalse(TEXT("A missing floor invalidates local targeting"), Policy->ResolveTarget(Validated));
	TestFalse(TEXT("A stale ground point is also rejected by the server"),
		Policy->ValidateReplicatedTarget(Local, Validated, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkillImmunityIntegrationTest,
	"ProjectRPG.Combat.Immunity.SkillCommitAndFrozenActivation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSkillImmunityIntegrationTest::RunTest(const FString&)
{
	auto* Authored = LoadObject<URPGSkillDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_ChargeSlash.DA_Skill_ChargeSlash"));
	if (!TestNotNull(TEXT("Authored charge definition"), Authored)) return false;
	auto* Definition = DuplicateObject<URPGSkillDefinition>(Authored, GetTransientPackage());
	Definition->SkillVFX = nullptr;
	AddInfo(FString::Printf(TEXT("Fixture cooldown normalized: authored=%g seconds, test=1 second"), Definition->BaseCooldown));
	Definition->BaseCooldown = 1.0f;
	Definition->ActivationImmunities.Flags = 3;
	RPGSkillIntegrationTests::FFixture Fixture;
	if (!Fixture.Initialize(*this, Definition)) return false;
	Fixture.ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 100);
	Fixture.ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100);
	TestFalse(TEXT("Granting a skill does not grant protection"), RPGCombatImmunity::IsImmune(Fixture.Player, ERPGCombatEffectKind::HitReaction));
	Fixture.Press();
	if (!TestTrue(TEXT("Real equipped skill commits"), Fixture.Ability->IsActive())) return false;
	TestTrue(TEXT("Committed skill gets displacement protection"), RPGCombatImmunity::IsImmune(Fixture.Player, ERPGCombatEffectKind::Airborne));
	TestFalse(TEXT("Committed skill remains susceptible to stun"), RPGCombatImmunity::IsImmune(Fixture.Player, ERPGCombatEffectKind::Stun));
	Definition->ActivationImmunities.Flags = 0;
	TestEqual(TEXT("Activation retains frozen settings"), Fixture.Ability->GetActiveSkillSpec().ActivationImmunities.Flags, 3);
	Fixture.ASC->CancelAbilityHandle(Fixture.Handle); Fixture.Release();
	TestFalse(TEXT("Cancelling real skill removes its protection"), RPGCombatImmunity::IsImmune(Fixture.Player, ERPGCombatEffectKind::Airborne));
	Fixture.Press(); Fixture.Release();
	TestFalse(TEXT("Cooldown rejection cannot grant protection"), RPGCombatImmunity::IsImmune(Fixture.Player, ERPGCombatEffectKind::HitReaction));
	Fixture.Advance(1.1f); Fixture.Press();
	TestTrue(TEXT("Next activation is available"), Fixture.Ability->IsActive());
	TestFalse(TEXT("Next activation uses new settings"), RPGCombatImmunity::IsImmune(Fixture.Player, ERPGCombatEffectKind::Airborne));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkillMontageImmunityIntegrationTest,
 "ProjectRPG.Combat.MontageImmunity.SkillCommitAndCooldown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSkillMontageImmunityIntegrationTest::RunTest(const FString&)
{
 auto* Authored=LoadObject<URPGSkillDefinition>(nullptr,TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_ChargeSlash.DA_Skill_ChargeSlash"));
 if(!TestNotNull(TEXT("Authored skill"),Authored))return false;
 auto* Definition=DuplicateObject<URPGSkillDefinition>(Authored,GetTransientPackage());Definition->SkillVFX=nullptr;Definition->ActivationImmunities.Flags=0;
 Definition->SkillMontage=DuplicateObject<UAnimMontage>(Authored->SkillMontage,Definition);Definition->SkillMontage->Notifies.Reset();
 RPGMontageImmunityTests::AddWindow(*Definition->SkillMontage,0,.9f,3);RPGMontageImmunityTests::Refresh(*Definition->SkillMontage);
 RPGSkillIntegrationTests::FFixture Fixture;if(!Fixture.Initialize(*this,Definition))return false;
 Fixture.ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(),100);Fixture.ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),100);
 TestFalse(TEXT("Grant is not a committed execution"),Fixture.Ability->CanUseMontageImmunity());
 Fixture.ASC->PlayMontage(Fixture.Ability,Fixture.Ability->GetCurrentActivationInfo(),Definition->SkillMontage,1);
 TestFalse(TEXT("Inactive/uncommitted skill montage cannot grant a window"),RPGCombatImmunity::IsImmune(Fixture.Player,ERPGCombatEffectKind::HitReaction));
 Fixture.AnimInstance->Montage_Stop(0);
 Fixture.Press();if(!TestTrue(TEXT("Real equipped skill commits"),Fixture.Ability->IsActive()))return false;
 TestTrue(TEXT("Successful commit enables montage window"),Fixture.Ability->CanUseMontageImmunity());
 TestTrue(TEXT("Actual skill GAS montage grants window"),RPGCombatImmunity::IsImmune(Fixture.Player,ERPGCombatEffectKind::Knockback));
 Fixture.ASC->CancelAbilityHandle(Fixture.Handle);Fixture.Release();
 TestFalse(TEXT("Cancelled skill window gone"),RPGCombatImmunity::IsImmune(Fixture.Player,ERPGCombatEffectKind::Knockback));
 Fixture.Press();Fixture.Release();TestFalse(TEXT("Cooldown rejection cannot grant a montage window"),RPGCombatImmunity::IsImmune(Fixture.Player,ERPGCombatEffectKind::HitReaction));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillGroundReleaseCancellationIntegrationTest,
	"ProjectRPG.Skill.Integration.GroundReleaseFailureCancels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillGroundReleaseCancellationIntegrationTest::RunTest(const FString& Parameters)
{
	using namespace RPGSkillIntegrationTests;
	URPGSkillDefinition* Authored = LoadObject<URPGSkillDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_ChargeSlash.DA_Skill_ChargeSlash"));
	if (!TestNotNull(TEXT("Authored charge definition"), Authored)) return false;
	URPGSkillDefinition* Definition = DuplicateObject<URPGSkillDefinition>(Authored, GetTransientPackage());
	Definition->SkillVFX = nullptr;
	Definition->DefaultTargetingPolicyClass = URPGSkillTargetingPolicy_GroundPoint::StaticClass();
	Definition->DefaultTargetingConfig = FInstancedStruct::Make(FRPGSkillGroundPointTargetingConfig());
	FFixture Fixture;
	if (!Fixture.Initialize(*this, Definition)) return false;
	ACharacter* Character = CastChecked<ACharacter>(Fixture.ASC->GetAvatarActor());
	Character->SetActorLocation(FVector(0.0f, 0.0f, 100.0f));
	UBoxComponent* Ground = AddGround(*Fixture.World);
	if (!TestNotNull(TEXT("Charge ground"), Ground)) return false;
	Character->GetController()->SetControlRotation(FRotator(-45.0f, 0.0f, 0.0f));
	Fixture.Advance(0.05f);
	bool bObservedEnd = false, bCancelled = false;
	const FDelegateHandle EndHandle = Fixture.ASC->OnAbilityEnded.AddLambda([&](const FAbilityEndedData& End)
	{
		if (End.AbilitySpecHandle == Fixture.Handle)
		{
			bObservedEnd = true;
			bCancelled = End.bWasCancelled;
		}
	});
	ON_SCOPE_EXIT { Fixture.ASC->OnAbilityEnded.Remove(EndHandle); };
	Fixture.Press();
	if (!TestTrue(TEXT("Charge starts with valid ground"), Fixture.Ability->IsActive())) return false;
	TestEqual(TEXT("The ground skill is charging, not attacking"),
		Fixture.AnimInstance->Montage_GetCurrentSection(), FName(TEXT("Prepare")));
	TestTrue(TEXT("Charge initially has resolved targeting"), Fixture.Ability->GetActiveSkillTargetResult().bIsValid);
	Fixture.Advance(0.6f);
	Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Fixture.Release();
	TestTrue(TEXT("Invalid release follows GAS cancellation"), bObservedEnd && bCancelled);
	TestFalse(TEXT("Invalid release leaves no active skill"), Fixture.Ability->IsActive());
	TestFalse(TEXT("Invalid release clears the previous aim"), Fixture.Ability->GetActiveSkillTargetResult().bIsValid);
	TestFalse(TEXT("Invalid release stops the charge montage"), Fixture.AnimInstance->Montage_IsPlaying(Definition->SkillMontage));
	FGameplayEffectQuery Cooldown;
	Cooldown.EffectDefinition = UGE_CoolDown::StaticClass();
	Cooldown.EffectSource = Definition;
	TestEqual(TEXT("Cancellation retains its committed cooldown"), Fixture.ASC->GetActiveEffects(Cooldown).Num(), 1);
	Fixture.Advance(5.0f);
	TestFalse(TEXT("Cancelled charge timers do not restart the skill"), Fixture.Ability->IsActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillInputLifecycleIntegrationTest,
	"ProjectRPG.Skill.Integration.InputReleaseCancelCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillInputLifecycleIntegrationTest::RunTest(const FString& Parameters)
{
	URPGSkillDefinition* AuthoredDefinition = LoadObject<URPGSkillDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_ChargeSlash.DA_Skill_ChargeSlash"));
	if (!TestNotNull(TEXT("Authored charge definition loads"), AuthoredDefinition))
	{
		return false;
	}
	URPGSkillDefinition* Definition = DuplicateObject<URPGSkillDefinition>(
		AuthoredDefinition, GetTransientPackage());
	Definition->SkillVFX = nullptr;
	AddInfo(FString::Printf(TEXT("Fixture cooldown normalized: authored=%g seconds, test=1 second"), Definition->BaseCooldown));
	Definition->BaseCooldown = 1.0f;
	RPGSkillIntegrationTests::FFixture Fixture;
	if (!Fixture.Initialize(*this, Definition))
	{
		return false;
	}
	Fixture.Press();
	if (!TestTrue(TEXT("ASC input activates the charge ability"), Fixture.Ability->IsActive()))
	{
		return false;
	}
	TestTrue(TEXT("Held state reaches the execution host"), Fixture.Ability->IsSkillInputPressed());
	TestEqual(TEXT("Charge starts in Prepare"),
		Fixture.AnimInstance->Montage_GetCurrentSection(), FName(TEXT("Prepare")));
	Fixture.Advance(0.6f);
	Fixture.Release();
	TestFalse(TEXT("Release clears the execution host's held state"), Fixture.Ability->IsSkillInputPressed());
	TestEqual(TEXT("Release advances the real montage to Attack"),
		Fixture.AnimInstance->Montage_GetCurrentSection(), FName(TEXT("Attack")));
	Fixture.ASC->CancelAbilityHandle(Fixture.Handle);
	TestFalse(TEXT("GAS cancellation ends the skill"), Fixture.Ability->IsActive());
	TestFalse(TEXT("Cancellation clears resolved targeting"),
		Fixture.Ability->GetActiveSkillTargetResult().bIsValid);
	Fixture.Press();
	TestFalse(TEXT("Re-input before one second cannot reactivate"), Fixture.Ability->IsActive());
	Fixture.Release();
	Fixture.Advance(0.5f);
	Fixture.Press();
	TestTrue(TEXT("A fresh press after cooldown can reactivate"), Fixture.Ability->IsActive());
	TestEqual(TEXT("Reactivation restarts charge, not the prior attack"),
		Fixture.AnimInstance->Montage_GetCurrentSection(), FName(TEXT("Prepare")));
	Fixture.ASC->CancelAbilityHandle(Fixture.Handle);
	Fixture.Release();
	Fixture.Advance(5.0f);
	TestFalse(TEXT("Cancelled timers never reactivate the ability"), Fixture.Ability->IsActive());
	TestFalse(TEXT("No montage is left running after cancellation"),
		Fixture.AnimInstance->Montage_IsPlaying(Definition->SkillMontage));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillDefinitionCooldownIntegrationTest,
	"ProjectRPG.Skill.Integration.DefinitionCooldownAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillDefinitionCooldownIntegrationTest::RunTest(const FString& Parameters)
{
	URPGSkillDefinition* AuthoredDefinition = LoadObject<URPGSkillDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_ChargeSlash.DA_Skill_ChargeSlash"));
	UClass* AuthoredClass = LoadClass<URPGGameplayAbility_SkillContainer>(nullptr,
		TEXT("/Game/Blueprints/Character/Player/Abilities/Skill/GA_Player_ChargeSlash.GA_Player_ChargeSlash_C"));
	if (!TestNotNull(TEXT("Cooldown fixture definition"), AuthoredDefinition) ||
		!TestNotNull(TEXT("Cooldown fixture ability class"), AuthoredClass)) return false;
	// Inspect the inherited setting, without changing a CDO or saving an asset.
	UGameplayEffect* LegacyCooldown = AuthoredClass->GetDefaultObject<URPGGameplayAbility_SkillContainer>()
		->UGameplayAbility::GetCooldownGameplayEffect();
	FClassProperty* Property = FindFProperty<FClassProperty>(
		UGameplayAbility::StaticClass(), TEXT("CooldownGameplayEffectClass"));
	if (!TestNotNull(TEXT("Authored legacy cooldown"), LegacyCooldown) ||
		!TestNotNull(TEXT("Inherited cooldown property"), Property)) return false;
	URPGSkillDefinition* Definition = DuplicateObject<URPGSkillDefinition>(AuthoredDefinition, GetTransientPackage());
	Definition->SkillVFX = nullptr;
	Definition->BaseCooldown = 1.0f;
	RPGSkillIntegrationTests::FFixture Fixture;
	if (!Fixture.Initialize(*this, Definition)) return false;
	Property->SetObjectPropertyValue_InContainer(Fixture.Ability, LegacyCooldown->GetClass());
	TestTrue(TEXT("The old setting is retained on the transient instance"),
		Fixture.Ability->UGameplayAbility::GetCooldownGameplayEffect() == LegacyCooldown);
	TestNull(TEXT("The skill container disables the second asset cooldown"), Fixture.Ability->GetCooldownGameplayEffect());
	URPGPlayerGameplayAbility* OrdinaryAbility = NewObject<URPGPlayerGameplayAbility>(Fixture.World);
	Property->SetObjectPropertyValue_InContainer(OrdinaryAbility, LegacyCooldown->GetClass());
	TestTrue(TEXT("Non-container abilities retain their authored cooldown"),
		OrdinaryAbility->GetCooldownGameplayEffect() == LegacyCooldown);
	const FGameplayAbilityActorInfo* ActorInfo = Fixture.Ability->GetCurrentActorInfo();
	auto ExpectCooldown = [this, &Fixture, ActorInfo](const TCHAR* Message, const float Expected)
	{
		float Remaining = 0.0f, Duration = 0.0f;
		Fixture.Ability->GetCooldownTimeRemainingAndDuration(Fixture.Handle, ActorInfo, Remaining, Duration);
		TestEqual(Message, Duration, Expected);
		TestTrue(TEXT("Remaining time is positive and bounded by the Definition cooldown"),
			Remaining > 0.0f && Remaining <= Expected + KINDA_SMALL_NUMBER);
		TestTrue(TEXT("Both public cooldown queries agree"), FMath::IsNearlyEqual(
			Fixture.Ability->GetCooldownTimeRemaining(ActorInfo), Remaining));
	};
	FGameplayEffectQuery OwnQuery;
	OwnQuery.EffectDefinition = UGE_CoolDown::StaticClass();
	OwnQuery.EffectSource = Definition;
	FGameplayEffectQuery LegacyQuery;
	LegacyQuery.EffectDefinition = LegacyCooldown->GetClass();
	Fixture.Press();
	if (!TestTrue(TEXT("The skill activates with an inherited legacy setting"), Fixture.Ability->IsActive())) return false;
	ExpectCooldown(TEXT("Default cooldown is exactly one second"), 1.0f);
	TestEqual(TEXT("Exactly one per-definition cooldown is applied"), Fixture.ASC->GetActiveEffects(OwnQuery).Num(), 1);
	TestTrue(TEXT("No inherited cooldown effect is applied"), Fixture.ASC->GetActiveEffects(LegacyQuery).IsEmpty());
	Definition->BaseCooldown = 4.0f;
	TestEqual(TEXT("Data edits do not change the active snapshot"), Fixture.Ability->GetActiveSkillSpec().BaseCooldown, 1.0f);
	Fixture.ASC->CancelAbilityHandle(Fixture.Handle);
	Fixture.Release();
	ExpectCooldown(TEXT("Cancellation retains the committed one-second cooldown"), 1.0f);
	// A different ability can still own this long effect/tag on the same ASC.
	FGameplayEffectContextHandle Context = Fixture.ASC->MakeEffectContext();
	Context.AddSourceObject(OrdinaryAbility);
	FGameplayEffectSpec LegacySpec(LegacyCooldown, Context, 1.0f);
	if (!TestTrue(TEXT("An unrelated legacy cooldown is genuinely active"),
		Fixture.ASC->ApplyGameplayEffectSpecToSelf(LegacySpec).IsValid())) return false;
	Fixture.Advance(0.5f);
	Fixture.Press();
	TestFalse(TEXT("Rapid input before one second is blocked"), Fixture.Ability->IsActive());
	Fixture.Release();
	Fixture.Advance(0.6f);
	TestFalse(TEXT("The unrelated long cooldown has not expired"), Fixture.ASC->GetActiveEffects(LegacyQuery).IsEmpty());
	Fixture.Press();
	if (!TestTrue(TEXT("After one second the skill ignores another ability's legacy cooldown"),
		Fixture.Ability->IsActive())) return false;
	ExpectCooldown(TEXT("The next activation uses the explicit four-second Definition value"), 4.0f);
	Fixture.ASC->CancelAbilityHandle(Fixture.Handle);
	Fixture.Release();
	ExpectCooldown(TEXT("Ending does not reset the committed four-second effect"), 4.0f);
	Fixture.Advance(1.1f);
	Fixture.Press();
	TestFalse(TEXT("An explicit longer Definition cooldown is not shortened"), Fixture.Ability->IsActive());
	Fixture.Release();
	Fixture.Advance(3.0f);
	Fixture.Press();
	TestTrue(TEXT("The skill can reactivate after its explicit cooldown"), Fixture.Ability->IsActive());
	TestEqual(TEXT("Expired cooldown effects do not stack"), Fixture.ASC->GetActiveEffects(OwnQuery).Num(), 1);
	Fixture.ASC->CancelAbilityHandle(Fixture.Handle);
	Fixture.Release();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillRecoveryCooldownIntegrationTest,
	"ProjectRPG.Skill.Cooldown.RecoveryInputCommitCancelExpiry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillRecoveryCooldownIntegrationTest::RunTest(const FString& Parameters)
{
	auto* Authored = LoadObject<URPGSkillDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_AssultBlade.DA_Skill_AssultBlade"));
	if (!TestNotNull(TEXT("Authored Assault definition"), Authored)) return false;
	auto* Definition = DuplicateObject<URPGSkillDefinition>(Authored, GetTransientPackage());
	Definition->SkillVFX = nullptr;
	FRPGSkillSaveData Save;
	Save.SkillLevel = 7;
	Save.SelectedTripodIndices.Init(INDEX_NONE, Definition->TripodTiers.Num());
	for (const auto& Choice : {TPair<int32, FName>(4, TEXT("QuickApproach")), TPair<int32, FName>(7, TEXT("ExtraRecovery"))})
	{
		int32 Tier, Option;
		if (!TestTrue(TEXT("Resolve the stable saved recovery choice"),
			Definition->ResolveSavedTripod(Choice.Key, Choice.Value, Tier, Option))) return false;
		Save.SelectedTripodIndices[Tier] = Option;
	}
	RPGSkillIntegrationTests::FFixture Fixture;
	if (!Fixture.Initialize(*this, Definition, Save)) return false;
	Fixture.Press();
	if (!TestTrue(TEXT("Restored recovery choices activate through the equipped slot"), Fixture.Ability->IsActive())) return false;
	const auto& Active = Fixture.Ability->GetActiveSkillSpec();
	TestEqual(TEXT("Activation freezes the authored eight-second base"), Active.BaseCooldown, 8.f);
	TestEqual(TEXT("Both selected tiers subtract three seconds"), Active.GetCooldownDuration(), 5.f);
	FGameplayEffectQuery Query;
	Query.EffectDefinition = UGE_CoolDown::StaticClass();
	Query.EffectSource = Definition;
	const auto Effects = Fixture.ASC->GetActiveEffects(Query);
	if (!TestEqual(TEXT("Only one Definition-owned cooldown commits"), Effects.Num(), 1)) return false;
	const auto FirstHandle = Effects[0];
	TestEqual(TEXT("The real GAS effect uses five seconds"), Fixture.ASC->GetActiveGameplayEffect(FirstHandle)->Spec.GetDuration(), 5.f);
	Definition->BaseCooldown = 20.f;
	TestEqual(TEXT("Later data changes leave the active snapshot intact"), Active.GetCooldownDuration(), 5.f);
	Fixture.ASC->CancelAbilityHandle(Fixture.Handle);
	Fixture.Release();
	Fixture.Advance(1.1f);
	Fixture.Press();
	TestFalse(TEXT("The old one-second baseline cannot bypass recovery cooldown"), Fixture.Ability->IsActive());
	Fixture.Release();
	TestTrue(TEXT("Cancellation and rejected input retain the same effect"), Fixture.ASC->GetActiveGameplayEffect(FirstHandle) != nullptr);
	Fixture.Advance(4.f);
	TestTrue(TEXT("The committed effect expires normally"), Fixture.ASC->GetActiveEffects(Query).IsEmpty());
	Fixture.Press();
	TestTrue(TEXT("Fresh input succeeds after five seconds"), Fixture.Ability->IsActive());
	TestEqual(TEXT("Only the next activation uses updated base data"), Fixture.Ability->GetActiveSkillSpec().GetCooldownDuration(), 17.f);
	TestEqual(TEXT("Expired effects do not accumulate"), Fixture.ASC->GetActiveEffects(Query).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGPlayerSkillCompositionTest,
	"ProjectRPG.Skill.Integration.PlayerComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGPlayerSkillCompositionTest::RunTest(const FString& Parameters)
{
	const ARPGPlayer* Player = GetDefault<ARPGPlayer>();
	const URPGPlayerSkillComponent* Component = Player->GetPlayerSkillComponent();
	if (!TestNotNull(TEXT("Native player owns the skill progression component"), Component))
	{
		return false;
	}
	TestTrue(TEXT("UI and ability component discovery finds the same model"),
		Player->FindComponentByClass<URPGPlayerSkillComponent>() == Component);
	TestTrue(TEXT("The native skill component is replicated"), Component->GetIsReplicated());

	UClass* PlayerClass = LoadClass<ARPGPlayer>(nullptr,
		TEXT("/Game/Blueprints/Character/Player/BP_Player.BP_Player_C"));
	if (!TestNotNull(TEXT("Authored player Blueprint loads"), PlayerClass))
	{
		return false;
	}
	const ARPGPlayer* BlueprintPlayer = PlayerClass->GetDefaultObject<ARPGPlayer>();
	TestNotNull(TEXT("Existing player Blueprint inherits the skill component"),
		BlueprintPlayer->GetPlayerSkillComponent());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGPlayerSkillSelectionSnapshotTest,
	"ProjectRPG.Skill.Integration.PlayerSelectionSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGPlayerSkillSelectionSnapshotTest::RunTest(const FString& Parameters)
{
	URPGSkillDefinition* AuthoredDefinition = LoadObject<URPGSkillDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_ChargeSlash.DA_Skill_ChargeSlash"));
	if (!TestNotNull(TEXT("Authored charge definition loads"), AuthoredDefinition))
	{
		return false;
	}
	// Only the fixture receives this tripod; no authored asset or D1 code changes.
	URPGSkillDefinition* Definition = DuplicateObject<URPGSkillDefinition>(
		AuthoredDefinition, GetTransientPackage());
	Definition->SkillVFX = nullptr;
	AddInfo(FString::Printf(TEXT("Fixture cooldown normalized: authored=%g seconds, test=1 second"), Definition->BaseCooldown));
	Definition->BaseCooldown = 1.0f;
	Definition->TripodTiers.Reset();
	FRPGSkillTripodTier& Tier = Definition->TripodTiers.AddDefaulted_GetRef();
	Tier.RequiredSkillLevel = 4;
	FRPGSkillTripodOption& Option = Tier.Options.AddDefaulted_GetRef();
	Option.OverrideExecutionPolicyClass = URPGSkillExecutionPolicy_Instant::StaticClass();
	FRPGSkillInstantExecutionConfig InstantConfig;
	InstantConfig.StartSection = TEXT("Attack");
	Option.OverrideExecutionConfig.InitializeAs<FRPGSkillInstantExecutionConfig>(InstantConfig);

	RPGSkillIntegrationTests::FFixture Fixture;
	if (!Fixture.Initialize(*this, Definition))
	{
		return false;
	}
	URPGPlayerSkillComponent* Component = Fixture.Player->GetPlayerSkillComponent();
	FArrayProperty* ReplicatedDataProperty = FindFProperty<FArrayProperty>(
		URPGPlayerSkillComponent::StaticClass(), TEXT("ReplicatedSkillData"));
	if (!TestNotNull(TEXT("Player skill component"), Component) ||
		!TestNotNull(TEXT("Replicated selection property"), ReplicatedDataProperty))
	{
		return false;
	}
	// Simulate an authoritative selection delivery through the real OnRep path.
	// This does not exercise RPC transport, prediction or UI button commands.
	auto DeliverSelection = [&](const FRPGSkillSaveData& SaveData)
	{
		FRPGReplicatedSkillSaveEntry Entry;
		Entry.SkillTag = Definition->SkillTag;
		Entry.SaveData = SaveData;
		Entry.RestoreGeneration = Component->GetSkillRestoreGeneration();
		*ReplicatedDataProperty->ContainerPtrToValuePtr<TArray<FRPGReplicatedSkillSaveEntry>>(Component) = {Entry};
		Component->ProcessEvent(Component->FindFunctionChecked(TEXT("OnRep_ReplicatedSkillData")), nullptr);
	};
	FRPGSkillSaveData Selection;
	Selection.SkillLevel = 4;
	Selection.SelectedTripodIndices = {0};
	DeliverSelection(Selection);
	URPGSkillViewModel* ViewModel = NewObject<URPGSkillViewModel>(Fixture.Player);
	ViewModel->InitializeSkillData(Component, {Definition});
	if (!TestEqual(TEXT("The native player's selection creates one UI slot"),
		ViewModel->SkillSlots.Num(), 1))
	{
		return false;
	}
	URPGSkillSlotViewModel* Slot = ViewModel->SkillSlots[0];
	TestEqual(TEXT("UI reads the selected skill level"), Slot->SkillLevel, 4);
	TestTrue(TEXT("UI reads the selected tripod"), Slot->IsTripodSelected(0, 0));
	Fixture.Press();
	if (!TestTrue(TEXT("The native player's ASC activates the skill"), Fixture.Ability->IsActive()))
	{
		return false;
	}
	TestEqual(TEXT("GAS reads the same selected skill level"),
		Fixture.Ability->GetActiveSkillSpec().SkillLevel, 4);
	TestTrue(TEXT("The selected tripod replaces charge with instant execution"),
		Fixture.Ability->GetActiveSkillSpec().ExecutionPolicyClass ==
			URPGSkillExecutionPolicy_Instant::StaticClass());
	TestEqual(TEXT("The actual montage begins at the instant attack section"),
		Fixture.AnimInstance->Montage_GetCurrentSection(), FName(TEXT("Attack")));

	Selection.SkillLevel = 1;
	Selection.SelectedTripodIndices = {INDEX_NONE};
	DeliverSelection(Selection);
	TestEqual(TEXT("OnRep refreshes the existing UI slot"), Slot->SkillLevel, 1);
	TestFalse(TEXT("OnRep clears the UI's selected tripod"), Slot->IsTripodSelected(0, 0));
	TestEqual(TEXT("Selection changes cannot mutate an active level snapshot"),
		Fixture.Ability->GetActiveSkillSpec().SkillLevel, 4);
	TestTrue(TEXT("Selection changes cannot mutate an active execution snapshot"),
		Fixture.Ability->GetActiveSkillSpec().ExecutionPolicyClass ==
			URPGSkillExecutionPolicy_Instant::StaticClass());

	Fixture.ASC->CancelAbilityHandle(Fixture.Handle);
	Fixture.Release();
	Fixture.Advance(1.1f);
	Fixture.Press();
	TestTrue(TEXT("A new activation uses the refreshed selection"), Fixture.Ability->IsActive());
	TestEqual(TEXT("A new activation reads the refreshed level"),
		Fixture.Ability->GetActiveSkillSpec().SkillLevel, 1);
	TestTrue(TEXT("Removing the tripod restores charge on the next activation"),
		Fixture.Ability->GetActiveSkillSpec().ExecutionPolicyClass ==
			URPGSkillExecutionPolicy_Charge::StaticClass());
	TestEqual(TEXT("The next activation actually begins charging"),
		Fixture.AnimInstance->Montage_GetCurrentSection(), FName(TEXT("Prepare")));
	Fixture.Release();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillTripodButtonExecutionTest,
	"ProjectRPG.Skill.Integration.TripodButtonExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillTripodButtonExecutionTest::RunTest(const FString& Parameters)
{
	URPGSkillDefinition* Authored = LoadObject<URPGSkillDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_ChargeSlash.DA_Skill_ChargeSlash"));
	UClass* BoardClass = LoadClass<URPGTripodBoard>(nullptr,
		TEXT("/Game/Blueprints/UI/Skill/WBP_TripodBoard.WBP_TripodBoard_C"));
	UClass* ButtonClass = LoadClass<URPGTripodButton>(nullptr,
		TEXT("/Game/Blueprints/UI/Skill/WBP_TripodButton.WBP_TripodButton_C"));
	if (!TestNotNull(TEXT("Authored charge definition"), Authored)
		|| !TestNotNull(TEXT("Authored tripod board"), BoardClass)
		|| !TestNotNull(TEXT("Authored tripod button"), ButtonClass)) return false;
	UWidgetBlueprintGeneratedClass* BoardBP = Cast<UWidgetBlueprintGeneratedClass>(BoardClass);
	UWidgetBlueprintGeneratedClass* ButtonBP = Cast<UWidgetBlueprintGeneratedClass>(ButtonClass);
	if (!TestNotNull(TEXT("Board Blueprint initialization settings"), BoardBP)
		|| !TestNotNull(TEXT("Button Blueprint initialization settings"), ButtonBP)) return false;
	// Headless widgets have no local-player/viewport context. Explicitly allow
	// their normal native initialization for this test, then restore both flags.
	// No asset files are modified or saved; no input routing is simulated.
	const bool bBoardInitializedWithoutPlayer = BoardBP->bCanCallInitializedWithoutPlayerContext;
	const bool bButtonInitializedWithoutPlayer = ButtonBP->bCanCallInitializedWithoutPlayerContext;
	BoardBP->bCanCallInitializedWithoutPlayerContext = true;
	ButtonBP->bCanCallInitializedWithoutPlayerContext = true;
	ON_SCOPE_EXIT
	{
		BoardBP->bCanCallInitializedWithoutPlayerContext = bBoardInitializedWithoutPlayer;
		ButtonBP->bCanCallInitializedWithoutPlayerContext = bButtonInitializedWithoutPlayer;
	};
	URPGSkillDefinition* Definition = DuplicateObject<URPGSkillDefinition>(Authored, GetTransientPackage());
	Definition->SkillVFX = nullptr;
	AddInfo(FString::Printf(TEXT("Fixture cooldown normalized: authored=%g seconds, test=1 second"), Definition->BaseCooldown));
	Definition->BaseCooldown = 1.0f;
	Definition->TripodTiers.Reset();
	FRPGSkillTripodTier& Tier = Definition->TripodTiers.AddDefaulted_GetRef();
	Tier.RequiredSkillLevel = 4;
	FRPGSkillTripodOption& Instant = Tier.Options.AddDefaulted_GetRef();
	Instant.OptionName = FText::FromString(TEXT("Instant fixture"));
	Instant.OverrideExecutionPolicyClass = URPGSkillExecutionPolicy_Instant::StaticClass();
	FRPGSkillInstantExecutionConfig InstantConfig;
	InstantConfig.StartSection = TEXT("Attack");
	Instant.OverrideExecutionConfig.InitializeAs<FRPGSkillInstantExecutionConfig>(InstantConfig);
	Tier.Options.AddDefaulted_GetRef().OptionName = FText::FromString(TEXT("Charge fixture"));
	FRPGSkillTripodTier& LockedTier = Definition->TripodTiers.AddDefaulted_GetRef();
	LockedTier.RequiredSkillLevel = 7;
	LockedTier.Options.AddDefaulted();

	RPGSkillIntegrationTests::FFixture Fixture;
	if (!Fixture.Initialize(*this, Definition)
		|| !Fixture.InstallCatalogDefinition(*this, Definition)) return false;
	URPGPlayerSkillComponent* Component = Fixture.Player->GetPlayerSkillComponent();
	const int32 InitialSP = Component->GetRemainingSP();
	URPGSkillViewModel* Model = NewObject<URPGSkillViewModel>(Fixture.Player);
	Model->InitializeSkillData(Component, {Definition});
	if (!TestEqual(TEXT("One skill slot"), Model->SkillSlots.Num(), 1)) return false;
	URPGSkillSlotViewModel* Slot = Model->SkillSlots[0];
	URPGTripodBoard* Board = NewObject<URPGTripodBoard>(GetTransientPackage(), BoardClass);
	if (!TestTrue(TEXT("Authored board initializes"), Board->Initialize())) return false;
	[[maybe_unused]] const TSharedRef<SWidget> SlateBoard = Board->TakeWidget();
	Board->InitializeBoard(Slot);
	URPGTripodButton* InstantButton = Cast<URPGTripodButton>(Board->GetWidgetFromName(TEXT("Btn_1_1")));
	URPGTripodButton* ChargeButton = Cast<URPGTripodButton>(Board->GetWidgetFromName(TEXT("Btn_1_2")));
	URPGTripodButton* MissingButton = Cast<URPGTripodButton>(Board->GetWidgetFromName(TEXT("Btn_1_3")));
	URPGTripodButton* LockedButton = Cast<URPGTripodButton>(Board->GetWidgetFromName(TEXT("Btn_2_1")));
	if (!TestNotNull(TEXT("Instant tripod button"), InstantButton)
		|| !TestNotNull(TEXT("Charge tripod button"), ChargeButton)
		|| !TestNotNull(TEXT("Missing option button"), MissingButton)
		|| !TestNotNull(TEXT("Locked tier button"), LockedButton)) return false;
	const auto Click = [](URPGTripodButton* Button)
	{
		// Exercise CommonUI's actual handler, including its automatic selection
		// logic. This is not an OnClicked delegate or simulated mouse hardware.
		Button->ProcessEvent(Button->FindFunctionChecked(TEXT("HandleButtonClicked")), nullptr);
	};
	TestFalse(TEXT("Level one locks the first tier"), InstantButton->GetIsEnabled());
	TestTrue(TEXT("Absent options are collapsed"), MissingButton->GetVisibility() == ESlateVisibility::Collapsed);
	Click(InstantButton);
	TestFalse(TEXT("Locked button cannot select through CommonUI"), Slot->IsTripodSelected(0, 0));
	for (int32 Level = 1; Level < 4; ++Level) Model->RequestSkillLevelUp(Definition->SkillTag);
	TestEqual(TEXT("Level commands reach the canonical component"), Slot->SkillLevel, 4);
	TestTrue(TEXT("The first tier unlocks reactively"), InstantButton->GetIsEnabled());
	TestFalse(TEXT("Higher tier remains locked"), LockedButton->GetIsEnabled());
	Click(InstantButton);
	TestTrue(TEXT("CommonUI click reaches the real selection command"), Slot->IsTripodSelected(0, 0));
	TestTrue(TEXT("The selected native button matches the model"), InstantButton->GetSelected());
	TestFalse(TEXT("Unselected native button stays clear"), ChargeButton->GetSelected());
	Fixture.Press();
	if (!TestTrue(TEXT("Selected tripod activates through GAS"), Fixture.Ability->IsActive())) return false;
	TestTrue(TEXT("Button selection changes charge to instant execution"),
		Fixture.Ability->GetActiveSkillSpec().ExecutionPolicyClass == URPGSkillExecutionPolicy_Instant::StaticClass());
	TestEqual(TEXT("The actual montage starts at Attack"),
		Fixture.AnimInstance->Montage_GetCurrentSection(), FName(TEXT("Attack")));
	Click(ChargeButton);
	TestTrue(TEXT("Active class execution rejects configuration changes"), Slot->IsTripodSelected(0, 0));
	TestFalse(TEXT("Rejected switch cannot select the charge button"), ChargeButton->GetSelected());
	TestTrue(TEXT("Rejected UI changes preserve the active execution snapshot"),
		Fixture.Ability->GetActiveSkillSpec().ExecutionPolicyClass == URPGSkillExecutionPolicy_Instant::StaticClass());
	Fixture.ASC->CancelAbilityHandle(Fixture.Handle);
	Fixture.Release();
	Fixture.Advance(1.1f);
	Click(ChargeButton);
	TestTrue(TEXT("Switching buttons updates the same slot"), Slot->IsTripodSelected(0, 1));
	TestFalse(TEXT("Switch clears the previous native selection"), InstantButton->GetSelected());
	TestTrue(TEXT("Switch selects only the new native button"), ChargeButton->GetSelected());
	TestFalse(TEXT("An explicit duplicate selection is a no-op"),
		Component->SelectTripod(Definition->SkillTag, 0, 1));
	TestTrue(TEXT("Duplicate explicit commands preserve model selection"), Slot->IsTripodSelected(0, 1));
	Click(ChargeButton);
	TestFalse(TEXT("Clicking the selected UI option clears its display"), ChargeButton->GetSelected());
	TestFalse(TEXT("The UI clear command also clears model selection"), Slot->IsTripodSelected(0, 1));
	Click(ChargeButton);
	TestTrue(TEXT("A new UI click selects the option again"), ChargeButton->GetSelected() && Slot->IsTripodSelected(0, 1));
	Fixture.Press();
	if (!TestTrue(TEXT("Next activation succeeds"), Fixture.Ability->IsActive())) return false;
	TestTrue(TEXT("Switch restores charge on the next activation"),
		Fixture.Ability->GetActiveSkillSpec().ExecutionPolicyClass == URPGSkillExecutionPolicy_Charge::StaticClass());
	TestEqual(TEXT("The actual montage starts at Prepare"),
		Fixture.AnimInstance->Montage_GetCurrentSection(), FName(TEXT("Prepare")));
	Model->RequestSkillLevelDown(Definition->SkillTag);
	TestEqual(TEXT("Active class execution rejects level changes"), Slot->SkillLevel, 4);
	TestEqual(TEXT("An active level snapshot remains unchanged"), Fixture.Ability->GetActiveSkillSpec().SkillLevel, 4);
	Fixture.ASC->CancelAbilityHandle(Fixture.Handle);
	Fixture.Release();

	// Simulate a rejected prediction through the existing owner OnRep path.
	// Actual RPC transport is covered separately by the network probe.
	FRPGReplicatedSkillSaveEntry Confirmed;
	Confirmed.SkillTag = Definition->SkillTag;
	Confirmed.SaveData = Component->GetSkillSaveData(Definition->SkillTag);
	Confirmed.RestoreGeneration = Component->GetSkillRestoreGeneration();
	Click(InstantButton);
	TestTrue(TEXT("Selection change is visible before reconciliation"), InstantButton->GetSelected());
	FArrayProperty* Data = FindFProperty<FArrayProperty>(
		URPGPlayerSkillComponent::StaticClass(), TEXT("ReplicatedSkillData"));
	if (!TestNotNull(TEXT("Owner reconciliation fixture property"), Data)) return false;
	*Data->ContainerPtrToValuePtr<TArray<FRPGReplicatedSkillSaveEntry>>(Component) = {Confirmed};
	Component->ProcessEvent(Component->FindFunctionChecked(TEXT("OnRep_ReplicatedSkillData")), nullptr);
	TestFalse(TEXT("Reconciliation clears the rejected native selection"), InstantButton->GetSelected());
	TestTrue(TEXT("Reconciliation restores the confirmed native selection"), ChargeButton->GetSelected());
	Model->RequestSkillLevelDown(Definition->SkillTag);
	TestEqual(TEXT("Level down is reflected by the existing slot"), Slot->SkillLevel, 3);
	TestFalse(TEXT("Relocking a tier clears native selection"), ChargeButton->GetSelected());
	TestFalse(TEXT("Relocking a tier disables both options"), InstantButton->GetIsEnabled() || ChargeButton->GetIsEnabled());
	Model->RequestSkillLevelMin(Definition->SkillTag);
	TestEqual(TEXT("Fixture level commands refund the original SP budget"), Component->GetRemainingSP(), InitialSP);
	Board->InitializeBoard(nullptr);
	TestTrue(TEXT("Detaching the model hides all options"), InstantButton->GetVisibility() == ESlateVisibility::Collapsed);
	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillConfigBlueprintLibraryTest,
	"ProjectRPG.Skill.Config.TypeSafeWrapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillConfigBlueprintLibraryTest::RunTest(
	const FString& Parameters)
{
	FRPGSkillInstantExecutionConfig InstantConfig;
	InstantConfig.StartSection = TEXT("Attack");
	const FInstancedStruct InstantStruct =
		URPGSkillConfigBlueprintLibrary::MakeInstantExecutionConfig(
			InstantConfig);
	const FRPGSkillInstantExecutionConfig* WrappedInstant =
		InstantStruct.GetPtr<FRPGSkillInstantExecutionConfig>();
	TestNotNull(TEXT("Instant config retains its concrete type"), WrappedInstant);
	if (WrappedInstant)
	{
		TestEqual(TEXT("Instant config retains its authored section"),
			WrappedInstant->StartSection,
			InstantConfig.StartSection);
	}

	FRPGSkillHoldingExecutionConfig HoldingConfig;
	HoldingConfig.HoldDuration = 4.0f;
	HoldingConfig.SuccessSection = TEXT("End");
	const FInstancedStruct HoldingStruct =
		URPGSkillConfigBlueprintLibrary::MakeHoldingExecutionConfig(
			HoldingConfig);
	const FRPGSkillHoldingExecutionConfig* WrappedHolding =
		HoldingStruct.GetPtr<FRPGSkillHoldingExecutionConfig>();
	TestNotNull(TEXT("Holding config retains its concrete type"), WrappedHolding);
	if (WrappedHolding)
	{
		TestEqual(TEXT("Holding config retains its authored duration"),
			WrappedHolding->HoldDuration,
			HoldingConfig.HoldDuration);
	}

	FRPGSkillSoftTargetingConfig SoftTargetConfig;
	SoftTargetConfig.MaxRange = 875.0f;
	const FInstancedStruct SoftTargetStruct =
		URPGSkillConfigBlueprintLibrary::MakeSoftTargetingConfig(
			SoftTargetConfig);
	const FRPGSkillSoftTargetingConfig* WrappedSoftTarget =
		SoftTargetStruct.GetPtr<FRPGSkillSoftTargetingConfig>();
	TestNotNull(TEXT("Targeting config retains its concrete type"),
		WrappedSoftTarget);
	if (WrappedSoftTarget)
	{
		TestEqual(TEXT("Targeting config retains its authored range"),
			WrappedSoftTarget->MaxRange,
			SoftTargetConfig.MaxRange);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillViewModelReactiveInitializationTest,
	"ProjectRPG.Skill.UI.ReactiveInitialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillViewModelReactiveInitializationTest::RunTest(
	const FString& Parameters)
{
	URPGPlayerSkillComponent* SkillComponent =
		NewObject<URPGPlayerSkillComponent>();
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	Definition->SkillTag =
		RPGGameplayTags::Player_Ability_Skill_AssultBlade;
	URPGSkillViewModel* ViewModel = NewObject<URPGSkillViewModel>();

	ViewModel->InitializeSkillData(SkillComponent, {Definition});
	TestTrue(TEXT("The skill view model subscribes to model changes"),
		SkillComponent->OnSkillDataChanged.IsBound());
	TestEqual(TEXT("Initial total SP comes from the skill component"),
		ViewModel->TotalSP,
		SkillComponent->GetTotalSP());
	TestEqual(TEXT("One definition creates one slot"),
		ViewModel->SkillSlots.Num(),
		1);

	ViewModel->TotalSP = 0;
	SkillComponent->OnSkillDataChanged.Broadcast(FGameplayTag());
	TestEqual(TEXT("Aggregate model events refresh total SP"),
		ViewModel->TotalSP,
		SkillComponent->GetTotalSP());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillLevelAdjusterWidgetStateTest,
	"ProjectRPG.Skill.UI.LevelAdjusterWidgetState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillLevelAdjusterWidgetStateTest::RunTest(const FString& Parameters)
{
	// Use the authored widget tree but no viewport/Slate rendering. This covers
	// native presentation and subscriptions, not clicks, list entry generation
	// or network transport. Deliver transient snapshots through the real OnRep
	// path; never modify a live player or the game-instance catalog.
	UClass* WidgetClass = LoadClass<URPGSkillLevelAdjuster>(nullptr,
		TEXT("/Game/Blueprints/UI/Skill/WBP_SkillLevelAdjuster.WBP_SkillLevelAdjuster_C"));
	if (!TestNotNull(TEXT("Authored level adjuster class"), WidgetClass))
	{
		return false;
	}
	URPGPlayerSkillComponent* Component = NewObject<URPGPlayerSkillComponent>();
	URPGSkillDefinition* First = NewObject<URPGSkillDefinition>();
	First->SkillTag = RPGGameplayTags::Player_Ability_Skill_AssultBlade;
	First->MaxSkillLevel = 3;
	URPGSkillDefinition* Second = NewObject<URPGSkillDefinition>();
	Second->SkillTag = RPGGameplayTags::Player_Ability_Skill_Charge;
	Second->MaxSkillLevel = 3;
	FIntProperty* PointsProperty = FindFProperty<FIntProperty>(
		URPGPlayerSkillComponent::StaticClass(), TEXT("TotalSP"));
	FIntProperty* UsedProperty = FindFProperty<FIntProperty>(
		URPGPlayerSkillComponent::StaticClass(), TEXT("UsedSP"));
	FArrayProperty* DataProperty = FindFProperty<FArrayProperty>(
		URPGPlayerSkillComponent::StaticClass(), TEXT("ReplicatedSkillData"));
	if (!TestNotNull(TEXT("Transient SP fixture property"), PointsProperty)
		|| !TestNotNull(TEXT("Transient spent SP property"), UsedProperty)
		|| !TestNotNull(TEXT("Transient replicated data property"), DataProperty))
	{
		return false;
	}
	const int32 FirstCost = Component->GetRequiredSPForLevel(2);
	PointsProperty->SetPropertyValue_InContainer(Component, FirstCost);
	URPGSkillViewModel* Model = NewObject<URPGSkillViewModel>();
	Model->InitializeSkillData(Component, {First, Second});
	if (!TestEqual(TEXT("Two independent skill rows"), Model->SkillSlots.Num(), 2))
	{
		return false;
	}
	URPGSkillSlotViewModel* FirstSlot = Model->SkillSlots[0];
	URPGSkillSlotViewModel* SecondSlot = Model->SkillSlots[1];
	URPGSkillLevelAdjuster* Adjuster = NewObject<URPGSkillLevelAdjuster>(
		GetTransientPackage(), WidgetClass);
	if (!TestTrue(TEXT("Authored widget tree initializes"), Adjuster->Initialize()))
	{
		return false;
	}
	UWidget* Increase = Adjuster->GetWidgetFromName(TEXT("Btn_IncreaseLevel"));
	UWidget* Decrease = Adjuster->GetWidgetFromName(TEXT("Btn_DecreaseLevel"));
	UCommonTextBlock* Level = Cast<UCommonTextBlock>(
		Adjuster->GetWidgetFromName(TEXT("CurrentSkillLevelText")));
	UCommonTextBlock* Cost = Cast<UCommonTextBlock>(
		Adjuster->GetWidgetFromName(TEXT("RequireLevelText")));
	if (!TestNotNull(TEXT("Authored increase button"), Increase)
		|| !TestNotNull(TEXT("Authored decrease button"), Decrease)
		|| !TestNotNull(TEXT("Authored level display"), Level)
		|| !TestNotNull(TEXT("Authored cost display"), Cost))
	{
		return false;
	}
	const auto TestLevel = [this, Level](const TCHAR* Message, int32 Expected)
	{
		TestEqual(Message, Level->GetText().ToString(), FText::AsNumber(Expected).ToString());
	};
	const auto DeliverSnapshot = [=](int32 FirstLevel, int32 SecondLevel, int32 TotalSP)
	{
		FRPGReplicatedSkillSaveEntry FirstEntry;
		FirstEntry.SkillTag = First->SkillTag;
		FirstEntry.SaveData.SkillLevel = FirstLevel;
		FRPGReplicatedSkillSaveEntry SecondEntry;
		SecondEntry.SkillTag = Second->SkillTag;
		SecondEntry.SaveData.SkillLevel = SecondLevel;
		int32 UsedSP = 0;
		for (int32 SkillLevel : {FirstLevel, SecondLevel})
		{
			for (int32 CurrentLevel = 2; CurrentLevel <= SkillLevel; ++CurrentLevel)
			{
				UsedSP += Component->GetRequiredSPForLevel(CurrentLevel);
			}
		}
		PointsProperty->SetPropertyValue_InContainer(Component, TotalSP);
		UsedProperty->SetPropertyValue_InContainer(Component, UsedSP);
		*DataProperty->ContainerPtrToValuePtr<TArray<FRPGReplicatedSkillSaveEntry>>(Component) =
			{FirstEntry, SecondEntry};
		Component->ProcessEvent(Component->FindFunctionChecked(TEXT("OnRep_ReplicatedSkillData")), nullptr);
	};
	Adjuster->InitializeAdjuster(Model, FirstSlot);
	TestTrue(TEXT("Initial affordable increase is enabled"), Increase->GetIsEnabled());
	TestFalse(TEXT("Level one cannot be refunded"), Decrease->GetIsEnabled());
	TestLevel(TEXT("Initial displayed level"), 1);
	TestEqual(TEXT("Displayed cost uses the model"), Cost->GetText().ToString(),
		FText::AsNumber(FirstSlot->NextLevelCost).ToString());

	DeliverSnapshot(1, 2, FirstCost);
	TestEqual(TEXT("Other row spends the shared budget"), Model->RemainingSP, 0);
	TestEqual(TEXT("Untouched row retains its level"), FirstSlot->SkillLevel, 1);
	TestFalse(TEXT("SP notification disables the untouched row"), Increase->GetIsEnabled());
	DeliverSnapshot(1, 1, FirstCost);
	TestTrue(TEXT("Other row refund enables increase again"), Increase->GetIsEnabled());

	DeliverSnapshot(2, 1, FirstCost);
	TestEqual(TEXT("Level-only notification fixture keeps the next cost unchanged"),
		FirstSlot->NextLevelCost, FirstCost);
	TestLevel(TEXT("Displayed level updates even if the next cost is unchanged"), 2);
	TestTrue(TEXT("A spent level can be refunded"), Decrease->GetIsEnabled());
	const int32 FullBudget = FirstCost + Component->GetRequiredSPForLevel(3);
	DeliverSnapshot(2, 1, FullBudget);
	TestTrue(TEXT("Additional SP enables the next level"), Increase->GetIsEnabled());
	DeliverSnapshot(3, 1, FullBudget);
	TestLevel(TEXT("Maximum displayed level"), 3);
	TestFalse(TEXT("Maximum level disables increase"), Increase->GetIsEnabled());
	TestEqual(TEXT("Maximum level hides the cost"), Cost->GetVisibility(), ESlateVisibility::Collapsed);
	DeliverSnapshot(1, 1, FullBudget);
	TestLevel(TEXT("Reset restores the level display"), 1);
	TestEqual(TEXT("Reset refunds all transient SP"), Model->RemainingSP, Model->TotalSP);
	TestFalse(TEXT("Reset disables refund"), Decrease->GetIsEnabled());

	Adjuster->InitializeAdjuster(Model, SecondSlot);
	TestFalse(TEXT("Rebinding removes the old cost subscription"),
		FirstSlot->OnNextLevelCostChanged.IsBoundToObject(Adjuster));
	DeliverSnapshot(2, 1, FullBudget);
	TestLevel(TEXT("Old row level changes do not replace the rebound display"), 1);
	DeliverSnapshot(2, 2, FullBudget);
	TestLevel(TEXT("The new row updates the rebound display"), 2);
	Adjuster->InitializeAdjuster(nullptr, nullptr);
	TestFalse(TEXT("Release removes the current cost subscription"),
		SecondSlot->OnNextLevelCostChanged.IsBoundToObject(Adjuster));
	TestFalse(TEXT("Released row has no increase command"), Increase->GetIsEnabled());
	TestFalse(TEXT("Released row has no refund command"), Decrease->GetIsEnabled());
	TestTrue(TEXT("Released row clears its displayed level"), Level->GetText().IsEmpty());
	DeliverSnapshot(2, 1, FullBudget);
	TestTrue(TEXT("Former model changes leave the released display empty"), Level->GetText().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillPrimaryAssetIdentityTest,
	"ProjectRPG.Skill.Catalog.PrimaryAssetIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillPrimaryAssetIdentityTest::RunTest(const FString& Parameters)
{
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	Definition->SkillTag =
		RPGGameplayTags::Player_Ability_Skill_AssultBlade;

	const FPrimaryAssetId ExpectedId =
		URPGSkillDefinition::MakePrimaryAssetIdForTag(Definition->SkillTag);
	TestTrue(TEXT("A valid skill tag produces a primary asset id"),
		ExpectedId.IsValid());
	TestEqual(TEXT("Skill definitions use the catalog primary asset type"),
		ExpectedId.PrimaryAssetType,
		URPGSkillDefinition::PrimaryAssetType);
	TestEqual(TEXT("The skill tag is the stable primary asset name"),
		ExpectedId.PrimaryAssetName,
		Definition->SkillTag.GetTagName());
	TestEqual(TEXT("The definition exposes the same stable identity"),
		Definition->GetPrimaryAssetId(),
		ExpectedId);

	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGAuthoredSkillCatalogTest,
	"ProjectRPG.Skill.Catalog.AuthoredDefinitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGAuthoredSkillCatalogTest::RunTest(const FString& Parameters)
{
	const TArray<FGameplayTag> ExpectedTags =
	{
		RPGGameplayTags::Player_Ability_Skill_AssultBlade,
		RPGGameplayTags::Player_Ability_Skill_JumpSmash,
		RPGGameplayTags::Player_Ability_Skill_Charge,
		RPGGameplayTags::Player_Ability_Skill_WhirlWind,
	};

	UAssetManager& AssetManager = UAssetManager::Get();
	TArray<FPrimaryAssetId> RegisteredIds;
	AssetManager.GetPrimaryAssetIdList(
		URPGSkillDefinition::PrimaryAssetType,
		RegisteredIds);

	for (const FGameplayTag SkillTag : ExpectedTags)
	{
		const FPrimaryAssetId ExpectedId =
			URPGSkillDefinition::MakePrimaryAssetIdForTag(SkillTag);
		TestTrue(
			*FString::Printf(TEXT("Catalog registers %s"),
				*SkillTag.ToString()),
			RegisteredIds.Contains(ExpectedId));

		const FSoftObjectPath AssetPath =
			AssetManager.GetPrimaryAssetPath(ExpectedId);
		TestTrue(
			*FString::Printf(TEXT("Catalog resolves %s"),
				*SkillTag.ToString()),
			AssetPath.IsValid());
		URPGSkillDefinition* Definition =
			Cast<URPGSkillDefinition>(AssetPath.TryLoad());
		TestNotNull(
			*FString::Printf(TEXT("Catalog loads %s"),
				*SkillTag.ToString()),
			Definition);
		if (Definition)
		{
			TestEqual(TEXT("Loaded definition retains its stable tag"),
				Definition->SkillTag,
				SkillTag);
			FDataValidationContext ValidationContext;
			TestEqual(TEXT("Loaded definition passes data validation"),
				Definition->IsDataValid(ValidationContext),
				EDataValidationResult::Valid);
		}
	}

	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillDefinitionProgressionRulesTest,
	"ProjectRPG.Skill.Definition.ProgressionRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillDefinitionProgressionRulesTest::RunTest(
	const FString& Parameters)
{
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	Definition->MaxSkillLevel = 5;

	FRPGSkillTripodTier& FirstTier =
		Definition->TripodTiers.AddDefaulted_GetRef();
	FirstTier.RequiredSkillLevel = 2;
	FirstTier.Options.AddDefaulted(2);
	FRPGSkillTripodTier& SecondTier =
		Definition->TripodTiers.AddDefaulted_GetRef();
	SecondTier.RequiredSkillLevel = 4;
	SecondTier.Options.AddDefaulted();

	FRPGSkillSaveData SaveData;
	SaveData.SkillLevel = 99;
	SaveData.SelectedTripodIndices = {1, 7, 0};
	Definition->NormalizeSaveData(SaveData);
	TestEqual(TEXT("Authored maximum level clamps save data"),
		SaveData.SkillLevel,
		5);
	TestEqual(TEXT("Selection count follows authored tripod tiers"),
		SaveData.SelectedTripodIndices.Num(),
		2);
	TestEqual(TEXT("A valid authored option is preserved"),
		SaveData.SelectedTripodIndices[0],
		1);
	TestEqual(TEXT("An out-of-range option is cleared"),
		SaveData.SelectedTripodIndices[1],
		INDEX_NONE);

	SaveData.SkillLevel = 3;
	SaveData.SelectedTripodIndices = {0, 0};
	Definition->NormalizeSaveData(SaveData);
	TestEqual(TEXT("A locked-tier selection is cleared"),
		SaveData.SelectedTripodIndices[1],
		INDEX_NONE);
	TestTrue(TEXT("A locked tier can always be deselected"),
		Definition->IsTripodSelectionAllowed(1, 1, INDEX_NONE));
	TestFalse(TEXT("Unknown tripod options are rejected"),
		Definition->IsTripodSelectionAllowed(5, 1, 2));

	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillDefinitionDataValidationTest,
	"ProjectRPG.Skill.Definition.DataValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillDefinitionDataValidationTest::RunTest(
	const FString& Parameters)
{
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	Definition->SkillName = FText::FromString(TEXT("Validation Skill"));
	Definition->SkillTag =
		RPGGameplayTags::Player_Ability_Skill_AssultBlade;
	Definition->SkillMontage = NewObject<UAnimMontage>(Definition);
	Definition->DefaultExecutionPolicyClass =
		URPGSkillExecutionPolicy_Instant::StaticClass();

	FDataValidationContext ValidContext;
	TestEqual(TEXT("A complete definition passes editor validation"),
		Definition->IsDataValid(ValidContext),
		EDataValidationResult::Valid);

	Definition->BaseCooldown = 0.5f;
	FDataValidationContext CooldownContext;
	TestEqual(TEXT("Sub-second authored cooldown is rejected"),
		Definition->IsDataValid(CooldownContext),
		EDataValidationResult::Invalid);

	Definition->BaseCooldown = 1.0f;
	FRPGSkillChargeExecutionConfig ChargeConfig;
	Definition->DefaultExecutionConfig.InitializeAs<
		FRPGSkillChargeExecutionConfig>(ChargeConfig);
	FDataValidationContext PolicyContext;
	TestEqual(TEXT("Mismatched policy and config are rejected"),
		Definition->IsDataValid(PolicyContext),
		EDataValidationResult::Invalid);

	Definition->DefaultExecutionConfig.Reset();
	FRPGSkillTripodTier& Tier =
		Definition->TripodTiers.AddDefaulted_GetRef();
	Tier.RequiredSkillLevel = 1;
	FRPGSkillTripodOption& Option = Tier.Options.AddDefaulted_GetRef();
	Option.OverrideExecutionPolicyClass =
		URPGSkillExecutionPolicy_Charge::StaticClass();
	FDataValidationContext InvalidTripodContext;
	TestEqual(TEXT("A tripod cannot select a policy without its config"),
		Definition->IsDataValid(InvalidTripodContext),
		EDataValidationResult::Invalid);

	Option.OverrideExecutionPolicyClass =
		URPGSkillExecutionPolicy_Instant::StaticClass();
	FRPGSkillInstantExecutionConfig InstantConfig;
	Option.OverrideExecutionConfig.InitializeAs<
		FRPGSkillInstantExecutionConfig>(InstantConfig);
	FDataValidationContext ValidTripodContext;
	TestEqual(TEXT("A type-safe tripod execution override is valid"),
		Definition->IsDataValid(ValidTripodContext),
		EDataValidationResult::Valid);

	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillDefaultCooldownTest,
	"ProjectRPG.Skill.Cooldown.DefaultAndMinimum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillDefaultCooldownTest::RunTest(const FString& Parameters)
{
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	FRPGSkillRuntimeSpec RuntimeSpec;
	Definition->BuildRuntimeSpec(
		nullptr,
		FRPGSkillSaveData(),
		RuntimeSpec);

	TestEqual(
		TEXT("Every skill definition defaults to a one second cooldown"),
		RuntimeSpec.BaseCooldown,
		1.0f);
	TestEqual(
		TEXT("Default resolved cooldown is one second"),
		RuntimeSpec.GetCooldownDuration(),
		1.0f);

	RuntimeSpec.BaseCooldown = 0.0f;
	TestEqual(
		TEXT("Zero authored cooldown cannot bypass the repeat-input guard"),
		RuntimeSpec.GetCooldownDuration(),
		1.0f);

	RuntimeSpec.BaseCooldown = 4.0f;
	TestEqual(
		TEXT("Longer authored cooldowns remain intact"),
		RuntimeSpec.GetCooldownDuration(),
		4.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillSecurityProfileFreezeTest,
	"ProjectRPG.Skill.RuntimeSpec.SecurityProfileFreeze",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillSecurityProfileFreezeTest::RunTest(
	const FString& Parameters)
{
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	Definition->SecurityProfile.MaximumServerHitDistance = 1350.0f;
	Definition->SecurityProfile.MaximumTargetsPerQuery = 4;
	Definition->SecurityProfile.MaximumHitsPerActivation = 8;
	Definition->SecurityProfile.AuthorizedMovement.bEnabled = true;
	Definition->SecurityProfile.AuthorizedMovement.ExtraDistance = 750.0f;

	FRPGSkillRuntimeSpec RuntimeSpec;
	Definition->BuildRuntimeSpec(nullptr, FRPGSkillSaveData(), RuntimeSpec);
	TestEqual(TEXT("Hit range is frozen into the activation"),
		RuntimeSpec.SecurityProfile.MaximumServerHitDistance, 1350.0f);
	TestEqual(TEXT("Target cap is frozen into the activation"),
		RuntimeSpec.SecurityProfile.MaximumTargetsPerQuery, 4);
	TestEqual(TEXT("Hit budget is frozen into the activation"),
		RuntimeSpec.SecurityProfile.MaximumHitsPerActivation, 8);
	TestEqual(TEXT("Movement budget is frozen into the activation"),
		RuntimeSpec.SecurityProfile.AuthorizedMovement.ExtraDistance, 750.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillRuntimeSpecExecutionOverrideTest,
	"ProjectRPG.Skill.RuntimeSpec.ExecutionOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillRuntimeSpecExecutionOverrideTest::RunTest(
	const FString& Parameters)
{
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	Definition->DefaultExecutionPolicyClass =
		URPGSkillExecutionPolicy_Charge::StaticClass();

	FRPGSkillChargeExecutionConfig DefaultConfig;
	DefaultConfig.MaxChargeLevel = 3;
	Definition->DefaultExecutionConfig
		.InitializeAs<FRPGSkillChargeExecutionConfig>(DefaultConfig);

	FRPGSkillTripodTier& Tier = Definition->TripodTiers.AddDefaulted_GetRef();
	Tier.RequiredSkillLevel = 1;
	FRPGSkillTripodOption& Option = Tier.Options.AddDefaulted_GetRef();
	Option.OverrideExecutionPolicyClass =
		URPGSkillExecutionPolicy_Instant::StaticClass();
	FRPGSkillInstantExecutionConfig InstantConfig;
	InstantConfig.StartSection = TEXT("Instant");
	Option.OverrideExecutionConfig
		.InitializeAs<FRPGSkillInstantExecutionConfig>(InstantConfig);

	FRPGSkillSaveData SaveData;
	SaveData.SkillLevel = 1;
	SaveData.SelectedTripodIndices = {0, INDEX_NONE, INDEX_NONE};

	FRPGSkillRuntimeSpec RuntimeSpec;
	Definition->BuildRuntimeSpec(nullptr, SaveData, RuntimeSpec);

	TestEqual(
		TEXT("Tripod replaces the execution policy"),
		RuntimeSpec.ExecutionPolicyClass.Get(),
		URPGSkillExecutionPolicy_Instant::StaticClass());
	const FRPGSkillInstantExecutionConfig* ResolvedConfig =
		RuntimeSpec.ExecutionConfig.GetPtr<FRPGSkillInstantExecutionConfig>();
	TestNotNull(TEXT("Tripod replaces policy-specific config"), ResolvedConfig);
	if (ResolvedConfig)
	{
		TestEqual(
			TEXT("Resolved instant montage section"),
			ResolvedConfig->StartSection,
			FName(TEXT("Instant")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillRuntimeSpecCastingChainOverrideTest,
	"ProjectRPG.Skill.RuntimeSpec.CastingChainOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillRuntimeSpecCastingChainOverrideTest::RunTest(
	const FString& Parameters)
{
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	Definition->DefaultExecutionPolicyClass =
		URPGSkillExecutionPolicy_Casting::StaticClass();
	FRPGSkillCastingExecutionConfig CastingConfig;
	CastingConfig.CastDuration = 1.5f;
	CastingConfig.CompleteSection = TEXT("CastComplete");
	Definition->DefaultExecutionConfig.InitializeAs<
		FRPGSkillCastingExecutionConfig>(CastingConfig);

	FRPGSkillTripodTier& Tier = Definition->TripodTiers.AddDefaulted_GetRef();
	Tier.RequiredSkillLevel = 1;
	FRPGSkillTripodOption& Option = Tier.Options.AddDefaulted_GetRef();
	Option.OverrideExecutionPolicyClass =
		URPGSkillExecutionPolicy_Chain::StaticClass();
	FRPGSkillChainExecutionConfig ChainConfig;
	ChainConfig.ChainSections = {TEXT("Chain01"), TEXT("Chain02")};
	ChainConfig.LinkWindowDuration = 0.6f;
	Option.OverrideExecutionConfig.InitializeAs<
		FRPGSkillChainExecutionConfig>(ChainConfig);

	FRPGSkillSaveData SaveData;
	SaveData.SkillLevel = 1;
	SaveData.SelectedTripodIndices = {0, INDEX_NONE, INDEX_NONE};

	FRPGSkillRuntimeSpec RuntimeSpec;
	Definition->BuildRuntimeSpec(nullptr, SaveData, RuntimeSpec);
	TestEqual(
		TEXT("A tripod can replace Casting with Chain"),
		RuntimeSpec.ExecutionPolicyClass.Get(),
		URPGSkillExecutionPolicy_Chain::StaticClass());
	const FRPGSkillChainExecutionConfig* ResolvedConfig =
		RuntimeSpec.ExecutionConfig.GetPtr<FRPGSkillChainExecutionConfig>();
	TestNotNull(TEXT("Chain config is frozen into the activation"), ResolvedConfig);
	if (ResolvedConfig)
	{
		TestEqual(TEXT("Chain section count"),
			ResolvedConfig->ChainSections.Num(), 2);
		TestEqual(TEXT("Chain link window"),
			ResolvedConfig->LinkWindowDuration, 0.6f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGChargeDefinitionRuntimeTranslationTest,
	"ProjectRPG.Skill.RuntimeSpec.LegacyChargeDefinitionTranslation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGChargeDefinitionRuntimeTranslationTest::RunTest(
	const FString& Parameters)
{
	URPGSkillDefinition_Charge* Definition =
		NewObject<URPGSkillDefinition_Charge>();
	Definition->ChargeTimePerLevel = 0.75f;
	Definition->MaxChargeLevel = 4;
	Definition->MaxChargeHoldTime = 1.25f;
	Definition->MontageSections.SectionNamesToPlay.Add(0, TEXT("Charge"));
	Definition->MontageSections.SectionNamesToPlay.Add(1, TEXT("Release"));

	FRPGSkillRuntimeSpec RuntimeSpec;
	Definition->BuildRuntimeSpec(
		nullptr,
		FRPGSkillSaveData(),
		RuntimeSpec);

	TestEqual(
		TEXT("Legacy charge definitions resolve the new charge policy"),
		RuntimeSpec.ExecutionPolicyClass.Get(),
		URPGSkillExecutionPolicy_Charge::StaticClass());
	const FRPGSkillChargeExecutionConfig* Config =
		RuntimeSpec.ExecutionConfig.GetPtr<FRPGSkillChargeExecutionConfig>();
	TestNotNull(TEXT("Charge config is frozen into the runtime spec"), Config);
	if (Config)
	{
		TestEqual(TEXT("Charge levels"), Config->MaxChargeLevel, 4);
		TestEqual(TEXT("Time per level"), Config->ChargeTimePerLevel, 0.75f);
		TestEqual(TEXT("Maximum hold"), Config->MaxChargeHoldTime, 1.25f);
		TestEqual(TEXT("Charge section"), Config->ChargeSection, FName(TEXT("Charge")));
		TestEqual(TEXT("Release section"), Config->ReleaseSection, FName(TEXT("Release")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillExecutionConfigValidationTest,
	"ProjectRPG.Skill.ExecutionConfig.PolicyValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillExecutionConfigValidationTest::RunTest(
	const FString& Parameters)
{
	const URPGSkillExecutionPolicy_Holding* HoldingPolicy =
		GetDefault<URPGSkillExecutionPolicy_Holding>();
	const URPGSkillExecutionPolicy_Combo* ComboPolicy =
		GetDefault<URPGSkillExecutionPolicy_Combo>();
	const URPGSkillExecutionPolicy_Casting* CastingPolicy =
		GetDefault<URPGSkillExecutionPolicy_Casting>();
	const URPGSkillExecutionPolicy_Chain* ChainPolicy =
		GetDefault<URPGSkillExecutionPolicy_Chain>();

	FRPGSkillHoldingExecutionConfig HoldingConfig;
	HoldingConfig.HoldDuration = 1.0f;
	HoldingConfig.PerfectZoneStartTime = 0.75f;
	HoldingConfig.PerfectZoneEndTime = 1.0f;
	HoldingConfig.SuccessSection = TEXT("HoldingSuccess");
	FInstancedStruct HoldingStruct;
	HoldingStruct.InitializeAs<FRPGSkillHoldingExecutionConfig>(
		HoldingConfig);

	FText ValidationError;
	TestTrue(
		TEXT("Valid holding config is accepted"),
		HoldingPolicy->ValidateExecutionConfig(
			HoldingStruct,
			ValidationError));

	HoldingConfig.PerfectZoneStartTime = 1.1f;
	HoldingStruct.InitializeAs<FRPGSkillHoldingExecutionConfig>(
		HoldingConfig);
	TestFalse(
		TEXT("Holding perfect zone outside duration is rejected"),
		HoldingPolicy->ValidateExecutionConfig(
			HoldingStruct,
			ValidationError));

	FRPGSkillComboExecutionConfig ComboConfig;
	ComboConfig.ComboSections = {
		TEXT("Combo01"),
		TEXT("Combo02"),
		TEXT("Combo03")};
	FInstancedStruct ComboStruct;
	ComboStruct.InitializeAs<FRPGSkillComboExecutionConfig>(ComboConfig);
	TestTrue(
		TEXT("Valid combo config is accepted"),
		ComboPolicy->ValidateExecutionConfig(
			ComboStruct,
			ValidationError));

	ComboConfig.ComboSections.Add(NAME_None);
	ComboStruct.InitializeAs<FRPGSkillComboExecutionConfig>(ComboConfig);
	TestFalse(
		TEXT("Combo config with an empty section is rejected"),
		ComboPolicy->ValidateExecutionConfig(
			ComboStruct,
			ValidationError));

	TestFalse(
		TEXT("Policy/config type mismatch is rejected"),
		ComboPolicy->ValidateExecutionConfig(
			HoldingStruct,
			ValidationError));

	FRPGSkillCastingExecutionConfig CastingConfig;
	CastingConfig.CastDuration = 1.25f;
	CastingConfig.CompleteSection = TEXT("CastComplete");
	FInstancedStruct CastingStruct;
	CastingStruct.InitializeAs<FRPGSkillCastingExecutionConfig>(CastingConfig);
	TestTrue(
		TEXT("Valid casting config is accepted"),
		CastingPolicy->ValidateExecutionConfig(
			CastingStruct,
			ValidationError));

	CastingConfig.CastDuration = 61.0f;
	CastingStruct.InitializeAs<FRPGSkillCastingExecutionConfig>(CastingConfig);
	TestFalse(
		TEXT("Casting config rejects an excessive server lifetime"),
		CastingPolicy->ValidateExecutionConfig(
			CastingStruct,
			ValidationError));

	FRPGSkillChainExecutionConfig ChainConfig;
	ChainConfig.ChainSections = {TEXT("Chain01"), TEXT("Chain02")};
	ChainConfig.LinkWindowDuration = 0.75f;
	FInstancedStruct ChainStruct;
	ChainStruct.InitializeAs<FRPGSkillChainExecutionConfig>(ChainConfig);
	TestTrue(
		TEXT("Valid chain config is accepted"),
		ChainPolicy->ValidateExecutionConfig(
			ChainStruct,
			ValidationError));

	ChainConfig.LinkWindowDuration = 5.1f;
	ChainStruct.InitializeAs<FRPGSkillChainExecutionConfig>(ChainConfig);
	TestFalse(
		TEXT("Chain config rejects an excessive input window"),
		ChainPolicy->ValidateExecutionConfig(
			ChainStruct,
			ValidationError));

	ChainConfig.LinkWindowDuration = 0.75f;
	ChainConfig.ChainSections.SetNum(1);
	ChainStruct.InitializeAs<FRPGSkillChainExecutionConfig>(ChainConfig);
	TestFalse(
		TEXT("Chain config requires at least two sections"),
		ChainPolicy->ValidateExecutionConfig(
			ChainStruct,
			ValidationError));

	TestFalse(
		TEXT("Casting policy rejects Chain config"),
		CastingPolicy->ValidateExecutionConfig(
			ChainStruct,
			ValidationError));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillAuthoredMontageValidationTest,
	"ProjectRPG.Skill.RuntimeSpec.AuthoredMontageValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillAuthoredMontageValidationTest::RunTest(
	const FString& Parameters)
{
	UAnimMontage* Montage = NewObject<UAnimMontage>();
	// AddAnimCompositeSection is editor-only in UE 5.8.  The runtime-spec
	// validation only needs authored section names, so construct the minimal
	// runtime representation directly for this Development automation test.
	auto AddRuntimeSection = [Montage](const FName SectionName)
	{
		FCompositeSection& Section = Montage->CompositeSections.Emplace_GetRef();
		Section.SectionName = SectionName;
		Section.SetTime(0.0f);
	};
	AddRuntimeSection(TEXT("CastStart"));
	AddRuntimeSection(TEXT("CastComplete"));
	AddRuntimeSection(TEXT("Chain01"));
	AddRuntimeSection(TEXT("Chain02"));

	FText ValidationError;
	FRPGSkillRuntimeSpec RuntimeSpec;
	RuntimeSpec.Montage = Montage;

	FRPGSkillCastingExecutionConfig CastingConfig;
	CastingConfig.CastingSection = TEXT("CastStart");
	CastingConfig.CompleteSection = TEXT("CastComplete");
	RuntimeSpec.ExecutionConfig.InitializeAs<
		FRPGSkillCastingExecutionConfig>(CastingConfig);
	const URPGSkillExecutionPolicy_Casting* CastingPolicy =
		GetDefault<URPGSkillExecutionPolicy_Casting>();
	TestTrue(
		TEXT("Casting accepts authored montage sections"),
		CastingPolicy->ValidateRuntimeSpecData(RuntimeSpec, ValidationError));

	CastingConfig.CompleteSection = TEXT("MissingSection");
	RuntimeSpec.ExecutionConfig.InitializeAs<
		FRPGSkillCastingExecutionConfig>(CastingConfig);
	TestFalse(
		TEXT("Casting rejects a missing completion section before activation"),
		CastingPolicy->ValidateRuntimeSpecData(RuntimeSpec, ValidationError));

	FRPGSkillChainExecutionConfig ChainConfig;
	ChainConfig.ChainSections = {TEXT("Chain01"), TEXT("Chain02")};
	RuntimeSpec.ExecutionConfig.InitializeAs<
		FRPGSkillChainExecutionConfig>(ChainConfig);
	const URPGSkillExecutionPolicy_Chain* ChainPolicy =
		GetDefault<URPGSkillExecutionPolicy_Chain>();
	TestTrue(
		TEXT("Chain accepts authored montage sections"),
		ChainPolicy->ValidateRuntimeSpecData(RuntimeSpec, ValidationError));

	ChainConfig.ChainSections[1] = TEXT("MissingSection");
	RuntimeSpec.ExecutionConfig.InitializeAs<
		FRPGSkillChainExecutionConfig>(ChainConfig);
	TestFalse(
		TEXT("Chain rejects a missing link section before activation"),
		ChainPolicy->ValidateRuntimeSpecData(RuntimeSpec, ValidationError));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillRuntimeSpecTargetingOverrideTest,
	"ProjectRPG.Skill.RuntimeSpec.TargetingOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillRuntimeSpecTargetingOverrideTest::RunTest(
	const FString& Parameters)
{
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	Definition->DefaultTargetingPolicyClass =
		URPGSkillTargetingPolicy_CameraDirection::StaticClass();
	FRPGSkillCameraDirectionTargetingConfig CameraConfig;
	CameraConfig.MaxRange = 5000.0f;
	Definition->DefaultTargetingConfig.InitializeAs<
		FRPGSkillCameraDirectionTargetingConfig>(CameraConfig);

	FRPGSkillTripodTier& Tier = Definition->TripodTiers.AddDefaulted_GetRef();
	Tier.RequiredSkillLevel = 1;
	FRPGSkillTripodOption& Option = Tier.Options.AddDefaulted_GetRef();
	Option.OverrideTargetingPolicyClass =
		URPGSkillTargetingPolicy_SoftTarget::StaticClass();
	FRPGSkillSoftTargetingConfig SoftTargetConfig;
	SoftTargetConfig.MaxRange = 900.0f;
	SoftTargetConfig.AssistAngleDegrees = 30.0f;
	Option.OverrideTargetingConfig.InitializeAs<
		FRPGSkillSoftTargetingConfig>(SoftTargetConfig);

	FRPGSkillSaveData SaveData;
	SaveData.SkillLevel = 1;
	SaveData.SelectedTripodIndices = {0, INDEX_NONE, INDEX_NONE};

	FRPGSkillRuntimeSpec RuntimeSpec;
	Definition->BuildRuntimeSpec(nullptr, SaveData, RuntimeSpec);

	TestEqual(
		TEXT("Tripod replaces only the targeting policy"),
		RuntimeSpec.TargetingPolicyClass.Get(),
		URPGSkillTargetingPolicy_SoftTarget::StaticClass());
	const FRPGSkillSoftTargetingConfig* ResolvedConfig =
		RuntimeSpec.TargetingConfig.GetPtr<FRPGSkillSoftTargetingConfig>();
	TestNotNull(TEXT("Tripod freezes the soft-target config"), ResolvedConfig);
	if (ResolvedConfig)
	{
		TestEqual(
			TEXT("Resolved assist range"),
			ResolvedConfig->MaxRange,
			900.0f);
		TestEqual(
			TEXT("Resolved assist angle"),
			ResolvedConfig->AssistAngleDegrees,
			30.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillTargetingPolicyValidationTest,
	"ProjectRPG.Skill.Targeting.PolicyValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillTargetingPolicyValidationTest::RunTest(
	const FString& Parameters)
{
	const URPGSkillTargetingPolicy_CameraDirection* CameraPolicy =
		GetDefault<URPGSkillTargetingPolicy_CameraDirection>();
	const URPGSkillTargetingPolicy_SoftTarget* SoftTargetPolicy =
		GetDefault<URPGSkillTargetingPolicy_SoftTarget>();
	const URPGSkillTargetingPolicy_GroundPoint* GroundPolicy =
		GetDefault<URPGSkillTargetingPolicy_GroundPoint>();

	FText ValidationError;
	FRPGSkillCameraDirectionTargetingConfig CameraConfig;
	FInstancedStruct CameraStruct;
	CameraStruct.InitializeAs<FRPGSkillCameraDirectionTargetingConfig>(
		CameraConfig);
	TestTrue(
		TEXT("Valid camera direction config is accepted"),
		CameraPolicy->ValidateTargetingConfig(
			CameraStruct,
			ValidationError));

	FRPGSkillSoftTargetingConfig SoftTargetConfig;
	FInstancedStruct SoftTargetStruct;
	SoftTargetStruct.InitializeAs<FRPGSkillSoftTargetingConfig>(
		SoftTargetConfig);
	TestTrue(
		TEXT("Valid soft-target config is accepted"),
		SoftTargetPolicy->ValidateTargetingConfig(
			SoftTargetStruct,
			ValidationError));

	SoftTargetConfig.AngleScoreWeight = 0.0f;
	SoftTargetConfig.DistanceScoreWeight = 0.0f;
	SoftTargetStruct.InitializeAs<FRPGSkillSoftTargetingConfig>(
		SoftTargetConfig);
	TestFalse(
		TEXT("Soft-target config rejects zero score weights"),
		SoftTargetPolicy->ValidateTargetingConfig(
			SoftTargetStruct,
			ValidationError));

	SoftTargetConfig.AngleScoreWeight = 0.75f;
	SoftTargetConfig.DistanceScoreWeight = 0.25f;
	SoftTargetConfig.ServerAssistConeToleranceDegrees = 46.0f;
	SoftTargetStruct.InitializeAs<FRPGSkillSoftTargetingConfig>(
		SoftTargetConfig);
	TestFalse(
		TEXT("Soft-target server cone rejects excessive validation grace"),
		SoftTargetPolicy->ValidateTargetingConfig(
			SoftTargetStruct,
			ValidationError));

	FRPGSkillGroundPointTargetingConfig GroundConfig;
	FInstancedStruct GroundStruct;
	GroundStruct.InitializeAs<FRPGSkillGroundPointTargetingConfig>(
		GroundConfig);
	TestTrue(
		TEXT("Valid ground-point config is accepted"),
		GroundPolicy->ValidateTargetingConfig(
			GroundStruct,
			ValidationError));

	GroundConfig.ServerAimToleranceDegrees = 181.0f;
	GroundStruct.InitializeAs<FRPGSkillGroundPointTargetingConfig>(
		GroundConfig);
	TestFalse(
		TEXT("Network validation rejects aim tolerance above 180 degrees"),
		GroundPolicy->ValidateTargetingConfig(
			GroundStruct,
			ValidationError));

	TestTrue(
		TEXT("Centered nearby candidates score above cone-edge candidates"),
		FRPGSkillTargetingMath::CalculateSoftTargetScore(
			0.0f, 100.0f, 45.0f, 1000.0f, 0.75f, 0.25f) >
		FRPGSkillTargetingMath::CalculateSoftTargetScore(
			45.0f, 100.0f, 45.0f, 1000.0f, 0.75f, 0.25f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillTargetDataCodecTest,
	"ProjectRPG.Skill.Targeting.TargetDataCodec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillTargetDataCodecTest::RunTest(const FString& Parameters)
{
	FRPGSkillTargetResult Original;
	Original.bIsValid = true;
	Original.SourceLocation = FVector(100.0, -50.0, 25.0);
	Original.TargetLocation = FVector(900.0, 150.0, 75.0);
	Original.AimDirection =
		(Original.TargetLocation - Original.SourceLocation).GetSafeNormal();
	Original.HitQueryTransform = FTransform(
		Original.AimDirection.Rotation(),
		Original.SourceLocation);
	Original.bOrientSourceToAim = true;

	const FGameplayAbilityTargetDataHandle Encoded =
		FRPGSkillTargetDataCodec::Encode(Original);
	TestTrue(TEXT("Valid target result encodes to GAS TargetData"),
		Encoded.IsValid(0));

	FRPGSkillTargetResult Decoded;
	TestTrue(TEXT("Standard GAS TargetData decodes to a skill target"),
		FRPGSkillTargetDataCodec::Decode(Encoded, Decoded));
	TestEqual(TEXT("Source location survives the codec"),
		Decoded.SourceLocation, Original.SourceLocation);
	TestEqual(TEXT("Target location survives the codec"),
		Decoded.TargetLocation, Original.TargetLocation);
	TestTrue(TEXT("Aim direction is rebuilt from spatial data"),
		Decoded.AimDirection.Equals(Original.AimDirection, KINDA_SMALL_NUMBER));
	TestFalse(TEXT("Client orientation flags are not trusted"),
		Decoded.bOrientSourceToAim);

	FRPGSkillTargetResult MalformedResult;
	TestFalse(TEXT("Empty TargetData is rejected"),
		FRPGSkillTargetDataCodec::Decode(
			FGameplayAbilityTargetDataHandle(),
			MalformedResult));

	return true;
}

#endif
