#include "Tests/RPGNPCDeathNetworkProbe.h"

#if !UE_BUILD_SHIPPING
#include "Tests/RPGSkillDamageSecurityTestFixture.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Attribute/RPGAttributeSet.h"
#include "BrainComponent.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
#include "FunctionLibrary/RPGSecurityBlueprintLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RPGGameplayTags.h"

namespace
{
const FVector FixturePosition(20000, 20000, 500);
uint64 NPCNetId(const UObject* Object)
{
	const UNetDriver* Driver = Object ? Object->GetWorld()->GetNetDriver() : nullptr;
	const UReplicationSystem* System = Driver ? Driver->GetReplicationSystem() : nullptr;
	const UObjectReplicationBridge* Bridge = System ? System->GetReplicationBridge() : nullptr;
	return Bridge ? Bridge->GetReplicatedRefHandle(Object).GetId() : 0;
}
void PauseNPC(ARPGNonPlayerCharacter& NPC)
{
	NPC.GetCharacterMovement()->DisableMovement();
	NPC.GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	if (AAIController* AI = Cast<AAIController>(NPC.GetController()); AI && AI->BrainComponent)
		AI->BrainComponent->StopLogic(TEXT("NPC death network fixture"));
}
ARPGNonPlayerCharacter* FindNPC(UWorld& World, const uint64 ExcludedActorId = 0)
{
	ARPGNonPlayerCharacter* Found = nullptr;
	for (TActorIterator<ARPGNonPlayerCharacter> It(&World); It; ++It)
	{
		// Iris allocates client-local actor names. Select only the unique fixture
		// class/location; proof binds its actual replicated actor/ASC IDs to server.
		if (It->GetClass()->GetName() != TEXT("BP_Dummy_Gdn_C") ||
			!It->GetActorLocation().Equals(FixturePosition, 1.f) || NPCNetId(*It) == ExcludedActorId) continue;
		if (Found) return nullptr;
		Found = *It;
	}
	return Found;
}
}

