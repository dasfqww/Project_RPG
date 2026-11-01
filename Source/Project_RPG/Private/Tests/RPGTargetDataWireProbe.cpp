#include "Tests/RPGTargetDataWireProbe.h"

#include "Tests/RPGTargetDataSecurityTestFixture.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/UI/PawnUIComponent.h"
#include "Components/SphereComponent.h"
#include "FunctionLibrary/RPGSecurityBlueprintLibrary.h"
#include "GameplayEffect/GE_ExecCalc_TakeDamage.h"
#include "RPGGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

#if !UE_BUILD_SHIPPING
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGTargetDataWireProbe)

#if !UE_BUILD_SHIPPING
namespace
{
	constexpr int32 CaseCount = 12;
	const TCHAR* CaseNames[CaseCount] = { TEXT("Baseline"), TEXT("Duplicate"), TEXT("Reversed"),
		TEXT("WrongActivation"), TEXT("ForeignSpec"), TEXT("OversizedActors"), TEXT("DuplicateCancel"),
		TEXT("Recovery"), TEXT("EndedReplay"), TEXT("Reactivate"), TEXT("OldActivationReplay"), TEXT("ForeignOwner") };
	const int32 ConfirmRX[CaseCount] = { 1, 4, 2, 1, 1, 1, 0, 1, 1, 0, 2, 1 };
	const int32 CancelRX[CaseCount] = { 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0 };
	const int32 ConfirmDelta[CaseCount] = { 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 1 };
	const int32 CancelDelta[CaseCount] = { 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 };

	bool Enabled() { return FParse::Param(FCommandLine::Get(), TEXT("RPGTargetDataAbuseE2E")); }
	bool RateEnabled() { return Enabled() && FParse::Param(FCommandLine::Get(), TEXT("RPGTargetDataRateE2E")); }
	bool DamageEnabled() { return Enabled() && FParse::Param(FCommandLine::Get(), TEXT("RPGTargetDataDamageE2E")); }
	constexpr int32 RateBurst = 96;
	constexpr int32 RateNormal = 48;
	constexpr int32 RateLimit = 64;
	struct FRateRequest
	{
		int16 Message;
		bool bCancellation;
		bool bWrongActivation;
		bool bFresh;
	};
	FRateRequest RateRequest(const int32 Stage, const bool bAttacker, const int32 Index)
	{
		if (!bAttacker) return { static_cast<int16>(5000 + Stage * 1000 + Index), false, false, true };
		if (Stage == 0) return { static_cast<int16>(2000 + Index), Index % 2 != 0, false, true };
		if (Stage == 1 && Index < RateLimit) return { 1006, Index % 3 == 2, Index % 3 == 0, false };
		return { static_cast<int16>(Stage == 1 ? 3000 + Index : 4000), false, false, true };
	}
	uint64 NetId(const UObject* Object)
	{
		const UNetDriver* Driver = Object->GetWorld()->GetNetDriver();
		const UReplicationSystem* System = Driver ? Driver->GetReplicationSystem() : nullptr;
		const UObjectReplicationBridge* Bridge = System ? System->GetReplicationBridge() : nullptr;
		return Bridge ? Bridge->GetReplicatedRefHandle(Object).GetId() : 0;
	}
	FPredictionKey MessageKey(const int16 Value)
	{
		FPredictionKey Key;
		Key.Current = Value;
		return Key;
	}

	// Bypass only the outgoing local ownership check. The unmodified Iris server
	// still authenticates the receiving connection before executing the GAS RPC.
	// Parameters use UFunction reflection and the engine serializer, not fabricated bytes.
	bool SendIris(ARPGTargetDataWireTestActor* Root, URPGTargetDataWireTestASC* Target,
		const FGameplayAbilitySpecHandle Handle, const FPredictionKey Original)
	{
		UNetDriver* Driver = Root->GetWorld()->GetNetDriver();
		if (!Driver || !Driver->ServerConnection || !Driver->GetReplicationSystem()) return false;
		UFunction* Function = Target->FindFunction(TEXT("ServerSetReplicatedTargetData"));
		if (!Function) return false;
		FStructOnScope Parameters(Function);
		const FGameplayAbilityTargetDataHandle Data;
		const FGameplayTag Tag;
		const FPredictionKey Message = MessageKey(1006);
		auto Copy = [&](const TCHAR* Name, const void* Value)
		{
			FProperty* Property = FindFProperty<FProperty>(Function, Name);
			if (!Property) return false;
			Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Parameters.GetStructMemory()), Value);
			return true;
		};
		if (!Copy(TEXT("AbilityHandle"), &Handle) || !Copy(TEXT("AbilityOriginalPredictionKey"), &Original) ||
			!Copy(TEXT("ReplicatedTargetDataHandle"), &Data) || !Copy(TEXT("ApplicationTag"), &Tag) ||
			!Copy(TEXT("CurrentPredictionKey"), &Message)) return false;
		return Driver->GetReplicationSystem()->SendRPC(Driver->ServerConnection->GetConnectionHandle().GetParentConnectionId(),
			Root, Target, Function, Parameters.GetStructMemory());
	}
}
#endif

ARPGTargetDataDamageTestTarget::ARPGTargetDataDamageTestTarget()
{
	auto* Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("WireDamageSphere"));
	SetRootComponent(Sphere);
	Sphere->InitSphereRadius(20.f);
	Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ASC = CreateDefaultSubobject<URPGAbilitySystemComponent>(TEXT("WireDamageASC"));
	ASC->SetIsReplicated(true);
	ASC->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	Attributes = CreateDefaultSubobject<URPGAttributeSet>(TEXT("WireDamageAttributes"));
	UI = CreateDefaultSubobject<UPawnUIComponent>(TEXT("WireDamageUI"));
	bReplicates = bAlwaysRelevant = bReplicateUsingRegisteredSubObjectList = true;
}

void ARPGTargetDataDamageTestTarget::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	if (!DamageEnabled()) return;
	ASC->InitAbilityActorInfo(this, this);
	ASC->AddAttributeSetSubobject(Attributes.Get());
	if (HasAuthority())
	{
		Attributes->SetMaxHealth(1000.f);
		Attributes->SetCurrentHealth(1000.f);
		Attributes->SetDefense(1.f);
	}
