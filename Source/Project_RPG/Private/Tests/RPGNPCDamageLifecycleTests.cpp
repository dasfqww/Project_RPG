#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Tests/RPGSkillDamageSecurityTestFixture.h"
#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Attribute/RPGAttributeSet.h"
#include "BrainComponent.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
#include "FunctionLibrary/RPGSecurityBlueprintLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "RPGGameplayTags.h"

namespace RPGNPCDamageLifecycle
{
struct FDeathObservation { int32 Activated = 0; int32 Ended = 0; bool Cancelled = false; int32 DropActivated = 0; int32 DropEnded = 0; bool DropClaimConsumed = false; };

// Actual saved NPC, startup, death ability and shared damage GE. The source is a
// native fixture; AI movement is paused and replacement is explicitly server-forced.
// No BP/CDO edits, fake death tags, fabricated health loss or network evidence.
class FCommand final : public IAutomationLatentCommand
{
public:
	explicit FCommand(FAutomationTestBase* InTest) : Test(InTest) {}
	virtual ~FCommand() override { Cleanup(); }
	virtual bool Update() override
	{
		if (!World) return Initialize();
		if (FPlatformTime::Seconds() > Deadline) { Test->AddError(TEXT("Authored NPC lifecycle timed out")); return Finish(); }
		World->Tick(LEVELTICK_All, .05f);
		if (Stage == 0)
		{
			if (!IsValid(NPC) || !NPC->IsNPCStartUpDataReady()) return false;
			PauseMovement();
			OldASC = NPC->GetRPGAbilitySystemComponent();
			const float Health = NPC->GetRPGAttributeSet()->GetCurrentHealth();
			const float Defense = NPC->GetRPGAttributeSet()->GetDefense();
			if (!Test->TestTrue(TEXT("Authored startup supplies positive finite combat health/defense"),
				FMath::IsFinite(Health) && Health > 0.f && FMath::IsFinite(Defense) && Defense > 0.f)) return Finish();
			DeathClass = LoadClass<UGameplayAbility>(nullptr,
				TEXT("/Game/Blueprints/Character/NPC/Gruntling/GameplayAbility/GA_Guardian_Death.GA_Guardian_Death_C"));
			if (!Test->TestNotNull(TEXT("Saved Guardian death BP"), DeathClass) ||
				!Test->TestNotNull(TEXT("Death BP actually granted by authored startup"), OldASC->FindAbilitySpecFromClass(DeathClass))) return Finish();
			OldActorId = NPC->GetUniqueID();
			OldASCId = OldASC->GetUniqueID();
			OldHit = FHitResult(NPC, nullptr, NPC->GetActorLocation(), FVector::ForwardVector);
			StaleSpec = Source->ASC()->MakeOutgoingSpec(DamageClass, 1, Source->ASC()->MakeEffectContext());
			StaleSpec.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, 10.f);
			const TSharedRef<FDeathObservation> Counter = Death;
			const UClass* ExpectedDeath = DeathClass;
			UClass* ExpectedDrop = LoadClass<UGameplayAbility>(nullptr,
				TEXT("/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Guardian_DropItemAbility.BP_Guardian_DropItemAbility_C"));
			if (!Test->TestNotNull(TEXT("Saved Guardian drop ability"), ExpectedDrop) ||
				!Test->TestNotNull(TEXT("Saved startup grants the drop ability"), OldASC->FindAbilitySpecFromClass(ExpectedDrop))) return Finish();
			ActivatedHandle = OldASC->AbilityActivatedCallbacks.AddLambda([Counter, ExpectedDeath, ExpectedDrop](UGameplayAbility* Ability)
			{
				if (Ability && Ability->IsA(ExpectedDeath)) ++Counter->Activated;
				if (Ability && Ability->IsA(ExpectedDrop)) ++Counter->DropActivated;
			});
			const TWeakObjectPtr<ARPGNonPlayerCharacter> DropNPC = NPC;
			EndedHandle = OldASC->OnAbilityEnded.AddLambda([Counter, ExpectedDeath, ExpectedDrop, DropNPC](const FAbilityEndedData& Data)
			{
				if (Data.AbilityThatEnded && Data.AbilityThatEnded->IsA(ExpectedDeath))
				{
					++Counter->Ended;
					Counter->Cancelled |= Data.bWasCancelled;
				}
				if (Data.AbilityThatEnded && Data.AbilityThatEnded->IsA(ExpectedDrop))
				{
					++Counter->DropEnded;
					Counter->DropClaimConsumed = DropNPC.IsValid() && DropNPC->IsNPCStartUpDataReady() &&
						!DropNPC->IsActorBeingDestroyed() && DropNPC->GetRPGAttributeSet()->GetCurrentHealth() == 0.f &&
						DropNPC->GetRPGAbilitySystemComponent()->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death) &&
						!DropNPC->CanProcessDeathItemDrop();
				}
			});
			const float LethalBase = FMath::CeilToFloat(Health * Defense) + Defense;
			Profile.MaximumDamagePerHit = FMath::Max(100.f, LethalBase);
			Profile.MaximumFinalDamagePerHit = FMath::Max(100.f, Health + 2.f);
			Profile.MaximumServerHitDistance = 1000.f;
			UE_LOG(LogTemp, Display, TEXT("RPG_NPC_LIFECYCLE READY Actor=%u ASC=%u Health=%.6f Defense=%.6f Class=%s"),
				OldActorId, OldASCId, Health, Defense, *NPC->GetClass()->GetName());
			if (!Apply(OldHit, LethalBase) || !Test->TestEqual(TEXT("Actual shared GE kills authored NPC"), NPC->GetRPGAttributeSet()->GetCurrentHealth(), 0.f) ||
				!Test->TestEqual(TEXT("Actual death tag was added once"), OldASC->GetTagCount(RPGGameplayTags::Shared_Status_Death), 1) ||
				!Test->TestEqual(TEXT("Saved death BP activates once"), Death->Activated, 1)) return Finish();
			UAnimInstance* Anim = NPC->GetMesh()->GetAnimInstance();
			UAnimMontage* Montage = OldASC->GetCurrentMontage();
			if (!Test->TestTrue(TEXT("Saved death BP plays a real death montage"), Anim && Montage && Anim->Montage_IsActive(Montage))) return Finish();
			UE_LOG(LogTemp, Display, TEXT("RPG_NPC_LIFECYCLE DEATH Health=0 TagCount=1 Activations=1 Montage=1"));
			for (int32 Replay = 0; Replay < 3; ++Replay)
			{
				FActiveGameplayEffectHandle Handle;
				FText Error;
				if (!Test->TestFalse(TEXT("Authorized corpse replay rejected"), URPGSecurityBlueprintLibrary::ApplyAuthorizedServerDamage(
					Source, OldHit, DamageClass, 10.f, FGameplayTag(), Profile, Handle, Error)) ||
					!Test->TestEqual(TEXT("Corpse HP unchanged"), NPC->GetRPGAttributeSet()->GetCurrentHealth(), 0.f) ||
					!Test->TestEqual(TEXT("Corpse replay cannot reactivate death BP"), Death->Activated, 1)) return Finish();
				UE_LOG(LogTemp, Display, TEXT("RPG_NPC_LIFECYCLE CORPSE_REPLAY Index=%d Rejected=1 Health=0 Activations=1"), Replay);
			}
			Stage = 1;
			return false;
		}
		if (Stage == 1)
		{
			if (Death->Ended == 0) return false;
			// Saved NPC BP invokes drops only after its dissolve timeline finishes.
			// Waiting never counts as proof: require the actual activation/end below.
			if (Death->DropActivated == 0 && IsValid(NPC)) return false;
			if (!Test->TestEqual(TEXT("Death montage/ability completes once"), Death->Ended, 1) ||
				!Test->TestFalse(TEXT("Death completion is not a forced cancellation"), Death->Cancelled)) return Finish();
			UE_LOG(LogTemp, Display, TEXT("RPG_NPC_LIFECYCLE DEATH_COMPLETED Ended=1 Cancelled=0"));
			if (!Test->TestEqual(TEXT("Actual death BP invokes its saved drop ability once"), Death->DropActivated, 1) ||
				!Test->TestEqual(TEXT("Actual drop ability ends once"), Death->DropEnded, 1) ||
				!Test->TestTrue(TEXT("Real drop consumes the actor-lifetime claim before BP destruction"), Death->DropClaimConsumed)) return Finish();
			UE_LOG(LogTemp, Display, TEXT("RPG_NPC_DROP_SAVED Activated=1 Ended=1 ClaimConsumed=1 PostDissolve=1 RewardContentsNotInjected=1"));
			RemoveObservation();
			if (IsValid(NPC)) NPC->Destroy();
			NPC = SpawnNPC();
			if (!Test->TestNotNull(TEXT("Server-forced replacement NPC"), NPC)) return Finish();
			Stage = 2;
			return false;
		}
		if (!IsValid(NPC) || !NPC->IsNPCStartUpDataReady()) return false;
		PauseMovement();
		const float Before = NPC->GetRPGAttributeSet()->GetCurrentHealth();
		const float Defense = NPC->GetRPGAttributeSet()->GetDefense();
		if (!Test->TestTrue(TEXT("Replacement owns a distinct actor and ASC"), NPC->GetUniqueID() != OldActorId &&
			NPC->GetRPGAbilitySystemComponent()->GetUniqueID() != OldASCId) ||
			!Test->TestTrue(TEXT("Replacement startup supplies fresh health"), Before > 0.f && FMath::IsFinite(Before)) ||
			!Test->TestEqual(TEXT("Replacement does not inherit corpse death tag"), NPC->GetRPGAbilitySystemComponent()->GetTagCount(RPGGameplayTags::Shared_Status_Death), 0)) return Finish();
		if (!Test->TestFalse(TEXT("Old hit cannot alias a fresh NPC at the same coordinates"),
			URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(Source, OldHit, StaleSpec, Profile)) ||
			!Test->TestEqual(TEXT("Stale hit preserves replacement HP"), NPC->GetRPGAttributeSet()->GetCurrentHealth(), Before)) return Finish();
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_LIFECYCLE STALE_REJECTED OldActor=%u NewActor=%u OldASC=%u NewASC=%u Health=%.6f"),
			OldActorId, NPC->GetUniqueID(), OldASCId, NPC->GetRPGAbilitySystemComponent()->GetUniqueID(), Before);
		const float FreshBase = Defense * FMath::Min(10.f, Before * .5f);
		const float ExpectedDamage = static_cast<int32>(static_cast<double>(FreshBase) / Defense);
		const FHitResult FreshHit(NPC, nullptr, NPC->GetActorLocation(), FVector::ForwardVector);
		if (!Test->TestTrue(TEXT("Fresh hit damage is positive and nonlethal"), ExpectedDamage >= 1.f && ExpectedDamage < Before) ||
			!Apply(FreshHit, FreshBase) || !Test->TestEqual(TEXT("Fresh NPC receives only its own normal hit"), NPC->GetRPGAttributeSet()->GetCurrentHealth(), Before - ExpectedDamage)) return Finish();
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_LIFECYCLE FRESH_ACCEPTED Before=%.6f After=%.6f Damage=%.6f"), Before, NPC->GetRPGAttributeSet()->GetCurrentHealth(), ExpectedDamage);
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_LIFECYCLE COMPLETE AuthoredNPC=1 AuthoredDeath=1 ServerForcedReplacement=1 Network=0"));
		return Finish();
	}