void FRPGNPCDeathNetworkProbe::Fail(const TCHAR* Reason)
{
	if (!FailureReason.IsEmpty()) return;
	FailureReason = Reason;
	UE_LOG(LogTemp, Error, TEXT("RPG_NPC_NET FAILED Reason=%s"), Reason);
}
ARPGNonPlayerCharacter* FRPGNPCDeathNetworkProbe::SpawnNPC(UWorld& World, const FName Name)
{
	FActorSpawnParameters Params;
	Params.Name = Name;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* NPC = World.SpawnActor<ARPGNonPlayerCharacter>(NPCClass, FixturePosition, FRotator::ZeroRotator, Params);
	if (NPC) { NPC->bAlwaysRelevant = true; PauseNPC(*NPC); }
	// Bound the fixture's cull radius below Iris' world-location maximum.
	if (NPC) NPC->SetNetCullDistanceSquared(1.e10f);
	return NPC;
}
bool FRPGNPCDeathNetworkProbe::Apply(ARPGNonPlayerCharacter& Target, const float Base)
{
	FRPGSkillSecurityProfile Profile;
	Profile.MaximumDamagePerHit = FMath::Max(100.f, Base);
	Profile.MaximumFinalDamagePerHit = FMath::Max(100.f, Target.GetRPGAttributeSet()->GetCurrentHealth() + 2.f);
	Profile.MaximumServerHitDistance = 1000.f;
	FActiveGameplayEffectHandle Handle;
	FText Error;
	return URPGSecurityBlueprintLibrary::ApplyAuthorizedServerDamage(Source.Get(),
		FHitResult(&Target, nullptr, Target.GetActorLocation(), FVector::ForwardVector), DamageClass,
		Base, FGameplayTag(), Profile, Handle, Error);
}
void FRPGNPCDeathNetworkProbe::TickServer(APlayerController& Controller)
{
	UWorld& World = *Controller.GetWorld();
	// One shared NPC lifecycle, not one duplicate lifecycle per server controller.
	if (World.GetFirstPlayerController() != &Controller) return;
	if (Stage == 0)
	{
		int32 Players = 0;
		for (FConstPlayerControllerIterator It = World.GetPlayerControllerIterator(); It; ++It)
			if (It->IsValid() && It->Get()->PlayerState && It->Get()->GetPawn()) ++Players;
		if (Players != 2) return;
		NPCClass = LoadClass<ARPGNonPlayerCharacter>(nullptr, TEXT("/Game/Blueprints/Character/NPC/DummyNPC/BP_Dummy_Gdn.BP_Dummy_Gdn_C"));
		DamageClass = LoadClass<UGameplayEffect>(nullptr, TEXT("/Game/Blueprints/Character/Shared/GameplayEffect/GE_Shared_ApplyDamage.GE_Shared_ApplyDamage_C"));
		if (!NPCClass || !DamageClass) { Fail(TEXT("MissingAuthoredAssets")); return; }
		Source = World.SpawnActor<ARPGSkillDamageSecurityTestPawn>();
		auto* AI = World.SpawnActor<AAIController>();
		if (!Source.IsValid() || !AI) { Fail(TEXT("SourceSpawn")); return; }
		Source->SetActorLocation(FixturePosition - FVector(300, 0, 0));
		AI->SetGenericTeamId(FGenericTeamId(0));
		AI->Possess(Source.Get());
		OldNPC = SpawnNPC(World, TEXT("RPGNPCNet_Old"));
		if (!OldNPC.IsValid()) { Fail(TEXT("NPCSpawn")); return; }
		Stage = 1;
		return;
	}
	if (Stage == 1)
	{
		if (!OldNPC.IsValid() || !OldNPC->IsNPCStartUpDataReady()) return;
		auto* ASC = OldNPC->GetRPGAbilitySystemComponent();
		OldActorNetId = NPCNetId(OldNPC.Get()); OldASCNetId = NPCNetId(ASC);
		if (!OldActorNetId || !OldASCNetId) return;
		Baseline = OldNPC->GetRPGAttributeSet()->GetCurrentHealth();
		if (!FMath::IsFinite(Baseline) || Baseline <= 0.f) { Fail(TEXT("InvalidStartupHealth")); return; }
		UClass* DeathClass = LoadClass<UGameplayAbility>(nullptr, TEXT("/Game/Blueprints/Character/NPC/Gruntling/GameplayAbility/GA_Guardian_Death.GA_Guardian_Death_C"));
		if (!DeathClass || !ASC->FindAbilitySpecFromClass(DeathClass)) { Fail(TEXT("MissingDeathAbility")); return; }
		ObservedASC = ASC;
		const auto Counter = Observation;
		ActivatedHandle = ASC->AbilityActivatedCallbacks.AddLambda([Counter, DeathClass](UGameplayAbility* Ability)
			{ if (Ability && Ability->IsA(DeathClass)) ++Counter->Activated; });
		EndedHandle = ASC->OnAbilityEnded.AddLambda([Counter, DeathClass](const FAbilityEndedData& Data)
			{ if (Data.AbilityThatEnded && Data.AbilityThatEnded->IsA(DeathClass)) { ++Counter->Ended; Counter->Cancelled |= Data.bWasCancelled; } });
		OldHit = FHitResult(OldNPC.Get(), nullptr, FixturePosition, FVector::ForwardVector);
		OldSpec = Source->ASC()->MakeOutgoingSpec(DamageClass, 1, Source->ASC()->MakeEffectContext());
		OldSpec.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, 10.f);
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_NET SERVER_READY Actor=%llu ASC=%llu Health=%.6f"), OldActorNetId, OldASCNetId, Baseline);
		// Explicit fixture barriers only. Proof requires both clients to actually
		// observe each baseline; elapsed time alone never counts as client evidence.
		NextStepAt = FPlatformTime::Seconds() + 12.; Stage = 2;
		return;
	}
	if (FPlatformTime::Seconds() < NextStepAt) return;
	if (Stage == 2)
	{
		if (!OldNPC.IsValid()) { Fail(TEXT("OldNPCDisappearedBeforeDamage")); return; }
		const float Defense = OldNPC->GetRPGAttributeSet()->GetDefense();
		if (!Apply(*OldNPC, FMath::CeilToFloat(Baseline * Defense) + Defense) ||
			OldNPC->GetRPGAttributeSet()->GetCurrentHealth() != 0.f ||
			ObservedASC->GetTagCount(RPGGameplayTags::Shared_Status_Death) != 1 || Observation->Activated != 1) { Fail(TEXT("AuthoredDeathRejected")); return; }
		const UAnimMontage* Montage = ObservedASC->GetCurrentMontage();
		if (!Montage || Montage->GetName() != TEXT("AM_Guardian_Death") || !OldNPC->GetMesh()->GetAnimInstance()->Montage_IsActive(Montage)) { Fail(TEXT("ServerDeathMontageMissing")); return; }
		for (int32 I = 0; I < 3; ++I)
			if (Apply(*OldNPC, 10.f) || OldNPC->GetRPGAttributeSet()->GetCurrentHealth() != 0.f || Observation->Activated != 1) { Fail(TEXT("CorpseReplayApproved")); return; }
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_NET SERVER_DEATH Actor=%llu ASC=%llu Health=0 TagCount=1 Activations=1 Montage=AM_Guardian_Death CorpseRejected=3"), OldActorNetId, OldASCNetId);
		Stage = 3; NextStepAt = FPlatformTime::Seconds() + 8.; return;
	}
	if (Stage == 3)
	{
		if (Observation->Ended != 1 || Observation->Cancelled) { Fail(TEXT("DeathDidNotCompleteNaturally")); return; }
		if (OldNPC.IsValid()) OldNPC->Destroy();
		NewNPC = SpawnNPC(World, TEXT("RPGNPCNet_New"));
		if (!NewNPC.IsValid()) { Fail(TEXT("ReplacementSpawn")); return; }
		Stage = 4; return;
	}
	if (Stage == 4)
	{
		if (!NewNPC.IsValid() || !NewNPC->IsNPCStartUpDataReady()) return;
		auto* ASC = NewNPC->GetRPGAbilitySystemComponent();
		const uint64 ActorId = NPCNetId(NewNPC.Get()), ASCId = NPCNetId(ASC);
		if (!ActorId || !ASCId) return;
		NewBaseline = NewNPC->GetRPGAttributeSet()->GetCurrentHealth();
		FRPGSkillSecurityProfile Profile; Profile.MaximumServerHitDistance = 1000.f;
		if (ActorId == OldActorNetId || ASCId == OldASCNetId || NewBaseline <= 0.f ||
			ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death) ||
			URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(Source.Get(), OldHit, OldSpec, Profile) ||
			NewNPC->GetRPGAttributeSet()->GetCurrentHealth() != NewBaseline) { Fail(TEXT("ReplacementStateOrStaleHit")); return; }
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_NET SERVER_REPLACEMENT OldActor=%llu OldASC=%llu Actor=%llu ASC=%llu Health=%.6f TagCount=0 StaleRejected=1 Ended=1 Cancelled=0"), OldActorNetId, OldASCNetId, ActorId, ASCId, NewBaseline);
		Stage = 5; NextStepAt = FPlatformTime::Seconds() + 10.; return;
	}
	if (Stage == 5)
	{
		if (!NewNPC.IsValid() || !Apply(*NewNPC, NewNPC->GetRPGAttributeSet()->GetDefense() * 10.f) ||
			NewNPC->GetRPGAttributeSet()->GetCurrentHealth() != NewBaseline - 10.f) { Fail(TEXT("ReplacementFreshDamage")); return; }
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_NET SERVER_COMPLETE Actor=%llu ASC=%llu Before=%.6f After=%.6f Damage=10.000000 ForcedReplacement=1 NativeSource=1 PausedAI=1"),
			NPCNetId(NewNPC.Get()), NPCNetId(NewNPC->GetRPGAbilitySystemComponent()), NewBaseline, NewNPC->GetRPGAttributeSet()->GetCurrentHealth());
		bPassed = true;
	}
}