#endif
}
void ARPGTargetDataDamageTestTarget::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, Peer);
	DOREPLIFETIME(ThisClass, bFinished);
}
UAbilitySystemComponent* ARPGTargetDataDamageTestTarget::GetAbilitySystemComponent() const { return ASC; }
UPawnUIComponent* ARPGTargetDataDamageTestTarget::GetPawnUIComponent() const { return UI; }
float ARPGTargetDataDamageTestTarget::Health() const { return Attributes->GetCurrentHealth(); }

URPGTargetDataDamageTestEffect::URPGTargetDataDamageTestEffect()
{
#if !UE_BUILD_SHIPPING
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayEffectExecutionDefinition Execution;
	Execution.CalculationClass = UGE_ExecCalc_TakeDamage::StaticClass();
	Executions.Add(Execution);
#endif
}

URPGTargetDataWireTestASC::URPGTargetDataWireTestASC()
{
	SetIsReplicatedByDefault(true);
	SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
}

bool URPGTargetDataWireTestASC::HasCache(const FGameplayAbilitySpecHandle Handle, const FPredictionKey Key) const
{
	return AbilityTargetDataMap.Find(FGameplayAbilitySpecHandleAndPredictionKey(Handle, Key)).IsValid();
}

void URPGTargetDataWireTestASC::ServerSetReplicatedTargetData_Implementation(const FGameplayAbilitySpecHandle Handle,
	const FPredictionKey Original, const FGameplayAbilityTargetDataHandle& Data, const FGameplayTag Tag, const FPredictionKey Message)
{
#if !UE_BUILD_SHIPPING
	if (Enabled()) ++ConfirmRequests;
	auto* Probe = Cast<ARPGTargetDataWireTestActor>(GetOwner());
	if (RateEnabled() && Probe) Probe->ObserveRateRequest(Handle, Original, Message, false, &Data);
#endif
	Super::ServerSetReplicatedTargetData_Implementation(Handle, Original, Data, Tag, Message);
#if !UE_BUILD_SHIPPING
	if (RateEnabled() && Probe) Probe->ObserveRateResult();
#endif
}

void URPGTargetDataWireTestASC::ServerSetReplicatedTargetDataCancelled_Implementation(const FGameplayAbilitySpecHandle Handle,
	const FPredictionKey Original, const FPredictionKey Message)
{
#if !UE_BUILD_SHIPPING
	if (Enabled()) ++CancelRequests;
	auto* Probe = Cast<ARPGTargetDataWireTestActor>(GetOwner());
	if (RateEnabled() && Probe) Probe->ObserveRateRequest(Handle, Original, Message, true, nullptr);
#endif
	Super::ServerSetReplicatedTargetDataCancelled_Implementation(Handle, Original, Message);
#if !UE_BUILD_SHIPPING
	if (RateEnabled() && Probe) Probe->ObserveRateResult();
#endif
}

void URPGTargetDataWireTestASC::ServerWireCheckpoint_Implementation(const int32 Case)
{
#if !UE_BUILD_SHIPPING
	if (Enabled())
	{
		if (auto* Probe = Cast<ARPGTargetDataWireTestActor>(GetOwner())) Probe->Checkpoint(Case);
	}
#endif
}

void URPGTargetDataWireTestASC::ServerRateCheckpoint_Implementation(const int32 Stage)
{
#if !UE_BUILD_SHIPPING
	if (RateEnabled())
	{
		if (auto* Probe = Cast<ARPGTargetDataWireTestActor>(GetOwner())) Probe->RateCheckpoint(Stage);
	}
#endif
}

void URPGTargetDataWireTestASC::ServerDamageReady_Implementation()
{
#if !UE_BUILD_SHIPPING
	if (DamageEnabled())
		if (auto* Probe = Cast<ARPGTargetDataWireTestActor>(GetOwner())) Probe->DamageReady();
#endif
}

ARPGTargetDataWireTestActor::ARPGTargetDataWireTestActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	bReplicateUsingRegisteredSubObjectList = true;
	ASC = CreateDefaultSubobject<URPGTargetDataWireTestASC>(TEXT("WireASC"));
	DamageAttributes = CreateDefaultSubobject<URPGAttributeSet>(TEXT("WireSourceAttributes"));
}

UAbilitySystemComponent* ARPGTargetDataWireTestActor::GetAbilitySystemComponent() const { return ASC; }

void ARPGTargetDataWireTestActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, GrantedHandle);
	DOREPLIFETIME(ThisClass, Peer);
	DOREPLIFETIME(ThisClass, DamageTarget);
	DOREPLIFETIME(ThisClass, bDamageReady);
}

void ARPGTargetDataWireTestActor::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	bEnabled = Enabled();
	if (!bEnabled) return;
	Deadline = FPlatformTime::Seconds() + 180.0;
	ASC->InitAbilityActorInfo(this, this);
	if (DamageEnabled())
	{
		if (RateEnabled()) { Fail(TEXT("DamageAndRateMustBeSeparate")); return; }
		ASC->AddAttributeSetSubobject(DamageAttributes.Get());
		if (HasAuthority())
		{
			DamageAttributes->SetAttack(1.f);
			DamageAttributes->SetCriticalChance(0.f);
		}
	}
	if (HasAuthority())
	{
		// GAS TryActivateAbility rejects a simulated avatar before CanActivateAbility.
		// Only the owning connection receives AutonomousProxy; foreign copies stay simulated.
		SetAutonomousProxy(true);
		const auto* PC = Cast<APlayerController>(GetOwner());
		Peer = PC && PC->PlayerState ? PC->PlayerState->GetPlayerId() : -1;
		if (DamageEnabled())
		{
			FActorSpawnParameters Params;
			Params.Name = FName(*FString::Printf(TEXT("TargetDataDamagePeer_%d"), Peer));
			DamageTarget = GetWorld()->SpawnActor<ARPGTargetDataDamageTestTarget>(
				ARPGTargetDataDamageTestTarget::StaticClass(), GetActorLocation() + FVector(120, 0, 0), FRotator::ZeroRotator, Params);
			if (!DamageTarget) { Fail(TEXT("DamageTargetSpawn")); return; }
			DamageTarget->Peer = Peer;
			DamageEffectDelegate = DamageTarget->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddUObject(
				this, &ThisClass::ObserveDamageEffect);
		}
		GrantedHandle = ASC->GiveAbility(FGameplayAbilitySpec(URPGTargetDataSecurityTestAbility::StaticClass(), 1));
	}
	SetActorTickEnabled(true);