private:
	bool Initialize()
	{
		NPCClass = LoadClass<ARPGNonPlayerCharacter>(nullptr,
			TEXT("/Game/Blueprints/Character/NPC/DummyNPC/BP_Dummy_Gdn.BP_Dummy_Gdn_C"));
		DamageClass = LoadClass<UGameplayEffect>(nullptr,
			TEXT("/Game/Blueprints/Character/Shared/GameplayEffect/GE_Shared_ApplyDamage.GE_Shared_ApplyDamage_C"));
		if (!Test->TestNotNull(TEXT("Saved ordinary NPC BP"), NPCClass) || !Test->TestNotNull(TEXT("Saved shared damage GE"), DamageClass)) return true;
		World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
		if (!Test->TestNotNull(TEXT("Isolated NPC world"), World)) return true;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->CreateAISystem();
		World->BeginPlay();
		World->GetWorldSettings()->NotifyBeginPlay();
		Source = World->SpawnActor<ARPGSkillDamageSecurityTestPawn>();
		AAIController* Controller = World->SpawnActor<AAIController>();
		if (!Test->TestNotNull(TEXT("Native damage source"), Source) || !Test->TestNotNull(TEXT("Source controller"), Controller)) return Finish();
		Controller->SetGenericTeamId(FGenericTeamId(0));
		Controller->Possess(Source);
		NPC = SpawnNPC();
		if (!Test->TestNotNull(TEXT("Authored NPC instance"), NPC)) return Finish();
		Deadline = FPlatformTime::Seconds() + 90.0;
		return false;
	}
	ARPGNonPlayerCharacter* SpawnNPC()
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ARPGNonPlayerCharacter* Spawned = World->SpawnActor<ARPGNonPlayerCharacter>(NPCClass, FVector(300, 0, 0), FRotator::ZeroRotator, Params);
		// This isolated world has no floor: stop gravity before the async startup wait.
		if (Spawned) Spawned->GetCharacterMovement()->DisableMovement();
		return Spawned;
	}
	void PauseMovement()
	{
		NPC->GetCharacterMovement()->DisableMovement();
		NPC->SetActorLocation(FVector(300, 0, 0));
		NPC->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		if (AAIController* Controller = Cast<AAIController>(NPC->GetController()); Controller && Controller->BrainComponent)
			Controller->BrainComponent->StopLogic(TEXT("Isolated NPC security fixture"));
	}
	bool Apply(const FHitResult& Hit, const float BaseDamage)
	{
		FActiveGameplayEffectHandle Handle;
		FText Error;
		const bool Result = URPGSecurityBlueprintLibrary::ApplyAuthorizedServerDamage(Source, Hit, DamageClass,
			BaseDamage, FGameplayTag(), Profile, Handle, Error);
		if (!Result) Test->AddError(FString::Printf(TEXT("Authored NPC damage rejected: %s"), *Error.ToString()));
		return Result;
	}
	void RemoveObservation()
	{
		if (OldASC.IsValid())
		{
			OldASC->AbilityActivatedCallbacks.Remove(ActivatedHandle);
			OldASC->OnAbilityEnded.Remove(EndedHandle);
		}
	}
	void Cleanup()
	{
		RemoveObservation();
		if (World)
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			World = nullptr;
		}
	}
	bool Finish() { Cleanup(); return true; }
	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	ARPGSkillDamageSecurityTestPawn* Source = nullptr;
	ARPGNonPlayerCharacter* NPC = nullptr;
	TWeakObjectPtr<URPGAbilitySystemComponent> OldASC;
	UClass* NPCClass = nullptr;
	UClass* DamageClass = nullptr;
	UClass* DeathClass = nullptr;
	TSharedRef<FDeathObservation> Death = MakeShared<FDeathObservation>();
	FDelegateHandle ActivatedHandle, EndedHandle;
	FHitResult OldHit;
	FGameplayEffectSpecHandle StaleSpec;
	FRPGSkillSecurityProfile Profile;
	uint32 OldActorId = 0, OldASCId = 0;
	int32 Stage = 0;
	double Deadline = 0.;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNPCAuthoredDamageLifecycleTest,
	"ProjectRPG.NPCDamage.AuthoredDeathAndRespawn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGNPCAuthoredDamageLifecycleTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(RPGNPCDamageLifecycle::FCommand(this));
	return true;
}
#endif
