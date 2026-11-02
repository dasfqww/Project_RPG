#include "CoreMinimal.h"
#include <limits>
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Tests/RPGSkillDamageSecurityTestFixture.h"
#include "Tests/RPGCombatControlTestFixture.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "Ability/Equipment/RPGEquipmentHitAbility.h"
#include "Ability/Skill/RPGGreatSwordResolveAbility.h"
#include "Animation/AnimInstance.h"
#include "Character/RPGPlayer.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "DataAsset/StartUpData/DataAsset_StartUpDataBase.h"
#include "PlayerClass/RPGPlayerClassComponent.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "Skill/RPGSkillDefinition.h"
#include "GameplayEffect/GE_CoolDown.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/UnrealType.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/Combat/RPGShieldComponent.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Item/Definition/RPGItemDefinitionFragments.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "Skill/RPGApprovedSkillHitEvent.h"
#include "Manager/DataManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "AIController.h"
#include "Misc/AutomationTest.h"
#include "RPGGameplayTags.h"
#include "UObject/StrongObjectPtr.h"

namespace RPGEquipmentHitTests
{
struct FWorld
{
	UWorld* World = nullptr;
	uint64 PreviousFrame = GFrameCounter;
	FWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
	}
	~FWorld() { if (World) RPGTestWorld::Destroy(World); GFrameCounter = PreviousFrame; }
	void Tick(float Delta = 1.f / 60.f) { ++GFrameCounter; World->Tick(LEVELTICK_All, Delta); }
};
FRPGItemRecord Record(const URPGItemDefinition& Definition, const FRPGItemOwnerRef& Owner, const FGuid Item = FGuid::NewGuid())
{
	FRPGItemInstanceState State;
	FRPGEquipmentProgressionState Growth; Growth.GrowthBandId = TEXT("FirstSlice"); Growth.RuleVersion = 1; Growth.Level = 0;
	FRPGItemInstanceState::TryRestore(Item, 0, 1, {}, {}, State, Growth);
	FRPGItemLocation Location; Location.ContainerType = ERPGItemContainerType::Equipment;
	Location.ContainerId = Owner.OwnerId; Location.SlotIndex = static_cast<int32>(EEquipmentSlotType::Weapon_Primary_R);
	FRPGItemRecordMetadata Metadata; Metadata.BindState = ERPGItemBindState::CharacterBound;
	FRPGItemRecord Result;
	FRPGItemRecord::TryRestore(Definition.GetPrimaryAssetId(), Definition.GetDefinitionVersion(), Owner, Location, State, 1,
		ERPGItemLifecycleState::Active, Metadata, Result);
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentShieldLifecycleTest, "ProjectRPG.Item.EquipmentHitEffects.ShieldLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentShieldLifecycleTest::RunTest(const FString&)
{
	RPGEquipmentHitTests::FWorld F;
	auto* Actor = F.World->SpawnActor<ARPGSkillDamageSecurityTestPawn>();
	if (!TestNotNull(TEXT("Actor"), Actor)) return false;
	auto* Shield = URPGShieldComponent::FindOrCreate(Actor);
	if (!TestNotNull(TEXT("Shield"), Shield)) return false;
	const FGuid Source = FGuid::NewGuid();
	TestTrue(TEXT("Grant 5% of 1000"), Shield->GrantOrRefresh(Source, .05f, 3.f));
	TestEqual(TEXT("Amount"), Shield->GetShieldState().Remaining, 50.f);
	TestEqual(TEXT("Absorbs small hit"), Shield->AbsorbDamage(30.f), 0.f);
	TestEqual(TEXT("Remaining"), Shield->GetShieldState().Remaining, 20.f);
	TestTrue(TEXT("Refresh replaces instead of accumulating"), Shield->GrantOrRefresh(Source, .05f, 3.f));
	TestEqual(TEXT("Refresh total"), Shield->GetShieldState().Remaining, 50.f);
	Actor->ASC()->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 2000.f);
	TestEqual(TEXT("Increased maximum does not refill"), Shield->GetShieldState().Remaining, 50.f);
	Actor->ASC()->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 600.f);
	TestEqual(TEXT("Decreased maximum caps amount"), Shield->GetShieldState().Remaining, 30.f);
	Actor->ASC()->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 1000.f);
	TestEqual(TEXT("Reversing maximum change does not restore lost amount"), Shield->GetShieldState().Remaining, 30.f);
	TestEqual(TEXT("Overflow goes to health"), Shield->AbsorbDamage(80.f), 50.f);
	TestEqual(TEXT("Exhausted"), Shield->GetShieldState().Remaining, 0.f);
	Shield->GrantOrRefresh(Source, .05f, 3.f);
	auto* Damage = NewObject<UGameplayEffect>(); Damage->DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo Modifier; Modifier.Attribute = URPGAttributeSet::GetTakeDamageAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive; Modifier.ModifierMagnitude = FScalableFloat(70.f); Damage->Modifiers.Add(Modifier);
	Actor->ASC()->ApplyGameplayEffectToSelf(Damage, 1.f, Actor->ASC()->MakeEffectContext());
	TestEqual(TEXT("Real damage route subtracts only unabsorbed 20"), Actor->Attributes()->GetCurrentHealth(), 980.f);
	Shield->GrantOrRefresh(Source, .05f, .1f);
	for (int32 Frame = 0; Frame < 20; ++Frame) F.Tick();
	TestEqual(TEXT("Expired automatically"), Shield->GetShieldState().Remaining, 0.f);
	Shield->GrantOrRefresh(Source, .05f, 3.f);
	Actor->ASC()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
	TestEqual(TEXT("Death clears shield"), Shield->GetShieldState().Remaining, 0.f);
	TestFalse(TEXT("Dead actors cannot acquire shields"), Shield->GrantOrRefresh(Source, .05f, 3.f));
	Actor->ASC()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 500.f);
	TestEqual(TEXT("Revival does not regrant"), Shield->GetShieldState().Remaining, 0.f);
	TestFalse(TEXT("Invalid duration rejected"), Shield->GrantOrRefresh(Source, .05f, -1.f));
	TestFalse(TEXT("Nonfinite amount rejected"), Shield->GrantOrRefresh(Source, std::numeric_limits<float>::quiet_NaN(), 3.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentHitDispatchTest, "ProjectRPG.Item.EquipmentHitEffects.ApprovedDispatchAndOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentHitDispatchTest::RunTest(const FString&)
{
	using namespace RPGEquipmentHitTests;
	FWorld F;
	auto Pawn = [&](FVector Location, uint8 Team)
	{
		FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* P = F.World->SpawnActor<ARPGSkillDamageSecurityTestPawn>(Location, FRotator::ZeroRotator, Params);
		auto* C = F.World->SpawnActor<AAIController>(); C->SetGenericTeamId(FGenericTeamId(Team)); C->Possess(P); return P;
	};
	auto* Actor = Pawn(FVector::ZeroVector, 1); auto* Target = Pawn(FVector(200, 0, 0), 2);
	auto* Equipment = NewObject<URPGAuthoritativeEquipmentComponent>(Actor); Actor->AddInstanceComponent(Equipment); Equipment->RegisterComponent();
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
	TStrongObjectPtr<UDataManager> Data(NewObject<UDataManager>(Instance.Get())); Data->RefreshItemCache();
	const FPrimaryAssetId Id(TEXT("RPGItemDefinition"), TEXT("GameItem.Equipment.Progression.Target.EnduranceGreatSword"));
	const auto* Definition = Data->FindNativeItemDefinition(Id);
	if (!TestNotNull(TEXT("Authored item"), Definition)) return false;
	auto* Trigger = const_cast<URPGItemShieldOnHitDefinitionFragment*>(Definition->FindFragmentByClass<URPGItemShieldOnHitDefinitionFragment>());
	if (!TestNotNull(TEXT("Authored effect configuration"), Trigger)) return false;
	// Closed native fixture: exercise a second skill by changing only loaded data,
	// never saving it. Production assets retain their authored charge condition.
	TGuardValue<FGameplayTag> SkillTag(Trigger->RequiredSkillTag, RPGGameplayTags::Player_Ability_Skill_AssultBlade);
	TGuardValue<bool> Charge(Trigger->bRequireMaximumCharge, false);
	FRPGItemOwnerRef Owner; Owner.Type = ERPGItemOwnerType::Character; Owner.OwnerId = FGuid::NewGuid().ToString();
	const auto Sword = Record(*Definition, Owner);
	if (!TestTrue(TEXT("Actual equipment grant"), Equipment->Reconcile(Owner, {Sword}, *Data))) return false;
	const auto* Passive = Actor->ASC()->FindAbilitySpecFromClass(URPGEquipmentShieldOnHitAbility::StaticClass());
	if (!TestTrue(TEXT("AbilitySet activates passive"), Passive && Passive->IsActive())) return false;
	const auto Handle = Actor->ASC()->GiveAbility(FGameplayAbilitySpec(URPGSkillDamageSecurityTestAbility::StaticClass(), 1));
	auto* Skill = Cast<URPGSkillDamageSecurityTestAbility>(Actor->ASC()->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
	FRPGSkillRuntimeSpec Spec; Spec.SkillTag = RPGGameplayTags::Player_Ability_Skill_AssultBlade;
	Spec.ExecutionPolicyClass = URPGSkillExecutionPolicy::StaticClass(); Spec.BaseDamage = 10.f;
	Spec.TargetingProfile.Shape.Type = ERPGHitQueryShape::Sector; Spec.TargetingProfile.Shape.Radius = 500.f;
	Spec.TargetingProfile.Shape.AngleDegrees = 90.f; Spec.TargetingProfile.Shape.HalfHeight = 100.f;
	Spec.TargetingProfile.Filter.TeamRule = ERPGHitQueryTeamRule::HostileOnly;
	Spec.SecurityProfile.MaximumServerHitDistance = 600.f; Spec.SecurityProfile.MaximumTargetsPerQuery = 8;
	Spec.SecurityProfile.MaximumHitsPerActivation = 8; Spec.SecurityProfile.MaximumDamagePerHit = 100.f;
	Spec.SecurityProfile.MaximumFinalDamagePerHit = 500.f;
	F.Tick(.01f);
	auto Start = [&]
	{
		if (Skill->IsActive()) Skill->Finish();
		Skill->Configure(Spec, RPGGameplayTags::Player_Event_AOE);
		Actor->ASC()->TryActivateAbility(Handle);
		Equipment->GetEffectContext(EEquipmentSlotType::Weapon_Primary_R, Skill->CastEquipmentEffectContext);
		Skill->CastEquipmentEffectLifetime = FRPGApprovedHitContext(Actor, Actor);
	};
	auto Hit = [&]
	{
		TArray<FRPGHitQueryResult> Hits; int32 Count = 0; FText Error;
		return Skill->ExecuteAuthorizedSkillDamage(URPGSkillDamageSecurityTestEffect::StaticClass(), 10.f, {}, Hits, Count, Error);
	};
	Start();
	TestTrue(TEXT("Actual server damage produces notification"), Hit());
	auto* Shield = Actor->FindComponentByClass<URPGShieldComponent>();
	if (!TestNotNull(TEXT("Passive produced shield"), Shield)) return false;
	TestEqual(TEXT("Data-configured amount"), Shield->GetShieldState().Remaining, 50.f);
	Shield->AbsorbDamage(10.f);
	TestTrue(TEXT("Second approved hit is possible"), Hit());
	TestEqual(TEXT("Same cast cannot refill again"), Shield->GetShieldState().Remaining, 40.f);
	FGameplayEventData Forged; Forged.OptionalObject = NewObject<URPGApprovedSkillHitEvent>();
	Actor->ASC()->HandleGameplayEvent(URPGApprovedSkillHitEvent::EventTag(), &Forged);
	TestEqual(TEXT("Empty forged payload cannot proc"), Shield->GetShieldState().Remaining, 40.f);
	Equipment->SetActiveEquipState(EEquipState::WeaponSet_Secondary);
	Equipment->SetActiveEquipState(EEquipState::WeaponSet_Primary);
	TestEqual(TEXT("Stowing clears owned shield"), Shield->GetShieldState().Remaining, 0.f);
	TestTrue(TEXT("Old cast damage still valid after returning"), Hit());
	TestEqual(TEXT("Returning to weapon cannot restore old cast proc"), Shield->GetShieldState().Remaining, 0.f);
	Start(); TestTrue(TEXT("New cast damage"), Hit());
	TestEqual(TEXT("New cast can proc"), Shield->GetShieldState().Remaining, 50.f);
	const auto OldContext = Skill->CastEquipmentEffectContext;
	TestTrue(TEXT("Unequip"), Equipment->Reconcile(Owner, {}, *Data));
	TestEqual(TEXT("Removing AbilitySet clears effect"), Shield->GetShieldState().Remaining, 0.f);
	TestTrue(TEXT("Re-equip exact same item"), Equipment->Reconcile(Owner, {Sword}, *Data));
	FRPGEquipmentEffectContext NewContext; Equipment->GetEffectContext(EEquipmentSlotType::Weapon_Primary_R, NewContext);
	TestFalse(TEXT("Same ID receives a new grant lifetime"), OldContext.Matches(NewContext));
	Skill->Finish();
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentResolveHitTest, "ProjectRPG.Item.EquipmentHitEffects.ResolveClassGateAndFinalBerserk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentResolveHitTest::RunTest(const FString&)
{
	using namespace RPGEquipmentHitTests;
	FWorld F;
	auto* Player = F.World->SpawnActor<ARPGPlayer>();
	auto* PC = F.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Player"), Player) || !TestNotNull(TEXT("Controller"), PC)) return false;
	auto* Startup = FindFProperty<FSoftObjectProperty>(ARPGBaseCharacter::StaticClass(), TEXT("CharacterStartUpData"));
	if (!TestNotNull(TEXT("Startup"), Startup)) return false;
	*Startup->ContainerPtrToValuePtr<TSoftObjectPtr<UDataAsset_StartUpDataBase>>(Player) = NewObject<UDataAsset_StartUpDataBase>(Player);
	PC->SetAsLocalPlayerController(); PC->Possess(Player); Player->GetCharacterMovement()->SetMovementMode(MOVE_None);
	auto* ASC = Player->GetRPGAbilitySystemComponent(); ASC->InitAbilityActorInfo(Player, Player);
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 1000.f);
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 1000.f);
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxIdentityGaugeAttribute(), 100.f);
	auto* PlayerClass = LoadClass<ARPGPlayer>(nullptr, TEXT("/Game/Blueprints/Character/Player/BP_Player.BP_Player_C"));
	if (!TestNotNull(TEXT("Player content"), PlayerClass)) return false;
	Player->GetMesh()->SetSkeletalMesh(PlayerClass->GetDefaultObject<ARPGPlayer>()->GetMesh()->GetSkeletalMeshAsset());
	Player->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass()); ASC->RefreshAbilityActorInfo();
	// This isolated world has no game-instance subsystem to spawn streamed item
	// actors. Supply the native combat actor to its fixture snapshot explicitly.
	auto* Weapon = F.World->SpawnActor<ARPGPlayerWeapon>();
	auto* WeaponType = FindFProperty<FEnumProperty>(ARPGPlayerWeapon::StaticClass(), TEXT("WeaponType"));
	if (!TestNotNull(TEXT("Combat weapon"), Weapon) || !TestNotNull(TEXT("Weapon type"), WeaponType)) return false;
	WeaponType->GetUnderlyingProperty()->SetIntPropertyValue(WeaponType->ContainerPtrToValuePtr<void>(Weapon), static_cast<int64>(ERPGGladiatorWeaponType::GreatSword));
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
	TStrongObjectPtr<UDataManager> Data(NewObject<UDataManager>(Instance.Get())); Data->RefreshItemCache();
	const auto* Item = Data->FindNativeItemDefinition({TEXT("RPGItemDefinition"), TEXT("GameItem.Equipment.Progression.Target.CirculationGreatSword")});
	if (!TestNotNull(TEXT("Authored circulation definition"), Item)) return false;
	FRPGItemOwnerRef ItemOwner; ItemOwner.Type = ERPGItemOwnerType::Character; ItemOwner.OwnerId = FGuid::NewGuid().ToString();
	auto* Equipment = Player->FindComponentByClass<URPGAuthoritativeEquipmentComponent>();
	if (!TestTrue(TEXT("Actual circulation grant"), Equipment->Reconcile(ItemOwner, {Record(*Item, ItemOwner)}, *Data))) return false;
	FlushAsyncLoading();
	auto* SnapshotProperty = FindFProperty<FArrayProperty>(URPGAuthoritativeEquipmentComponent::StaticClass(), TEXT("EquipmentSnapshots"));
	if (!TestNotNull(TEXT("Equipment snapshot storage"), SnapshotProperty)) return false;
	auto& Snapshots = *SnapshotProperty->ContainerPtrToValuePtr<TArray<FRPGEquipmentSnapshot>>(Equipment);
	if (!TestEqual(TEXT("One equipped item"), Snapshots.Num(), 1)) return false;
	Snapshots[0].EquippedActor = Weapon;
	auto* Class = LoadObject<URPGPlayerClassDefinition>(nullptr, TEXT("/Game/Blueprints/GameData/PlayerClass/DA_Class_GreatSwordWarrior.DA_Class_GreatSwordWarrior"));
	if (!TestNotNull(TEXT("Class"), Class)) return false;
	auto* Growth = Player->GetPlayerSkillComponent();
	auto* ClassOwner = Player->FindComponentByClass<URPGPlayerClassComponent>();
	auto* Slots = Player->FindComponentByClass<UQuickSlotComponent>();
	FRPGPlayerSkillRestoreData Restore; Restore.ClassId = Class->GetPrimaryAssetId(); Restore.TotalSP = 1000; Restore.bIdentityUnlocked = true;
	for (const auto& Entry : Class->Skills) { auto& S = Restore.AcquiredSkills.AddDefaulted_GetRef(); S.SkillTag = Entry.Definition->SkillTag; }
	const auto Generation = Growth->BeginClassSkillRestore(Restore.ClassId); FText Error;
	if (!TestTrue(TEXT("Restore class"), Growth->CompleteClassSkillRestore(Generation, Class, Restore, Error))) return false;
	ClassOwner->NotifyAbilitySystemReady(*ASC, 1);
	if (!TestTrue(TEXT("Register spin"), Slots->InitializeSkillSlotsFromRecord(Generation, {RPGGameplayTags::Player_Ability_Skill_WhirlWind}, Error))) return false;
	const auto* SpinDefinition = Class->FindSkill(RPGGameplayTags::Player_Ability_Skill_WhirlWind)->Definition.Get();
	auto* SpinSpec = ASC->FindAbilitySpecFromClass(Class->FindSkill(RPGGameplayTags::Player_Ability_Skill_WhirlWind)->AbilityClass);
	auto* Spin = SpinSpec ? Cast<URPGGameplayAbility_SkillContainer>(SpinSpec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("Spin"), Spin)) return false;
	const auto* Passive = ASC->FindAbilitySpecFromClass(URPGEquipmentResolveOnHitAbility::StaticClass());
	if (!TestTrue(TEXT("Equipment passive is active"), Passive && Passive->IsActive())) return false;
	auto MakeConfirmation = [&]
	{
		auto* E = NewObject<URPGApprovedSkillHitEvent>(); E->Skill = Spin; E->Avatar = Player;
		E->Lifetime = FRPGApprovedHitContext(Player, Player);
		Equipment->GetEffectContext(EEquipmentSlotType::Weapon_Primary_R, E->Equipment);
		E->SkillTag = SpinDefinition->SkillTag; E->ActivationSerial = Spin->GetSkillActivationSerial(); E->bFinisher = true;
		return E;
	};
	auto Publish = [&](const URPGApprovedSkillHitEvent* E)
	{
		FGameplayEventData P; P.OptionalObject = E; ASC->HandleGameplayEvent(URPGApprovedSkillHitEvent::EventTag(), &P);
	};
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentIdentityGaugeAttribute(), 90.f);
	Slots->BeginUseSkillSlot(0);
	if (!TestTrue(TEXT("Normal spin is active"), Spin->IsActive())) return false;
	TStrongObjectPtr<URPGApprovedSkillHitEvent> Event(MakeConfirmation());
	Publish(Event.Get());
	TestEqual(TEXT("Bonus caps at 100"), ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()), 100.f);
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentIdentityGaugeAttribute(), 10.f);
	Publish(Event.Get());
	TestEqual(TEXT("Duplicate confirmation does not grant again"), ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()), 10.f);
	ASC->CancelAbilityHandle(SpinSpec->Handle); Slots->EndUseSkillSlot(0);
	// A new real activation must respect the existing server input interval.
	for (int32 Frame = 0; Frame < 6; ++Frame) F.Tick();
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentIdentityGaugeAttribute(), 100.f);
	const auto* Identity = ASC->FindAbilitySpecFromClass(URPGGreatSwordResolveAbility::StaticClass());
	if (!TestTrue(TEXT("Enter berserk"), Identity && ASC->TryActivateAbility(Identity->Handle))) return false;
	ASC->CancelAbilityHandle(Identity->Handle); // Ends presentation, not the opportunity.
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentIdentityGaugeAttribute(), 50.f);
	Slots->BeginUseSkillSlot(0);
	if (!TestTrue(TEXT("Final berserk spin is active"), Spin->IsActive())) return false;
	TestTrue(TEXT("Cast retains no-gain flag after exhaustion"), Spin->SuppressesResolveGain());
	TestFalse(TEXT("Opportunity is exhausted"), URPGGreatSwordResolveAbility::IsBerserk(Player));
	Publish(MakeConfirmation());
	TestEqual(TEXT("Final berserk finisher cannot recover gauge"), ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()), 0.f);
	ASC->CancelAbilityHandle(SpinSpec->Handle); Slots->EndUseSkillSlot(0);
	return true;
}
#endif