#endif
}

void ARPGTargetDataWireTestActor::EndPlay(const EEndPlayReason::Type Reason)
{
	for (int32 I = 0; I < 2; ++I)
		if (auto* Target = ClientDamageTargets[I].Get())
			Target->GetAbilitySystemComponent()->GetGameplayAttributeValueChangeDelegate(
				URPGAttributeSet::GetCurrentHealthAttribute()).Remove(DamageHealthDelegates[I]);
	if (DamageTarget)
		DamageTarget->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(DamageEffectDelegate);
	if (BoundOriginal.IsValidKey() && ASC->HasCache(GrantedHandle, BoundOriginal))
	{
		ASC->AbilityTargetDataSetDelegate(GrantedHandle, BoundOriginal).Remove(DataDelegate);
		ASC->AbilityTargetDataCancelledDelegate(GrantedHandle, BoundOriginal).Remove(CancelDelegate);
	}
	Super::EndPlay(Reason);
}

void ARPGTargetDataWireTestActor::Fail(const TCHAR* Reason)
{
#if !UE_BUILD_SHIPPING
	UE_LOG(LogTemp, Error, TEXT("RPG_TARGETDATA_WIRE FAILED Peer=%d Case=%d Reason=%s"), Peer, NextCase, Reason);
#endif
	bEnabled = false;
	SetActorTickEnabled(false);
}

void ARPGTargetDataWireTestActor::BindActivation()
{
#if !UE_BUILD_SHIPPING
	if (BoundOriginal.IsValidKey() && ASC->HasCache(GrantedHandle, BoundOriginal))
	{
		ASC->AbilityTargetDataSetDelegate(GrantedHandle, BoundOriginal).Remove(DataDelegate);
		ASC->AbilityTargetDataCancelledDelegate(GrantedHandle, BoundOriginal).Remove(CancelDelegate);
	}
	UGameplayAbility* Ability = ASC->FindAbilitySpecFromHandle(GrantedHandle)->GetPrimaryInstance();
	BoundOriginal = Ability->GetCurrentActivationInfo().GetActivationPredictionKey();
	DataDelegate = ASC->AbilityTargetDataSetDelegate(GrantedHandle, BoundOriginal).AddUObject(this, &ThisClass::OnTargetData);
	CancelDelegate = ASC->AbilityTargetDataCancelledDelegate(GrantedHandle, BoundOriginal).AddUObject(this, &ThisClass::OnCancellation);
#endif
}

void ARPGTargetDataWireTestActor::OnTargetData(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag)
{
	++Confirmations;
#if !UE_BUILD_SHIPPING
	if (bEnabled && DamageEnabled()) ApplyFixtureDamage(Data);
#endif
	ASC->ConsumeClientReplicatedTargetData(GrantedHandle, BoundOriginal);
}

void ARPGTargetDataWireTestActor::OnCancellation()
{
	++Cancellations;
	ASC->ConsumeClientReplicatedTargetData(GrantedHandle, BoundOriginal);
}

void ARPGTargetDataWireTestActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	if (!bEnabled) return;
	if (!HasAuthority())
	{
		const auto* PC = Cast<APlayerController>(GetOwner());
		if (!PC || !PC->IsLocalController()) return; // Foreign replicas are passive RPC targets.
		if (FPlatformTime::Seconds() > Deadline) { Fail(TEXT("Timeout")); return; }
		if (DamageEnabled())
		{
			if (!PrepareClientDamage() || !bDamageReady) return;
			if (bWireCompleteReceived) { TickDamage(); return; }
		}
		if (bClientRateSending && FPlatformTime::Seconds() >= ClientRateNextAt)
		{
			SubmitRateRequest();
			return;
		}
		if (GetLocalRole() == ROLE_AutonomousProxy && !bStartedActivation && GrantedHandle.IsValid() &&
			ASC->FindAbilitySpecFromHandle(GrantedHandle))
		{
			ASC->InitAbilityActorInfo(this, this);
			bStartedActivation = true;
			if (!ASC->TryActivateAbility(GrantedHandle)) Fail(TEXT("ClientActivation"));
		}
		return;
	}
	if (FPlatformTime::Seconds() > Deadline) { Fail(TEXT("Timeout")); return; }
	if (DamageEnabled() && !bDamageReady) return;
	if (NextCase == CaseCount)
	{
		if (RateEnabled()) TickRate();
		if (DamageEnabled()) TickDamage();
		return;
	}
	if (!BoundOriginal.IsValidKey())
	{
		const auto* Spec = ASC->FindAbilitySpecFromHandle(GrantedHandle);
		if (!Spec || !Spec->IsActive() || !Spec->GetPrimaryInstance()) return;
		BindActivation();
		if (NetId(ASC) == 0) { Fail(TEXT("MissingNetIdentity")); return; }
		// Iris's receiver rejection names the RPC caller ASC, not its parent Actor.
		UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_WIRE IDENTIFIED Peer=%d Root=%s NetId=%llu"), Peer, *GetName(), NetId(ASC));
	}
	if (!bAwaitingCheckpoint) IssueCase();
#endif
}

