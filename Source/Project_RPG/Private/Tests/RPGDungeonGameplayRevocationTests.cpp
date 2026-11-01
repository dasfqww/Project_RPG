#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/RPGDungeonGameplayRevocationTestFixture.h"
#include "Tests/RPGBossBattleRetryTestFixture.h"
#include "Tests/RPGBossDamageTestFixture.h"
#include "Tests/RPGBossTestFixture.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Combat/HitQuery/RPGHitQuerySubsystem.h"
#include "Component/Combat/PawnCombatComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/RPGPlayerMovementComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Components/BoxComponent.h"
#include "Controller/RPGAIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameMode/RPGBossBattleGameMode.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "Misc/AutomationTest.h"
#include "Player/RPGPlayerState.h"
#include "RPGGameplayTags.h"
#include "Security/RPGDamageExecutionGuard.h"
#include "Security/RPGDungeonGameplayAuthority.h"

struct FRPGDungeonGameplayRevocationTestAccess
{
	static void AddParticipant(ARPGBossBattleGameMode& Mode, APlayerController* Controller)
	{
		Mode.Participants.Add(Controller);
		Mode.ParticipantRetryStates.Add(Controller).LastPawn = Controller->GetPawn();
	}
	static bool HasLivingParticipant(const ARPGBossBattleGameMode& Mode) { return Mode.HasLivingParticipant(); }
	static bool ResetForRetry(ARPGBossBattleGameMode& Mode) { return Mode.ResetParticipantsForRetry(); }
};

