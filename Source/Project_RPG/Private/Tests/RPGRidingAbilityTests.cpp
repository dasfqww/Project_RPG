#include "Tests/RPGRidingAbilityTests.h"
#include "Tests/RPGRidingActorTests.h"
#include "Riding/RPGRidingAbilityGate.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"

URPGRidingGateTestAbility::URPGRidingGateTestAbility()
{ InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor; NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly; }
URPGRidingGateLocalAbility::URPGRidingGateLocalAbility() { NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly; }
URPGRidingGateAttackAbility::URPGRidingGateAttackAbility() { SetAssetTags(FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.Attack.Test"), false))); }
URPGRidingGateDeathAbility::URPGRidingGateDeathAbility() { SetAssetTags(FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("Shared.Ability.Death"), false))); }
URPGRidingGateLockAbility::URPGRidingGateLockAbility() { SetAssetTags(FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.TargetLock"), false))); }
URPGRidingGateReactionAbility::URPGRidingGateReactionAbility() { SetAssetTags(FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("Shared.Ability.HitReact"), false))); }
URPGRidingGateMixedAbility::URPGRidingGateMixedAbility()
{ auto Tags = FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.TargetLock"), false)); Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.Attack.Test"), false)); SetAssetTags(Tags); }
bool URPGRidingGateTestAbility::CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
    const FGameplayTagContainer* Source, const FGameplayTagContainer* Target, FGameplayTagContainer* Failures) const
{ return Info && !FRPGRidingAbilityGate::IsBlocked(Info->AvatarActor.Get(), this) && Super::CanActivateAbility(Handle, Info, Source, Target, Failures); }
void URPGRidingGateTestAbility::ActivateAbility(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*, FGameplayAbilityActivationInfo, const FGameplayEventData*) { ++Activations; }
void URPGRidingGateTestAbility::Stop() { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false); }

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    FGameplayTag Tag(const TCHAR* Text) { return FGameplayTag::RequestGameplayTag(Text); }
    struct FFixture
    {
        UWorld* World;
        ARPGRidingTestPawn* Pawn;
        URPGRidingComponent* Riding;
        UAbilitySystemComponent* ASC;
        FRPGRidingAbilityGate Gate;
        FGameplayAbilitySpecHandle Handle;
        URPGRidingGateTestAbility* Ability;
        explicit FFixture(TSubclassOf<URPGRidingGateTestAbility> Class = URPGRidingGateTestAbility::StaticClass())
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            auto* PC = World->SpawnActor<APlayerController>(); auto* PS = World->SpawnActor<APlayerState>();
            PC->SetAsLocalPlayerController(); PC->SetPlayerState(PS); PS->SetOwner(PC);
            Pawn = World->SpawnActor<ARPGRidingTestPawn>(); PC->Possess(Pawn);
            ASC = Pawn->ASC; ASC->InitAbilityActorInfo(Pawn, Pawn); ASC->AddAttributeSetSubobject(Pawn->Attributes.Get());
            Pawn->Attributes->CurrentHealth.SetBaseValue(100); Pawn->Attributes->CurrentHealth.SetCurrentValue(100);
            Pawn->GetCharacterMovement()->MovementMode = MOVE_Walking;
            auto* Session = NewObject<URPGRidingSessionComponent>(PS); Session->RegisterComponent();
            Riding = Pawn->FindComponentByClass<URPGRidingComponent>(); RPGPrepareRidingMovementTestSession(*Session, *Riding);
            Handle = ASC->GiveAbility(FGameplayAbilitySpec(Class));
            Ability = Class->GetDefaultObject<URPGRidingGateTestAbility>();
        }
        ~FFixture() { ASC->CancelAllAbilities(); World->DestroyWorld(false); }
        bool Mount() { Riding->RequestSetMounted(true, {}); return Riding->GetRidingState().Mode == ERPGRidingMode::Mounted; }
        void Bind(FGameplayTag Input) { ASC->FindAbilitySpecFromHandle(Handle)->GetDynamicSpecSourceTags().AddTag(Input); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingAbilityClassificationTest, "ProjectRPG.Riding.Ability.Classification", Flags)