void ARPGTargetDataWireTestActor::IssueCase()
{
#if !UE_BUILD_SHIPPING
	ARPGTargetDataWireTestActor* Foreign = nullptr;
	for (TActorIterator<ARPGTargetDataWireTestActor> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->BoundOriginal.IsValidKey()) { Foreign = *It; break; }
	}
	if (!Foreign) return; // Both independent connections must have activated before the first challenge.
	if (NextCase == 11 && (Foreign->NextCase < 10 ||
		!Foreign->ASC->FindAbilitySpecFromHandle(Foreign->GrantedHandle)->IsActive())) return;
	if (NextCase == 8)
	{
		PreviousOriginal = BoundOriginal;
		CastChecked<URPGTargetDataSecurityTestAbility>(ASC->FindAbilitySpecFromHandle(GrantedHandle)->GetPrimaryInstance())->Finish();
	}
	BeforeConfirmRequests = ASC->ConfirmRequests;
	BeforeCancelRequests = ASC->CancelRequests;
	BeforeConfirmations = Confirmations;
	BeforeCancellations = Cancellations;
	if (DamageEnabled())
	{
		BeforeDamageHealth = DamageTarget->Health();
		BeforeDamageEffects = DamageEffects;
	}
	bAwaitingCheckpoint = true;
	if (NextCase == 11)
	{
		UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_WIRE FOREIGN_CHALLENGE Peer=%d ForeignPeer=%d Root=%s NetId=%llu Handle=%s Original=%d Active=1"),
			Peer, Foreign->Peer, *Foreign->GetName(), NetId(Foreign->ASC), *Foreign->GrantedHandle.ToString(), Foreign->BoundOriginal.Current);
	}
	// PredictionKey deliberately serializes to zero on another owner's connection.
	// Only this opt-in dev fixture provides the foreign token's scalar for forgery.
	ClientRunCase(NextCase, BoundOriginal, PreviousOriginal, Foreign, Foreign->GrantedHandle, Foreign->BoundOriginal.Current);
#endif
}

void ARPGTargetDataWireTestActor::ClientRunCase_Implementation(const int32 Case, const FPredictionKey Original,
	const FPredictionKey Previous, ARPGTargetDataWireTestActor* Foreign,
	const FGameplayAbilitySpecHandle ForeignHandle, const int32 ForeignOriginalValue)
{
#if !UE_BUILD_SHIPPING
	if (!bEnabled || Case < 0 || Case >= CaseCount || !Foreign || !ForeignHandle.IsValid() ||
		ForeignOriginalValue < 1 || ForeignOriginalValue > 32767)
	{
		Fail(TEXT("InvalidChallenge")); return;
	}
	FGameplayAbilityTargetDataHandle Data;
	if (DamageEnabled() && Case != 5)
	{
		if (!Foreign->DamageTarget) { Fail(TEXT("ForeignDamageTargetUnavailable")); return; }
		auto* Actors = new FGameplayAbilityTargetData_ActorArray();
		Actors->TargetActorArray.Add(Foreign->DamageTarget);
		Data.Add(Actors); // Deliberately submit the other peer's target; never trusted as damage authority.
	}
	auto Submit = [&](const int16 Message, const FPredictionKey Key)
	{
		ASC->ServerSetReplicatedTargetData(GrantedHandle, Key, Data, FGameplayTag(), MessageKey(Message));
	};
	switch (Case)
	{
	case 0: Submit(1000, Original); break;
	case 1: for (int32 I = 0; I < 4; ++I) Submit(1000, Original); break;
	case 2: Submit(1002, Original); Submit(1001, Original); break;
	case 3: Submit(1003, MessageKey(Original.Current == 1 ? 32767 : Original.Current - 1)); break;
	case 4:
		ASC->ServerSetReplicatedTargetData(ForeignHandle, Original, Data, FGameplayTag(), MessageKey(1003)); break;
	case 5:
	{
		// GAS caps each serialized ActorArray at 31. Three valid wire entries
		// exceed the project's aggregate 64-actor cap without serializer truncation.
		for (const int32 Count : { 31, 31, 3 })
		{
			auto* Actors = new FGameplayAbilityTargetData_ActorArray();
			Actors->TargetActorArray.SetNum(Count);
			Data.Add(Actors);
		}
		Submit(1003, Original); break;
	}
	case 6:
		for (int32 I = 0; I < 2; ++I) ASC->ServerSetReplicatedTargetDataCancelled(GrantedHandle, Original, MessageKey(1002));
		break;
	case 7: Submit(1003, Original); break;
	case 8: Submit(1004, Original); break;
	case 9:
		CastChecked<URPGTargetDataSecurityTestAbility>(ASC->FindAbilitySpecFromHandle(GrantedHandle)->GetPrimaryInstance())->Finish();
		if (!ASC->TryActivateAbility(GrantedHandle)) { Fail(TEXT("ClientReactivation")); return; }
		break;
	case 10: Submit(1005, Previous); Submit(1005, Original); break;
	case 11:
		if (NetId(Foreign->ASC) == 0) { Fail(TEXT("MissingForeignNetIdentity")); return; }
		if (!SendIris(Foreign, Foreign->ASC, ForeignHandle, MessageKey(static_cast<int16>(ForeignOriginalValue))) ||
			!SendIris(this, ASC, GrantedHandle, Original)) { Fail(TEXT("IrisSendUnavailable")); return; }
		UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_WIRE FOREIGN_QUEUED Peer=%d ForeignPeer=%d RootLocal=%s NetId=%llu Handle=%s Original=%d"),
			Peer, Foreign->Peer, *Foreign->GetName(), NetId(Foreign->ASC), *ForeignHandle.ToString(), ForeignOriginalValue);
		break;
	}
	// Use the exact same ASC attachment queue as the GAS RPCs, including activation.
	// Do not assume order between the root actor and its subobject's queues.
	ASC->ServerWireCheckpoint(Case);
#endif
}