namespace RPGDungeonGameplayRevocationTests
{
	const FString Session = TEXT("20304050-6070-8090-a0b0-c0d0e0f00102");
	struct FMember
	{
		ARPGBossThreatProbeController* Controller = nullptr;
		ARPGPlayerState* State = nullptr;
		ARPGBossRetryTestPlayer* Pawn = nullptr;
		URPGAbilitySystemComponent* ASC() const { return Pawn->GetRPGAbilitySystemComponent(); }
		URPGPlayerMovementComponent* Movement() const { return Cast<URPGPlayerMovementComponent>(Pawn->GetCharacterMovement()); }
		URPGSecurityValidationComponent* Security() const { return Pawn->FindComponentByClass<URPGSecurityValidationComponent>(); }
		bool Revoke() const { return State->RevokeAuthenticatedIdentity(Session, State->GetBackendCharacterId(), State->GetAuthenticatedSteamId()); }
	};
	struct FFixture
	{
		UWorld* World = nullptr;
		FMember First, Remaining;
		FFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
			if (!World) return;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL()); World->CreateAISystem(); World->BeginPlay();
			World->GetWorldSettings()->NotifyBeginPlay();
			First = AddMember(TEXT("76561198000000001"));
			Remaining = AddMember(TEXT("76561198000000002"));
		}
		FMember AddMember(const FString& Steam)
		{
			FMember Result;
			Result.Controller = World->SpawnActor<ARPGBossThreatProbeController>();
			Result.State = World->SpawnActor<ARPGPlayerState>();
			Result.Controller->SetPlayerState(Result.State); Result.State->SetOwner(Result.Controller);
			Result.State->SetAuthenticatedIdentity(FGuid::NewGuid().ToString(), Steam, Session);
			Result.Pawn = World->SpawnActor<ARPGBossRetryTestPlayer>();
			Result.Controller->Possess(Result.Pawn);
			Result.ASC()->SetNumericAttributeBase(URPGAttributeSet::GetAttackAttribute(), 1.f);
			Result.ASC()->SetNumericAttributeBase(URPGAttributeSet::GetDefenseAttribute(), 1.f);
			Result.ASC()->SetNumericAttributeBase(URPGAttributeSet::GetCriticalChanceAttribute(), 0.f);
			return Result;
		}
		~FFixture()
		{
			if (World) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonGameplayRevocationTest,
	"ProjectRPG.Online.DungeonAuthorityRevocation.Gameplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonGameplayRevocationTest::RunTest(const FString&)
{
	using namespace RPGDungeonGameplayRevocationTests;
	FFixture F;
	if (!TestNotNull(TEXT("Player movement replacement"), F.First.Movement())) return false;
	const auto Active = F.First.ASC()->GiveAbility(FGameplayAbilitySpec(URPGDungeonRevocationTestAbility::StaticClass()));
	const auto NewAction = F.First.ASC()->GiveAbility(FGameplayAbilitySpec(URPGDungeonRevocationTestAbility::StaticClass()));
	const auto PartyAction = F.Remaining.ASC()->GiveAbility(FGameplayAbilitySpec(URPGDungeonRevocationTestAbility::StaticClass()));
	F.First.ASC()->OnAbilitySpecInputPressed(Active);
	F.Remaining.ASC()->OnAbilitySpecInputPressed(PartyAction);
	TestTrue(TEXT("Exact player has an active untagged ability"), F.First.ASC()->FindAbilitySpecFromHandle(Active)->IsActive());
	TestTrue(TEXT("Other player has an active ability"), F.Remaining.ASC()->FindAbilitySpecFromHandle(PartyAction)->IsActive());
	F.First.ASC()->FindAbilitySpecFromHandle(Active)->GetPrimaryInstance()->SetCanBeCanceled(false);
	const int32 GrantCount = F.First.ASC()->GetActivatableAbilities().Num();
	FPredictionKey OldKey; OldKey.Current = 101;
	FPredictionKey MessageKey; MessageKey.Current = 102;
	F.First.ASC()->ServerSetReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, NewAction, OldKey, MessageKey);
	TestTrue(TEXT("Generic input RPC positive control"), F.First.ASC()->GetReplicatedDataOfGenericReplicatedEvent(
		EAbilityGenericReplicatedEvent::InputPressed, NewAction, OldKey).bTriggered);
	F.First.ASC()->ConsumeAllReplicatedData(NewAction, OldKey);
	UGameplayEffect* PersistentEffect = NewObject<UGameplayEffect>(F.First.Pawn);
	PersistentEffect->DurationPolicy = EGameplayEffectDurationType::Infinite;
	const auto Effect = F.First.ASC()->ApplyGameplayEffectSpecToSelf(FGameplayEffectSpec(PersistentEffect, F.First.ASC()->MakeEffectContext(), 1.f));
	TestTrue(TEXT("Persistent effect positive control"), Effect.IsValid());
	const FGuid Authorization = F.First.Security()->BeginMovementAuthorization(2.f, 400.f, TEXT("RevocationTestDash"));
	TestTrue(TEXT("Movement grant positive control"), Authorization.IsValid());
	F.First.Movement()->Velocity = FVector(400.f, 0.f, 0.f);
	F.First.Movement()->SetMovementMode(MOVE_Flying);
	auto* Weapon = F.World->SpawnActor<ARPGPlayerWeapon>();
	auto* Combat = F.First.Pawn->GetPawnCombatComponent();
	Combat->RegisterSpawnedWeapon(RPGGameplayTags::Fragment_StatMod_1, Weapon, true);
	Combat->ToggleWeaponCollision(true);
	TestEqual(TEXT("Open weapon window positive control"), Weapon->GetWeaponCollisionBox()->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	const auto PartyMode = F.Remaining.Movement()->MovementMode;
	bool bCallbackCalled = false, bReactivationBlocked = false;
	const auto Callback = F.First.ASC()->OnAbilityEnded.AddLambda([&](const FAbilityEndedData&)
	{
		bCallbackCalled = true;
		bReactivationBlocked = !F.First.ASC()->TryActivateAbility(NewAction);
		F.First.Movement()->SetMovementMode(MOVE_Walking);
		Combat->ToggleWeaponCollision(true);
	});
	TestFalse(TEXT("Unrevoked identity cannot quarantine GAS"), F.First.ASC()->RevokeDungeonGameplayAuthority(*F.First.State));
	F.First.State->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client cannot revoke"), F.First.Revoke());
	F.First.State->SetRole(ROLE_Authority);
	TestFalse(TEXT("Wrong member receipt cannot revoke"), F.First.State->RevokeAuthenticatedIdentity(Session,
		F.Remaining.State->GetBackendCharacterId(), F.Remaining.State->GetAuthenticatedSteamId()));
	TestTrue(TEXT("Current exact identity revoked"), F.First.Revoke());
	F.First.ASC()->OnAbilityEnded.Remove(Callback);
	TestTrue(TEXT("Cancellation callback cannot reactivate"), bCallbackCalled && bReactivationBlocked);
	TestFalse(TEXT("Non-cancelable execution is terminated on its own ASC"), F.First.ASC()->FindAbilitySpecFromHandle(Active)->IsActive());
	TestFalse(TEXT("Old input is cleared"), F.First.ASC()->IsAbilitySpecInputPressed(Active));
	F.First.ASC()->OnAbilitySpecInputPressed(NewAction);
	TestFalse(TEXT("New input cannot activate or latch"), F.First.ASC()->FindAbilitySpecFromHandle(NewAction)->IsActive()
		|| F.First.ASC()->IsAbilitySpecInputPressed(NewAction));
	TestFalse(TEXT("Direct GAS activation is blocked even without ability tags"), F.First.ASC()->TryActivateAbility(NewAction));
	F.First.ASC()->ServerSetReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, NewAction, OldKey, MessageKey);
	F.First.ASC()->ServerSetReplicatedEventWithPayload(EAbilityGenericReplicatedEvent::InputReleased, NewAction, OldKey, MessageKey, FVector::ZeroVector);
	TestFalse(TEXT("Late generic input cannot recreate pending pressed data"), F.First.ASC()->GetReplicatedDataOfGenericReplicatedEvent(
		EAbilityGenericReplicatedEvent::InputPressed, NewAction, OldKey).bTriggered);
	TestFalse(TEXT("Late payload input cannot recreate pending released data"), F.First.ASC()->GetReplicatedDataOfGenericReplicatedEvent(
		EAbilityGenericReplicatedEvent::InputReleased, NewAction, OldKey).bTriggered);
	TestEqual(TEXT("Grants retained"), F.First.ASC()->GetActivatableAbilities().Num(), GrantCount);
	TestNotNull(TEXT("Permanent effects retained"), F.First.ASC()->GetActiveGameplayEffect(Effect));
	TestFalse(TEXT("Movement authorization revoked"), F.First.Security()->IsMovementAuthorizationActive());
	TestFalse(TEXT("Late movement task cannot open another grant"), F.First.Security()->BeginMovementAuthorization(1.f, 200.f, TEXT("LateDash")).IsValid());
	TestEqual(TEXT("Weapon window remains closed after cancellation callback"), Weapon->GetWeaponCollisionBox()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestEqual(TEXT("Movement remains disabled"), F.First.Movement()->MovementMode, TEnumAsByte<EMovementMode>(MOVE_None));
	TestTrue(TEXT("Velocity stopped"), F.First.Movement()->Velocity.IsNearlyZero());
	const FVector Position = F.First.Pawn->GetActorLocation();
	F.First.Movement()->SetMovementMode(MOVE_Flying);
	F.First.Movement()->Velocity = FVector(1000.f, 0.f, 0.f);
	F.First.Movement()->PerformMovement(.1f);
	FCharacterNetworkMoveDataContainer QueuedMoves;
	F.First.Movement()->ServerMove_HandleMoveData(QueuedMoves);
	TestTrue(TEXT("Retained Pawn ignores local/queued packed movement"), Position.Equals(F.First.Pawn->GetActorLocation()));
	TestTrue(TEXT("Pawn remains valid instead of immediate Iris object destruction"), IsValid(F.First.Pawn));
	TestEqual(TEXT("Revocation does not kill player"), F.First.Pawn->GetRPGAttributeSet()->GetCurrentHealth(), 100.f);
	TestFalse(TEXT("Revocation does not add death tag"), F.First.ASC()->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death));
	TestTrue(TEXT("Other party's active execution is retained"), F.Remaining.ASC()->FindAbilitySpecFromHandle(PartyAction)->IsActive());
	TestEqual(TEXT("Other party's movement is retained"), F.Remaining.Movement()->MovementMode, PartyMode);
	TestFalse(TEXT("Another ASC cannot inherit revocation"), F.Remaining.ASC()->RevokeDungeonGameplayAuthority(*F.First.State));
	F.First.Pawn->SetPlayerState(nullptr);
	TestTrue(TEXT("Pawn-local barrier survives PlayerState detachment"), IsDungeonGameplayRevoked(F.First.Pawn));
	TestFalse(TEXT("Detached old Pawn cannot activate"), F.First.ASC()->TryActivateAbility(NewAction));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonRevokedDamageTest,
	"ProjectRPG.Online.DungeonAuthorityRevocation.LateDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonRevokedDamageTest::RunTest(const FString&)
{
	using namespace RPGDungeonGameplayRevocationTests;
	FFixture F;
	const auto Outgoing = MakeBossTestDamageSpec(F.First.ASC(), 7.f);
	const auto Incoming = MakeBossTestDamageSpec(F.Remaining.ASC(), 7.f);
	const auto Apply = [](UAbilitySystemComponent* Source, UAbilitySystemComponent* Target, const FGameplayEffectSpecHandle& Spec)
	{
		FRPGDamageExecutionGuard Receipt(*Spec.Data, *Source, *Target, 100.f);
		Source->ApplyGameplayEffectSpecToTarget(*Spec.Data, Target);
		return Receipt.WasApproved();
	};
	TestTrue(TEXT("Production GAS damage positive control"), Apply(F.Remaining.ASC(), F.First.ASC(), Incoming));
	const float Before = F.First.Pawn->GetRPGAttributeSet()->GetCurrentHealth();
	TestTrue(TEXT("Positive control really changed health"), Before < 100.f);
	// Receipt created before revocation must be rechecked at the final execution gate.
	{
		FRPGDamageExecutionGuard PendingReceipt(*Outgoing.Data, *F.First.ASC(), *F.Remaining.ASC(), 100.f);
		TestTrue(TEXT("Revoke during pending damage permit"), F.First.Revoke());
		F.First.ASC()->ApplyGameplayEffectSpecToTarget(*Outgoing.Data, F.Remaining.ASC());
		TestFalse(TEXT("Previously granted permit cannot deal delayed damage"), PendingReceipt.WasApproved());
	}
	TestFalse(TEXT("Revoked target cannot receive delayed damage"), Apply(F.Remaining.ASC(), F.First.ASC(), Incoming));
	// Direct ASC application without a receipt also reaches the common execution gate.
	F.First.ASC()->ApplyGameplayEffectSpecToTarget(*Outgoing.Data, F.Remaining.ASC());
	F.Remaining.ASC()->ApplyGameplayEffectSpecToTarget(*Incoming.Data, F.First.ASC());
	TestEqual(TEXT("Other party HP unchanged by stale source"), F.Remaining.Pawn->GetRPGAttributeSet()->GetCurrentHealth(), 100.f);
	TestEqual(TEXT("Revoked player's HP unchanged by stale target"), F.First.Pawn->GetRPGAttributeSet()->GetCurrentHealth(), Before);
	const auto* Queries = F.World->GetSubsystem<URPGHitQuerySubsystem>();
	FRPGHitQueryFilter Filter;
	Filter.TeamRule = ERPGHitQueryTeamRule::Any; Filter.bRequireLineOfSight = false;
	TestFalse(TEXT("Shared query rejects revoked source"), Queries->IsTargetEligible(F.First.Pawn, F.Remaining.Pawn, FVector::ZeroVector, Filter));
	TestFalse(TEXT("Shared query rejects revoked target"), Queries->IsTargetEligible(F.Remaining.Pawn, F.First.Pawn, FVector::ZeroVector, Filter));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonRevokedThreatTest,
	"ProjectRPG.Online.DungeonAuthorityRevocation.ThreatAndParticipation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonRevokedThreatTest::RunTest(const FString&)
{
	using namespace RPGDungeonGameplayRevocationTests;
	FFixture F;
	auto* Boss = F.World->SpawnActor<ARPGBossTestPawn>();
	auto* AI = F.World->SpawnActor<ARPGAIController>();
	AI->Possess(Boss);
	auto* Threat = NewObject<URPGBossThreatComponent>(Boss); Threat->RegisterComponent();
	TestTrue(TEXT("Encounter started"), Boss->Boss->StartEncounter());
	const int32 Epoch = Boss->Boss->GetBossState().EncounterId;
	TestTrue(TEXT("Departing player threat positive control"), Threat->AddThreat(F.First.Pawn, 100.f, Epoch));
	TestTrue(TEXT("Remaining player threat positive control"), Threat->AddThreat(F.Remaining.Pawn, 10.f, Epoch));
	TestTrue(TEXT("Departing player was current target"), Threat->GetCurrentTarget() == F.First.Pawn);
	auto* Mode = F.World->SpawnActor<ARPGBossBattleGameMode>();
	FRPGDungeonGameplayRevocationTestAccess::AddParticipant(*Mode, F.First.Controller);
	FRPGDungeonGameplayRevocationTestAccess::AddParticipant(*Mode, F.Remaining.Controller);
	TestTrue(TEXT("Living participants positive control"), FRPGDungeonGameplayRevocationTestAccess::HasLivingParticipant(*Mode));
	TestTrue(TEXT("Revoke target without disconnect/death"), F.First.Revoke());
	TestNull(TEXT("Getter invalidates target before periodic refresh"), Threat->GetCurrentTarget());
	TestFalse(TEXT("Late threat cannot readmit revoked player"), Threat->AddThreat(F.First.Pawn, 500.f, Epoch));
	Threat->RefreshTargets();
	TestTrue(TEXT("Boss selects remaining party"), Threat->GetCurrentTarget() == F.Remaining.Pawn);
	TestEqual(TEXT("Remaining player's threat is preserved"), Threat->GetThreatForTarget(F.Remaining.Pawn), 10.0);
	TestEqual(TEXT("Revoked player has no threat"), Threat->GetThreatForTarget(F.First.Pawn), 0.0);
	TestEqual(TEXT("Revocation does not reset encounter"), Boss->Boss->GetBossState().EncounterId, Epoch);
	TestTrue(TEXT("Remaining living player prevents wipe"), FRPGDungeonGameplayRevocationTestAccess::HasLivingParticipant(*Mode));
	TestTrue(TEXT("Retry cleanup may process other participants"), FRPGDungeonGameplayRevocationTestAccess::ResetForRetry(*Mode));
	TestEqual(TEXT("Retry never restores revoked movement"), F.First.Movement()->MovementMode, TEnumAsByte<EMovementMode>(MOVE_None));
	TestTrue(TEXT("Remaining member can also revoke"), F.Remaining.Revoke());
	TestFalse(TEXT("Retained live revoked pawns cannot prevent party wipe"), FRPGDungeonGameplayRevocationTestAccess::HasLivingParticipant(*Mode));
	TestFalse(TEXT("Revoked participant cannot request retry"), Mode->TryRestartBattleForParticipant(F.First.Controller));
	return true;
}

#endif