void FRPGNPCDeathNetworkProbe::TickClient(APlayerController& Controller)
{
	UWorld& World = *Controller.GetWorld();
	if (!Controller.IsLocalController() || !Controller.PlayerState) return;
	const int32 Peer = Controller.PlayerState->GetPlayerId();
	if (Stage == 0)
	{
		OldNPC = FindNPC(World);
		if (!OldNPC.IsValid()) return;
		PauseNPC(*OldNPC);
		auto* ASC = OldNPC->GetRPGAbilitySystemComponent();
		if (ObservedASC.Get() != ASC)
		{
			ObservedASC = ASC;
			const auto Counter = Observation;
			HealthHandle = ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).AddLambda(
				[Counter](const FOnAttributeChangeData& Data) { if (Data.NewValue == 0.f && Data.OldValue > 0.f) Counter->ZeroNotified = true; });
		}
		Baseline = OldNPC->GetRPGAttributeSet()->GetCurrentHealth();
		OldActorNetId = NPCNetId(OldNPC.Get()); OldASCNetId = NPCNetId(ASC);
		if (!OldActorNetId || !OldASCNetId || ASC->GetAvatarActor() != OldNPC.Get() || Baseline <= 1.f) return;
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_NET CLIENT_READY Peer=%d Actor=%llu ASC=%llu Health=%.6f"), Peer, OldActorNetId, OldASCNetId, Baseline);
		Stage = 1;
	}
	if (Stage == 1)
	{
		if (!OldNPC.IsValid()) { Fail(TEXT("ClientMissedDeathBeforeDestroy")); return; }
		auto* ASC = OldNPC->GetRPGAbilitySystemComponent();
		auto* Anim = OldNPC->GetMesh()->GetAnimInstance();
		const UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
		if (Montage && Montage->GetName() == TEXT("AM_Guardian_Death") && Anim->Montage_IsActive(Montage)) bDeathMontageObserved = true;
		if (OldNPC->GetRPGAttributeSet()->GetCurrentHealth() != 0.f || ASC->GetTagCount(RPGGameplayTags::Shared_Status_Death) != 1 ||
			!Observation->ZeroNotified || !bDeathMontageObserved) return;
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_NET CLIENT_DEATH Peer=%d Actor=%llu ASC=%llu Health=0 TagCount=1 HPNotified=1 Montage=AM_Guardian_Death"), Peer, OldActorNetId, OldASCNetId);
		Stage = 2;
	}
	if (Stage == 2)
	{
		NewNPC = FindNPC(World, OldActorNetId);
		if (OldNPC.IsValid() || !NewNPC.IsValid()) return;
		PauseNPC(*NewNPC);
		auto* ASC = NewNPC->GetRPGAbilitySystemComponent();
		const uint64 ActorId = NPCNetId(NewNPC.Get()), ASCId = NPCNetId(ASC);
		NewBaseline = NewNPC->GetRPGAttributeSet()->GetCurrentHealth();
		if (!ActorId || !ASCId || ASC->GetAvatarActor() != NewNPC.Get() || NewBaseline <= 1.f) return;
		if (ActorId == OldActorNetId || ASCId == OldASCNetId || ASC->GetTagCount(RPGGameplayTags::Shared_Status_Death) != 0) { Fail(TEXT("ClientReplacementState")); return; }
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_NET CLIENT_REPLACEMENT Peer=%d OldActor=%llu OldASC=%llu Actor=%llu ASC=%llu Health=%.6f TagCount=0 OldRemoved=1"), Peer, OldActorNetId, OldASCNetId, ActorId, ASCId, NewBaseline);
		Stage = 3;
	}
	if (Stage == 3 && NewNPC.IsValid() && NewNPC->GetRPGAttributeSet()->GetCurrentHealth() == NewBaseline - 10.f)
	{
		UE_LOG(LogTemp, Display, TEXT("RPG_NPC_NET CLIENT_COMPLETE Peer=%d Actor=%llu ASC=%llu Before=%.6f After=%.6f Damage=10.000000"), Peer,
			NPCNetId(NewNPC.Get()), NPCNetId(NewNPC->GetRPGAbilitySystemComponent()), NewBaseline, NewNPC->GetRPGAttributeSet()->GetCurrentHealth());
		bPassed = true;
	}
}
#endif

FRPGNPCDeathNetworkProbe::~FRPGNPCDeathNetworkProbe()
{
#if !UE_BUILD_SHIPPING
	if (ObservedASC.IsValid())
	{
		ObservedASC->AbilityActivatedCallbacks.Remove(ActivatedHandle);
		ObservedASC->OnAbilityEnded.Remove(EndedHandle);
		ObservedASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).Remove(HealthHandle);
	}
#endif
}
void FRPGNPCDeathNetworkProbe::Tick(APlayerController& Controller)
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("RPGNPCDeathE2E")) || bPassed || !FailureReason.IsEmpty()) return;
	if (Controller.HasAuthority()) TickServer(Controller); else TickClient(Controller);
#endif
}