void ARPGTargetDataWireTestActor::Checkpoint(const int32 Case)
{
#if !UE_BUILD_SHIPPING
	if (!bEnabled || !bAwaitingCheckpoint || Case != NextCase || Case < 0 || Case >= CaseCount)
	{
		if (bEnabled) Fail(TEXT("UnexpectedCheckpoint"));
		return;
	}
	const int32 RX = ASC->ConfirmRequests - BeforeConfirmRequests;
	const int32 CancelCount = ASC->CancelRequests - BeforeCancelRequests;
	const int32 Accepted = Confirmations - BeforeConfirmations;
	const int32 Cancelled = Cancellations - BeforeCancellations;
	if (RX != ConfirmRX[Case] || CancelCount != CancelRX[Case] ||
		Accepted != ConfirmDelta[Case] || Cancelled != CancelDelta[Case])
	{
		UE_LOG(LogTemp, Error, TEXT("RPG_TARGETDATA_WIRE COUNTERS Peer=%d Case=%d RX=%d CancelRX=%d Accepted=%d Cancelled=%d"),
			Peer, Case, RX, CancelCount, Accepted, Cancelled);
		Fail(TEXT("ReceiverOrCallbackMismatch")); return;
	}
	bool bCacheSafe = true;
	if (Case == 3) bCacheSafe = !ASC->HasCache(GrantedHandle, MessageKey(BoundOriginal.Current == 1 ? 32767 : BoundOriginal.Current - 1));
	if (Case == 4)
	{
		for (TActorIterator<ARPGTargetDataWireTestActor> It(GetWorld()); It; ++It)
			if (*It != this) bCacheSafe &= !ASC->HasCache(It->GrantedHandle, BoundOriginal);
	}
	if (Case == 8 || Case == 9 || Case == 10) bCacheSafe = !ASC->HasCache(GrantedHandle, PreviousOriginal);
	if (!bCacheSafe) { Fail(TEXT("RejectedRequestAllocatedCache")); return; }
	if (DamageEnabled())
	{
		const float Health = DamageTarget->Health();
		const int32 Effects = DamageEffects - BeforeDamageEffects;
		if (Health != BeforeDamageHealth - 10.f * ConfirmDelta[Case] || Effects != ConfirmDelta[Case])
		{
			Fail(TEXT("RejectedWireChangedHealthOrMissingPositiveDamage")); return;
		}
		UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_DAMAGE CASE Peer=%d Index=%d TargetNetId=%llu Before=%.6f After=%.6f Effects=%d"),
			Peer, Case, NetId(DamageTarget->GetAbilitySystemComponent()), BeforeDamageHealth, Health, Effects);
	}
	if (Case == 9)
	{
		const auto* Spec = ASC->FindAbilitySpecFromHandle(GrantedHandle);
		if (!Spec || !Spec->IsActive() || !Spec->GetPrimaryInstance() ||
			Spec->GetPrimaryInstance()->GetCurrentActivationInfo().GetActivationPredictionKey() == PreviousOriginal)
		{
			Fail(TEXT("ServerReactivation")); return;
		}
		BindActivation();
	}
	UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_WIRE CASE Peer=%d Index=%d Name=%s RX=%d CancelRX=%d Accepted=%d Cancelled=%d CacheSafe=1"),
		Peer, Case, CaseNames[Case], RX, CancelCount, Accepted, Cancelled);
	bAwaitingCheckpoint = false;
	if (++NextCase == CaseCount)
	{
		UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_WIRE SERVER_COMPLETE Peer=%d Cases=12 RX=15 CancelRX=2 Accepted=5 Cancelled=1"), Peer);
		ClientComplete();
		if (!RateEnabled() && !DamageEnabled()) SetActorTickEnabled(false);
	}
#endif
}

void ARPGTargetDataWireTestActor::ClientComplete_Implementation()
{
#if !UE_BUILD_SHIPPING
	if (!bEnabled) return;
	UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_WIRE CLIENT_COMPLETE Peer=%d Cases=12"), Peer);
	bWireCompleteReceived = true;
	if (!RateEnabled() && !DamageEnabled()) SetActorTickEnabled(false);
#endif
}

bool ARPGTargetDataWireTestActor::PrepareClientDamage()
{
#if !UE_BUILD_SHIPPING
	if (bClientDamagePrepared) return true;
	if (Peer < 0 || !DamageTarget) return false;
	TArray<ARPGTargetDataDamageTestTarget*> Targets;
	for (TActorIterator<ARPGTargetDataDamageTestTarget> It(GetWorld()); It; ++It)
		if (It->Peer >= 0) Targets.Add(*It);
	if (Targets.Num() != 2) return false;
	if (Targets[0] != DamageTarget) Swap(Targets[0], Targets[1]);
	if (Targets[0] != DamageTarget || Targets[0]->Peer != Peer || Targets[1]->Peer == Peer)
	{
		Fail(TEXT("ClientDamageIdentity")); return false;
	}
	for (auto* Target : Targets)
		if (Target->Health() != 1000.f || NetId(Target->GetAbilitySystemComponent()) == 0) return false;
	for (int32 I = 0; I < 2; ++I)
	{
		auto* Target = Targets[I];
		ClientDamageTargets[I] = Target;
		const uint64 Id = NetId(Target->GetAbilitySystemComponent());
		UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_DAMAGE BASELINE Peer=%d TargetPeer=%d TargetNetId=%llu Health=1000.000000"),
			Peer, Target->Peer, Id);
		DamageHealthDelegates[I] = Target->GetAbilitySystemComponent()->GetGameplayAttributeValueChangeDelegate(
			URPGAttributeSet::GetCurrentHealthAttribute()).AddWeakLambda(this, [this, I, Id](const FOnAttributeChangeData& Change)
			{
				if (!bEnabled || Change.NewValue == Change.OldValue) return;
				++ClientDamageNotifications[I];
				UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_DAMAGE HP_NOTIFY Peer=%d TargetPeer=%d TargetNetId=%llu Old=%.6f New=%.6f"),
					Peer, ClientDamageTargets[I]->Peer, Id, Change.OldValue, Change.NewValue);
			});
	}
	bClientDamagePrepared = true;
	ASC->ServerDamageReady(); // Both clients' actual HP baselines must precede any activation/damage.
	return true;
#else
	return false;
#endif
}