bool FRidingAbilityClassificationTest::RunTest(const FString&)
{
    FFixture F; if (!TestTrue(TEXT("Server fixture mounted"), F.Mount())) return false;
    TestTrue(TEXT("Untagged manual ability blocked"), FRPGRidingAbilityGate::IsBlocked(F.Pawn, F.Ability));
    TestFalse(TEXT("Automatic passive preserved"), FRPGRidingAbilityGate::IsBlocked(F.Pawn, GetDefault<URPGRidingGateAutomaticAbility>()));
    TestFalse(TEXT("Native Riding action category is exempt"), FRPGRidingAbilityGate::IsBlocked(F.Pawn, GetDefault<URPGRidingGateActionAbility>()));
    TestFalse(TEXT("Local presentation preserved"), FRPGRidingAbilityGate::IsBlocked(F.Pawn, GetDefault<URPGRidingGateLocalAbility>()));
    TestFalse(TEXT("Death processing preserved"), FRPGRidingAbilityGate::IsBlocked(F.Pawn, GetDefault<URPGRidingGateDeathAbility>()));
    TestFalse(TEXT("Target lock preserved"), FRPGRidingAbilityGate::IsBlocked(F.Pawn, GetDefault<URPGRidingGateLockAbility>()));
    TestTrue(TEXT("Combat cannot borrow target-lock exemption"), FRPGRidingAbilityGate::IsBlocked(F.Pawn, GetDefault<URPGRidingGateMixedAbility>()));
    TestFalse(TEXT("Riding grants no hit-reaction immunity"), FRPGRidingAbilityGate::IsBlocked(F.Pawn, GetDefault<URPGRidingGateReactionAbility>()));
    TestTrue(TEXT("An active forced reaction still prevents mounting"), FRPGRidingAbilityGate::BlocksMounting(GetDefault<URPGRidingGateReactionAbility>()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingAbilityActivationTest, "ProjectRPG.Riding.Ability.GASActivation", Flags)
bool FRidingAbilityActivationTest::RunTest(const FString&)
{
    FFixture F;
    if (!TestTrue(TEXT("Mounted"), F.Mount())) return false;
    TestFalse(TEXT("Direct GAS activation executes shared server gate"), F.ASC->TryActivateAbility(F.Handle));
    F.Riding->RequestSetMounted(false, {});
    TestTrue(TEXT("Fresh foot activation succeeds"), F.ASC->TryActivateAbility(F.Handle));
    TestFalse(TEXT("Already running untagged manual action blocks mount"), F.Mount());
    auto* Instance = Cast<URPGRidingGateTestAbility>(F.ASC->FindAbilitySpecFromHandle(F.Handle)->GetPrimaryInstance());
    if (!TestNotNull(TEXT("GAS instance"), Instance)) return false;
    Instance->Stop();
    TestTrue(TEXT("Ending manual action permits mount"), F.Mount());
    const auto Automatic = F.ASC->GiveAbility(FGameplayAbilitySpec(URPGRidingGateAutomaticAbility::StaticClass()));
    TestTrue(TEXT("Automatic ability remains available while mounted"), F.ASC->TryActivateAbility(Automatic));
    F.Riding->RequestSetMounted(false, {});
    TestTrue(TEXT("Active automatic ability does not prevent remount"), F.Mount());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingAbilityInputTest, "ProjectRPG.Riding.Ability.AttackPressAndRelease", Flags)
bool FRidingAbilityInputTest::RunTest(const FString&)
{
    FFixture F; const auto Input = Tag(TEXT("InputTag.LightAttack.Test")); F.Bind(Input);
    if (!TestTrue(TEXT("Mounted"), F.Mount())) return false;
    TestTrue(TEXT("Attack tag press consumed"), F.Gate.ConsumeTagPress(*F.ASC, Input));
    TestTrue(TEXT("Attack only dismounts"), F.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot);
    TestFalse(TEXT("No input flag latched"), F.ASC->FindAbilitySpecFromHandle(F.Handle)->InputPressed);
    TestTrue(TEXT("Held tag cannot replay after synchronous foot transition"), F.Gate.ConsumeTagPress(*F.ASC, Input));
    TestTrue(TEXT("Direct spec cannot bypass consumed physical press"), F.Gate.ConsumeSpecPress(*F.ASC, F.Handle));
    F.Gate.ReleaseTag(*F.ASC, Input);
    TestFalse(TEXT("Fresh press after release can execute foot ability"), F.Gate.ConsumeSpecPress(*F.ASC, F.Handle));
    TestTrue(TEXT("GAS foot activation after new edge"), F.ASC->TryActivateAbility(F.Handle));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingAbilitySharedInputTest, "ProjectRPG.Riding.Ability.SharedInputAndSkill", Flags)
bool FRidingAbilitySharedInputTest::RunTest(const FString&)
{
    FFixture F(URPGRidingGateAttackAbility::StaticClass()); const auto Input = Tag(TEXT("InputTag.CustomAttack")); F.Bind(Input);
    auto Other = F.ASC->GiveAbility(FGameplayAbilitySpec(URPGRidingGateAttackAbility::StaticClass()));
    F.ASC->FindAbilitySpecFromHandle(Other)->GetDynamicSpecSourceTags().AddTag(Input);
    if (!TestTrue(TEXT("Mounted"), F.Mount())) return false;
    TestTrue(TEXT("Custom attack binding dismounts"), F.Gate.ConsumeSpecPress(*F.ASC, F.Handle));
    TestTrue(TEXT("Another spec sharing this input is also consumed"), F.Gate.ConsumeSpecPress(*F.ASC, Other));
    F.Gate.ReleaseSpec(*F.ASC, F.Handle);
    TestFalse(TEXT("Shared input becomes usable after release"), F.Gate.ConsumeSpecPress(*F.ASC, Other));
    const auto Skill = F.ASC->GiveAbility(FGameplayAbilitySpec(URPGRidingGateTestAbility::StaticClass()));
    if (!TestTrue(TEXT("Remounted"), F.Mount())) return false;
    TestTrue(TEXT("Manual skill press rejected"), F.Gate.ConsumeSpecPress(*F.ASC, Skill));
    TestTrue(TEXT("Skill rejection keeps mounted"), F.Riding->GetRidingState().Mode == ERPGRidingMode::Mounted);
    F.Riding->RequestSetMounted(false, {});
    TestTrue(TEXT("Rejected skill was never queued for dismount"), F.Gate.ConsumeSpecPress(*F.ASC, Skill));
    F.Gate.ReleaseSpec(*F.ASC, Skill);
    TestFalse(TEXT("Skill available on a new edge"), F.Gate.ConsumeSpecPress(*F.ASC, Skill));
    F.Gate.Reset(); TestFalse(TEXT("Input lifetime can be reset safely"), F.Gate.ConsumeSpecPress(*F.ASC, F.Handle));
    return true;
}
#endif
