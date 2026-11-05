#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Animation/AnimInstance.h"
#include "Attribute/RPGAttributeSet.h"
#include "Ability/Skill/RPGGreatSwordResolveAbility.h"
#include "Character/RPGPlayer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "DataAsset/StartUpData/DataAsset_StartUpDataBase.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect/GE_CoolDown.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "PlayerClass/RPGPlayerClassComponent.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillDefinition.h"
#include "RPGGameplayTags.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGGreatSwordBerserkTest,
    "ProjectRPG.Skill.GreatSword.BerserkLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGGreatSwordBerserkTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
    if (!TestNotNull(TEXT("World"), World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    const uint64 PreviousFrame = GFrameCounter;
    ON_SCOPE_EXIT { RPGTestWorld::Destroy(World); GFrameCounter = PreviousFrame; };
    World->InitializeActorsForPlay(FURL()); World->BeginPlay();
    auto* Player = World->SpawnActor<ARPGPlayer>();
    auto* PC = World->SpawnActor<APlayerController>();
    auto* Startup = FindFProperty<FSoftObjectProperty>(ARPGBaseCharacter::StaticClass(), TEXT("CharacterStartUpData"));
    if (!TestNotNull(TEXT("Player"), Player) || !TestNotNull(TEXT("Startup property"), Startup)) return false;
    *Startup->ContainerPtrToValuePtr<TSoftObjectPtr<UDataAsset_StartUpDataBase>>(Player) = NewObject<UDataAsset_StartUpDataBase>(Player);
    PC->SetAsLocalPlayerController(); PC->Possess(Player);
    Player->GetCharacterMovement()->SetMovementMode(MOVE_None);
    auto* ASC = Player->GetRPGAbilitySystemComponent();
    auto* Class = LoadObject<URPGPlayerClassDefinition>(nullptr, TEXT("/Game/Blueprints/GameData/PlayerClass/DA_Class_GreatSwordWarrior.DA_Class_GreatSwordWarrior"));
    if (!TestNotNull(TEXT("Authored class"), Class)) return false;
    auto* Growth = Player->GetPlayerSkillComponent();
    auto* Owner = Player->FindComponentByClass<URPGPlayerClassComponent>();
    auto* Slots = Player->FindComponentByClass<UQuickSlotComponent>();
    ASC->InitAbilityActorInfo(Player, Player);
    ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 200.f);
    ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 200.f);
    ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxIdentityGaugeAttribute(), 100.f);
    auto* Weapon = World->SpawnActor<ARPGPlayerWeapon>();
    auto* WeaponType = FindFProperty<FEnumProperty>(ARPGPlayerWeapon::StaticClass(), TEXT("WeaponType"));
    if (!TestNotNull(TEXT("Weapon type"), WeaponType)) return false;
    WeaponType->GetUnderlyingProperty()->SetIntPropertyValue(WeaponType->ContainerPtrToValuePtr<void>(Weapon), static_cast<int64>(ERPGGladiatorWeaponType::GreatSword));
    Player->FindComponentByClass<UPlayerCombatComponent>()->RegisterSpawnedWeapon(RPGGameplayTags::InputTag_EquipWeapon, Weapon, true);
    FRPGPlayerSkillRestoreData Record;
    Record.ClassId = Class->GetPrimaryAssetId(); Record.TotalSP = 1000; Record.bIdentityUnlocked = true;
    for (const auto& Entry : Class->Skills) { auto& Acquired = Record.AcquiredSkills.AddDefaulted_GetRef(); Acquired.SkillTag = Entry.Definition->SkillTag; }
    const auto Generation = Growth->BeginClassSkillRestore(Record.ClassId);
    FText Error;
    if (!TestTrue(TEXT("Restore class"), Growth->CompleteClassSkillRestore(Generation, Class, Record, Error))) return false;
    Owner->NotifyAbilitySystemReady(*ASC, 1);
    const auto* IdentitySpec = ASC->FindAbilitySpecFromClass(URPGGreatSwordResolveAbility::StaticClass());
    if (!TestNotNull(TEXT("Identity granted"), IdentitySpec)) return false;
    const auto IdentityHandle = IdentitySpec->Handle;
    ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentIdentityGaugeAttribute(), 100.f);
    TestFalse(TEXT("No equipped rage skill rejects entry"), ASC->TryActivateAbility(IdentityHandle));
    if (!TestTrue(TEXT("Register charge and normal skill"), Slots->InitializeSkillSlotsFromRecord(Generation,
        {RPGGameplayTags::Player_Ability_Skill_Charge, RPGGameplayTags::Player_Ability_Skill_AssultBlade}, Error))) return false;
    const auto* Charge = Class->FindSkill(RPGGameplayTags::Player_Ability_Skill_Charge)->Definition.Get();
    const auto* Spin = Class->FindSkill(RPGGameplayTags::Player_Ability_Skill_WhirlWind)->Definition.Get();
    const auto* Normal = Class->FindSkill(RPGGameplayTags::Player_Ability_Skill_AssultBlade)->Definition.Get();
    TestTrue(TEXT("Charge explicitly classified"), Charge->bGreatSwordRageSkill);
    TestTrue(TEXT("Spin explicitly classified"), Spin->bGreatSwordRageSkill);
    TestFalse(TEXT("Normal explicitly excluded"), Normal->bGreatSwordRageSkill);
    auto Cooldowns = [ASC](const URPGSkillDefinition* Definition) { FGameplayEffectQuery Q; Q.EffectDefinition=UGE_CoolDown::StaticClass(); Q.EffectSource=Definition; return ASC->GetActiveEffects(Q); };
    for (const auto* Definition : {Charge, Spin, Normal}) {
        auto Context=ASC->MakeEffectContext(); Context.AddSourceObject(Definition);
        auto Spec=ASC->MakeOutgoingSpec(UGE_CoolDown::StaticClass(),1.f,Context); Spec.Data->SetDuration(60.f,true); ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
    }
    if (!TestTrue(TEXT("Enter berserk"), ASC->TryActivateAbility(IdentityHandle))) return false;
    TestEqual(TEXT("Entry retains full gauge"), ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()),100.f);
    TestTrue(TEXT("Equipped rage cooldown reset"), Cooldowns(Charge).IsEmpty());
    TestEqual(TEXT("Unregistered rage cooldown retained"), Cooldowns(Spin).Num(),1);
    TestEqual(TEXT("Normal cooldown retained"), Cooldowns(Normal).Num(),1);
    TestFalse(TEXT("Repeated entry rejected"), ASC->TryActivateAbility(IdentityHandle));
    for (int i=0;i<1200;++i) { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60.f); }
    TestTrue(TEXT("No ten-second timeout"), URPGGreatSwordResolveAbility::IsBerserk(Player));
    TestEqual(TEXT("No natural drain"), ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()),100.f);
    auto* PlayerClass = LoadClass<ARPGPlayer>(nullptr, TEXT("/Game/Blueprints/Character/Player/BP_Player.BP_Player_C"));
    if (!TestNotNull(TEXT("Authored player"), PlayerClass)) return false;
    Player->GetMesh()->SetSkeletalMesh(PlayerClass->GetDefaultObject<ARPGPlayer>()->GetMesh()->GetSkeletalMeshAsset());
    Player->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
    Player->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    ASC->RefreshAbilityActorInfo();
    auto* ChargeSpec=ASC->FindAbilitySpecFromClass(Class->FindSkill(RPGGameplayTags::Player_Ability_Skill_Charge)->AbilityClass);
    auto* Ability=ChargeSpec ? Cast<URPGGameplayAbility_SkillContainer>(ChargeSpec->GetPrimaryInstance()) : nullptr;
    if (!TestNotNull(TEXT("Charge instance"), Ability)) return false;
    Slots->BeginUseSkillSlot(0);
    if (!TestTrue(TEXT("Charge starts"), Ability->IsActive())) return false;
    TestTrue(TEXT("Configured identity supplies the frozen class policy"), Ability->ActiveClassSkillPolicy == Class->IdentityAbility->GetDefaultObject());
    TestEqual(TEXT("Cost once at commit"), ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()),50.f);
    TestTrue(TEXT("No duplicate commit"), Ability->CommitSkillAndNotifyCooldown());
    TestEqual(TEXT("Duplicate commit preserves gauge"), ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()),50.f);
    TestEqual(TEXT("Damage boost frozen"), Ability->CommittedClassDamageMultiplier,1.15f);
    ASC->CancelAbilityHandle(ChargeSpec->Handle);
    Slots->EndUseSkillSlot(0);
    TestNull(TEXT("Cancellation releases the class policy"), Ability->ActiveClassSkillPolicy.Get());
    TestEqual(TEXT("Cancellation does not refund"), ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()),50.f);
    for (int i=0;i<780;++i) { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60.f); }
    TestTrue(TEXT("Normal cooldown expires"), Cooldowns(Charge).IsEmpty());
    Slots->BeginUseSkillSlot(0);
    if (!TestTrue(TEXT("Same skill can be reused"), Ability->IsActive())) return false;
    TestEqual(TEXT("Second cast exhausts gauge"), ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()),0.f);
    TestFalse(TEXT("Exhaustion ends state"), URPGGreatSwordResolveAbility::IsBerserk(Player));
    TestEqual(TEXT("Last cast still boosted"), Ability->CommittedClassDamageMultiplier,1.15f);
    TestTrue(TEXT("Last cast forbids gain"), Ability->SuppressesResolveGain());
    CastChecked<IRPGClassSkillPolicy>(Ability->ActiveClassSkillPolicy)->OnClassSkillHitConfirmed(*Ability);
    TestEqual(TEXT("Late hit cannot replenish final cast"), ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()),0.f);
    ASC->CancelAbilityHandle(ChargeSpec->Handle);
    Slots->EndUseSkillSlot(0);
    ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentIdentityGaugeAttribute(),100.f);
    TestTrue(TEXT("New full gauge can enter again"), ASC->TryActivateAbility(IdentityHandle));
    Player->FindComponentByClass<URPGCombatControlComponent>()->ResetForAvatarReuse();
    TestFalse(TEXT("New life clears berserk"), URPGGreatSwordResolveAbility::IsBerserk(Player));
    TestEqual(TEXT("New life clears gauge"), ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()),0.f);
    ASC->CancelAbilityHandle(IdentityHandle);
    for (int i=0;i<6;++i) { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60.f); }
    Slots->SetSkillSlot(0, RPGGameplayTags::Player_Ability_Skill_WhirlWind);
    ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentIdentityGaugeAttribute(),100.f);
    if (!TestTrue(TEXT("Enter with spin alone registered"), ASC->TryActivateAbility(IdentityHandle))) return false;
    ASC->CancelAbilityHandle(IdentityHandle); // End only the entry presentation, retain the mode.
    TestTrue(TEXT("Newly registered spin cooldown resets at next entry"), Cooldowns(Spin).IsEmpty());
    auto* SpinSpec=ASC->FindAbilitySpecFromClass(Class->FindSkill(RPGGameplayTags::Player_Ability_Skill_WhirlWind)->AbilityClass);
    auto* SpinAbility=SpinSpec ? Cast<URPGGameplayAbility_SkillContainer>(SpinSpec->GetPrimaryInstance()) : nullptr;
    if (!TestNotNull(TEXT("Spin instance"), SpinAbility)) return false;
    Slots->BeginUseSkillSlot(0);
    if (!TestTrue(TEXT("Spin starts"), SpinAbility->IsActive())) return false;
    TestEqual(TEXT("Spin costs once"), ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()),50.f);
    for (int i=0;i<60;++i) { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60.f); }
    TestEqual(TEXT("Spin duration does not spend again"), ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()),50.f);
    CastChecked<IRPGClassSkillPolicy>(SpinAbility->ActiveClassSkillPolicy)->OnClassSkillHitConfirmed(*SpinAbility);
    TestEqual(TEXT("Spin hit does not gain"), ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()),50.f);
    ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),0.f);
    TestFalse(TEXT("Death clears mode"), URPGGreatSwordResolveAbility::IsBerserk(Player));
    TestEqual(TEXT("Death clears remaining gauge"), ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()),0.f);
    return true;
}
#endif