void ARPGTargetDataWireTestActor::DamageReady()
{
#if !UE_BUILD_SHIPPING
	if (!bEnabled || !HasAuthority() || !DamageEnabled() || bServerDamageReadyReceived) return;
	bServerDamageReadyReceived = true;
	TArray<ARPGTargetDataWireTestActor*> Probes;
	for (TActorIterator<ARPGTargetDataWireTestActor> It(GetWorld()); It; ++It) Probes.Add(*It);
	if (Probes.Num() != 2 || !Probes[0]->bServerDamageReadyReceived || !Probes[1]->bServerDamageReadyReceived) return;
	for (auto* Probe : Probes)
	{
		if (!Probe->DamageTarget || Probe->DamageTarget->Health() != 1000.f ||
			NetId(Probe->DamageTarget->GetAbilitySystemComponent()) == 0)
		{
			Fail(TEXT("ServerDamageBaseline")); return;
		}
		Probe->bDamageReady = true;
		Probe->ForceNetUpdate();
		UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_DAMAGE CONFIG Peer=%d TargetNetId=%llu Base=10 Attack=1 Defense=1 Crit=0 FinalCap=10 NativeHit=1"),
			Probe->Peer, NetId(Probe->DamageTarget->GetAbilitySystemComponent()));
	}
#endif
}

void ARPGTargetDataWireTestActor::ObserveDamageEffect(UAbilitySystemComponent* Source,
	const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
{
#if !UE_BUILD_SHIPPING
	if (!bEnabled || !DamageEnabled()) return;
	if (Source != ASC || !Spec.Def || Spec.Def->GetClass() != URPGTargetDataDamageTestEffect::StaticClass() ||
		Spec.GetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, false) != 10.f)
	{
		Fail(TEXT("UnexpectedDamageEffect")); return;
	}
	++DamageEffects; // Observed from the target's real GAS application delegate, not inferred from RPC acceptance.
#endif
}

void ARPGTargetDataWireTestActor::ApplyFixtureDamage(const FGameplayAbilityTargetDataHandle& Data)
{
#if !UE_BUILD_SHIPPING
	if (!HasAuthority() || !bDamageReady || !DamageTarget) { Fail(TEXT("UnreadyDamageCallback")); return; }
	ARPGTargetDataWireTestActor* Foreign = nullptr;
	for (TActorIterator<ARPGTargetDataWireTestActor> It(GetWorld()); It; ++It)
		if (*It != this) { Foreign = *It; break; }
	if (!Foreign || !Foreign->DamageTarget) { Fail(TEXT("MissingForeignDamageTarget")); return; }
	int32 IgnoredActors = 0;
	for (int32 I = 0; I < Data.Num(); ++I)
		if (const auto* Entry = Data.Get(I))
			for (const auto& Actor : Entry->GetActors())
			{
				if (Actor.Get() != Foreign->DamageTarget) { Fail(TEXT("UnexpectedSubmittedActor")); return; }
				++IgnoredActors;
			}
	const float Before = DamageTarget->Health(), ForeignBefore = Foreign->DamageTarget->Health();
	// This native server-authored hit is deliberately separate from the submitted TargetData.
	// It exercises production authorization/GE/execution/AttributeSet, not a BP montage or overlap query.
	FHitResult Hit(DamageTarget, Cast<UPrimitiveComponent>(DamageTarget->GetRootComponent()),
		DamageTarget->GetActorLocation(), FVector::UpVector);
	Hit.bBlockingHit = true;
	FRPGSkillSecurityProfile Profile;
	Profile.MaximumDamagePerHit = Profile.MaximumFinalDamagePerHit = 10.f;
	Profile.MaximumServerHitDistance = 250.f;
	FActiveGameplayEffectHandle Applied;
	FText Error;
	if (!URPGSecurityBlueprintLibrary::ApplyAuthorizedServerDamage(this, Hit,
		URPGTargetDataDamageTestEffect::StaticClass(), 10.f, RPGGameplayTags::Shared_SetByCaller_BaseDamage,
		Profile, Applied, Error) || !bEnabled || DamageTarget->Health() != Before - 10.f ||
		Foreign->DamageTarget->Health() != ForeignBefore)
	{
		Fail(TEXT("NativeAuthorizedDamageOrForeignHPInvariant")); return;
	}
	UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_DAMAGE EFFECT Peer=%d TargetNetId=%llu ForeignNetId=%llu Count=%d Before=%.6f After=%.6f ForeignBefore=%.6f ForeignAfter=%.6f IgnoredActors=%d"),
		Peer, NetId(DamageTarget->GetAbilitySystemComponent()), NetId(Foreign->DamageTarget->GetAbilitySystemComponent()),
		DamageEffects, Before, DamageTarget->Health(), ForeignBefore, Foreign->DamageTarget->Health(), IgnoredActors);
#endif
}

void ARPGTargetDataWireTestActor::TickDamage()
{
#if !UE_BUILD_SHIPPING
	if (bDamageFinished) return;
	if (!HasAuthority())
	{
		for (int32 I = 0; I < 2; ++I)
		{
			const auto* Target = ClientDamageTargets[I].Get();
			if (!Target || !Target->bFinished || Target->Health() != 950.f || ClientDamageNotifications[I] < 1) return;
		}
		for (int32 I = 0; I < 2; ++I)
		{
			const auto* Target = ClientDamageTargets[I].Get();
			UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_DAMAGE RESULT Peer=%d TargetPeer=%d TargetNetId=%llu Health=%.6f Notify=%d"),
				Peer, Target->Peer, NetId(Target->GetAbilitySystemComponent()), Target->Health(), ClientDamageNotifications[I]);
		}
		UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_DAMAGE CLIENT_COMPLETE Peer=%d Targets=2"), Peer);
		bDamageFinished = true;
		SetActorTickEnabled(false);
		return;
	}
	ARPGTargetDataWireTestActor* Other = nullptr;
	for (TActorIterator<ARPGTargetDataWireTestActor> It(GetWorld()); It; ++It)
		if (*It != this) { Other = *It; break; }
	if (!Other || Other->NextCase != CaseCount || Peer > Other->Peer) return;
	for (auto* Probe : { this, Other })
		if (!Probe->bEnabled || Probe->DamageTarget->Health() != 950.f || Probe->DamageEffects != 5 || Probe->Confirmations != 5)
		{
			Fail(TEXT("LateForeignOrReplayDamage")); return;
		}
	const double Now = FPlatformTime::Seconds();
	if (DamageQuietUntil == 0) { DamageQuietUntil = Now + 3.0; return; }
	if (Now < DamageQuietUntil) return;
	for (auto* Probe : { this, Other })
	{
		Probe->DamageTarget->bFinished = true;
		Probe->DamageTarget->ForceNetUpdate();
		Probe->bDamageFinished = true;
		UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_DAMAGE SERVER_COMPLETE Peer=%d TargetNetId=%llu Health=%.6f Effects=%d Quiet=3"),
			Probe->Peer, NetId(Probe->DamageTarget->GetAbilitySystemComponent()), Probe->DamageTarget->Health(), Probe->DamageEffects);
		Probe->SetActorTickEnabled(false);
	}
#endif
}

void ARPGTargetDataWireTestActor::TickRate()
{
#if !UE_BUILD_SHIPPING
	ARPGTargetDataWireTestActor* Other = nullptr;
	for (TActorIterator<ARPGTargetDataWireTestActor> It(GetWorld()); It; ++It)
		if (*It != this) { Other = *It; break; }
	if (!Other || Other->NextCase != CaseCount || Peer > Other->Peer) return;
	if (RateStage >= 0 && !bRateChallengeIssued)
	{
		// A client RPC being queued is not proof the control peer is ready under loss.
		// Start the burst only after its first normal request reaches this server.
		if (Other->RateReceived > 0)
		{
			bRateChallengeIssued = true;
			ClientRunRateStage(RateStage, true, BoundOriginal);
		}
		return;
	}
	if (RateStage >= 0 && (!bRateStageComplete || !Other->bRateStageComplete)) return;
	if (RateStage == 2)
	{
		for (auto* Probe : { this, Other })
		{
			UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_RATE SERVER_COMPLETE Peer=%d Role=%s Stages=3 Recovery=1"),
				Probe->Peer, Probe->bRateAttacker ? TEXT("Attacker") : TEXT("Control"));
			Probe->ClientRateComplete();
			Probe->SetActorTickEnabled(false);
		}
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	// Both same-ASC checkpoints have arrived; no phase traffic remains pending.
	// Do not reset/inspect the production limiter. Let its real server-time window expire.
	if (NextRateStageAt < 0.0) { NextRateStageAt = Now + 2.0; return; }
	if (Now < NextRateStageAt) return;
	NextRateStageAt = -1.0;
	const int32 Next = RateStage + 1;
	BeginRateStage(Next, true);
	Other->BeginRateStage(Next, false);
#endif
}

void ARPGTargetDataWireTestActor::BeginRateStage(const int32 Stage, const bool bAttacker)
{
#if !UE_BUILD_SHIPPING
	if (ASC->GetRequestLimit() != RateLimit) { Fail(TEXT("UnexpectedRateLimit")); return; }
	RateStage = Stage;
	bRateAttacker = bAttacker;
	bRateStageComplete = false;
	bRateChallengeIssued = !bAttacker || Stage == 2;
	RateReceived = RateDropped = 0;
	RateWindows.Reset();
	if (Stage == 0)
		UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_RATE CONFIG Peer=%d Role=%s Limit=64 Burst=96 Normal=48 Idle=2"),
			Peer, bAttacker ? TEXT("Attacker") : TEXT("Control"));
	if (bRateChallengeIssued) ClientRunRateStage(Stage, bAttacker, BoundOriginal);
#endif
}

void ARPGTargetDataWireTestActor::ClientRunRateStage_Implementation(const int32 Stage,
	const bool bAttacker, const FPredictionKey Original)
{
#if !UE_BUILD_SHIPPING
	if (!bEnabled || !RateEnabled() || Stage < 0 || Stage > 2 || !Original.IsValidKey())
	{
		Fail(TEXT("InvalidRateChallenge")); return;
	}
	RateStage = Stage;
	bRateAttacker = bAttacker;
	ClientRateOriginal = Original;
	ClientRateSubmitted = 0;
	bClientRateSending = true;
	ClientRateNextAt = FPlatformTime::Seconds();
	if (bAttacker || Stage == 2)
		while (bClientRateSending && bEnabled) SubmitRateRequest();
#endif
}

void ARPGTargetDataWireTestActor::SubmitRateRequest()
{
#if !UE_BUILD_SHIPPING
	const FRateRequest Plan = RateRequest(RateStage, bRateAttacker, ClientRateSubmitted);
	const FPredictionKey Original = Plan.bWrongActivation ?
		MessageKey(ClientRateOriginal.Current == 1 ? 32767 : ClientRateOriginal.Current - 1) : ClientRateOriginal;
	if (Plan.bCancellation)
		ASC->ServerSetReplicatedTargetDataCancelled(GrantedHandle, Original, MessageKey(Plan.Message));
	else
		ASC->ServerSetReplicatedTargetData(GrantedHandle, Original, FGameplayAbilityTargetDataHandle(), FGameplayTag(), MessageKey(Plan.Message));
	const int32 Count = RateStage == 2 ? 1 : (bRateAttacker ? RateBurst : RateNormal);
	if (++ClientRateSubmitted == Count)
	{
		bClientRateSending = false;
		UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_RATE CLIENT_STAGE Peer=%d Stage=%d Role=%s Submitted=%d"),
			Peer, RateStage, bRateAttacker ? TEXT("Attacker") : TEXT("Control"), Count);
		ASC->ServerRateCheckpoint(RateStage);
	}
	else ClientRateNextAt = FPlatformTime::Seconds() + 0.125;
#endif
}

void ARPGTargetDataWireTestActor::ObserveRateRequest(const FGameplayAbilitySpecHandle Handle,
	const FPredictionKey Original, const FPredictionKey Message, const bool bCancellation,
	const FGameplayAbilityTargetDataHandle* Data)
{
#if !UE_BUILD_SHIPPING
	if (!bEnabled || RateStage < 0 || bRateStageComplete) return;
	const int32 Count = RateStage == 2 ? 1 : (bRateAttacker ? RateBurst : RateNormal);
	const FRateRequest Plan = RateRequest(RateStage, bRateAttacker, RateReceived);
	const FPredictionKey ExpectedOriginal = Plan.bWrongActivation ?
		MessageKey(BoundOriginal.Current == 1 ? 32767 : BoundOriginal.Current - 1) : BoundOriginal;
	if (RateReceived >= Count || Handle != GrantedHandle || Original != ExpectedOriginal ||
		!Message.IsLocalClientKey() || Message.Current != Plan.Message || bCancellation != Plan.bCancellation ||
		(Data && Data->Num() != 0)) { Fail(TEXT("RateWireRequestMismatch")); return; }
	const double Now = GetWorld()->GetTimeSeconds();
	if (RateWindows.IsEmpty() || Now - RateWindows.Last().Started >= 1.0)
	{
		FRateWindow& Window = RateWindows.AddDefaulted_GetRef();
		Window.Started = Now;
		Window.First = RateReceived;
	}
	FRateWindow& Window = RateWindows.Last();
	Window.Ended = Now;
	++Window.Requests;
	if (bCancellation) ++Window.Cancels; else ++Window.Confirms;
	// Independent receipt-time oracle, not TryConsume or the production limiter's private state.
	bExpectedRateCallback = Plan.bFresh && Window.Requests <= RateLimit;
	if (Plan.bFresh && !bExpectedRateCallback) ++RateDropped;
	bRateCancellation = bCancellation;
	RateBeforeConfirmations = Confirmations;
	RateBeforeCancellations = Cancellations;
	++RateReceived;
#endif
}

void ARPGTargetDataWireTestActor::ObserveRateResult()
{
#if !UE_BUILD_SHIPPING
	if (!bEnabled || RateStage < 0 || bRateStageComplete) return;
	const int32 Accepted = Confirmations - RateBeforeConfirmations;
	const int32 Cancelled = Cancellations - RateBeforeCancellations;
	if (Accepted != (bExpectedRateCallback && !bRateCancellation ? 1 : 0) ||
		Cancelled != (bExpectedRateCallback && bRateCancellation ? 1 : 0))
	{
		Fail(TEXT("RateAdmissionOrCallbackMismatch")); return;
	}
	RateWindows.Last().Accepted += Accepted;
	RateWindows.Last().Cancelled += Cancelled;
	if (!bRateAttacker)
		UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_RATE NORMAL Peer=%d Stage=%d Index=%d At=%.6f Accepted=1"),
			Peer, RateStage, RateReceived - 1, RateWindows.Last().Ended);
	const FPredictionKey Wrong = MessageKey(BoundOriginal.Current == 1 ? 32767 : BoundOriginal.Current - 1);
	if (ASC->HasCache(GrantedHandle, Wrong)) Fail(TEXT("RateRejectedRequestAllocatedCache"));
#endif
}

void ARPGTargetDataWireTestActor::RateCheckpoint(const int32 Stage)
{
#if !UE_BUILD_SHIPPING
	if (!bEnabled || !RateEnabled() || Stage != RateStage || bRateStageComplete || Stage < 0 || Stage > 2)
	{
		if (bEnabled) Fail(TEXT("UnexpectedRateCheckpoint"));
		return;
	}
	const int32 Count = Stage == 2 ? 1 : (bRateAttacker ? RateBurst : RateNormal);
	if (RateReceived != Count || (bRateAttacker && Stage < 2 && RateDropped == 0))
	{
		Fail(TEXT("RateBurstNotReceivedOrNotOverLimit")); return;
	}
	int32 Confirms = 0, Cancels = 0, Accepted = 0, Cancelled = 0;
	for (int32 Index = 0; Index < RateWindows.Num(); ++Index)
	{
		const FRateWindow& Window = RateWindows[Index];
		UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_RATE WINDOW Peer=%d Stage=%d Role=%s Index=%d First=%d RX=%d ConfirmRX=%d CancelRX=%d Accepted=%d Cancelled=%d Started=%.6f Ended=%.6f Limit=64"),
			Peer, Stage, bRateAttacker ? TEXT("Attacker") : TEXT("Control"), Index, Window.First, Window.Requests,
			Window.Confirms, Window.Cancels, Window.Accepted, Window.Cancelled, Window.Started, Window.Ended);
		Confirms += Window.Confirms; Cancels += Window.Cancels;
		Accepted += Window.Accepted; Cancelled += Window.Cancelled;
	}
	UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_RATE STAGE Peer=%d Stage=%d Role=%s RX=%d CancelRX=%d Accepted=%d Cancelled=%d RateDropped=%d Windows=%d CacheSafe=1"),
		Peer, Stage, bRateAttacker ? TEXT("Attacker") : TEXT("Control"), Confirms, Cancels, Accepted, Cancelled, RateDropped, RateWindows.Num());
	bRateStageComplete = true;
#endif
}

void ARPGTargetDataWireTestActor::ClientRateComplete_Implementation()
{
#if !UE_BUILD_SHIPPING
	if (!bEnabled || !RateEnabled()) return;
	UE_LOG(LogTemp, Display, TEXT("RPG_TARGETDATA_RATE CLIENT_COMPLETE Peer=%d Role=%s Stages=3"),
		Peer, bRateAttacker ? TEXT("Attacker") : TEXT("Control"));
	SetActorTickEnabled(false);
#endif
}

URPGTargetDataWireProbeComponent::URPGTargetDataWireProbeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(false);
}

void URPGTargetDataWireProbeComponent::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	SetComponentTickEnabled(Enabled() && GetOwner()->HasAuthority());
#endif
}

void URPGTargetDataWireProbeComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
#if !UE_BUILD_SHIPPING
	auto* PC = Cast<APlayerController>(GetOwner());
	if (!Enabled() || !PC || !PC->HasAuthority()) { SetComponentTickEnabled(false); return; }
	if (!PC->PlayerState || !PC->GetPawn()) return;
	FActorSpawnParameters Parameters;
	Parameters.Owner = PC;
	Parameters.Name = FName(*FString::Printf(TEXT("TargetDataWirePeer_%d"), PC->PlayerState->GetPlayerId()));
	if (!GetWorld()->SpawnActor<ARPGTargetDataWireTestActor>(Parameters))
		UE_LOG(LogTemp, Error, TEXT("RPG_TARGETDATA_WIRE FAILED Reason=Spawn"));
	SetComponentTickEnabled(false);
#endif
}
