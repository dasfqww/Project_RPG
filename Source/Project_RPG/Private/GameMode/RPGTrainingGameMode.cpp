#include "GameMode/RPGTrainingGameMode.h"

#include "Character/RPGPlayer.h"
#include "Component/Progression/RPGTrainingObjectiveComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Player/RPGPlayerState.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#include "TimerManager.h"

ARPGTrainingGameMode::ARPGTrainingGameMode()
{
	bGiveReward = true;
	CurrentGameDifficulty = ERPGGameDifficulty::Normal;
}

void ARPGTrainingGameMode::BeginPlay()
{
	// Observe the full startup roster before the common base sends its asynchronous start request.
	if (HasAuthority() && IsRunningDedicatedServer())
		if (auto* Instance = GetGameInstance())
			if (auto* Web = Instance->GetSubsystem<UHttpWebManager>())
			{
				ObservedWeb = Web;
				Web->OnDungeonSessionUpdated.AddUniqueDynamic(this, &ThisClass::HandleTrainingSessionUpdated);
			}
	Super::BeginPlay();
	if (!bTrainingEnding && HasAuthority() && IsRunningDedicatedServer())
	{
		RefreshBindings();
		GetWorldTimerManager().SetTimer(TrainingCompletionTimer, this,
			&ThisClass::ReportConfiguredDungeonClear, 0.25f, true);
	}
}

void ARPGTrainingGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bTrainingEnding = true;
	GetWorldTimerManager().ClearTimer(TrainingCompletionTimer);
	if (auto* Web = ObservedWeb.Get())
		Web->OnDungeonSessionUpdated.RemoveDynamic(this, &ThisClass::HandleTrainingSessionUpdated);
	ObservedWeb.Reset();
	for (const auto& Observation : Observations)
		if (auto* Objectives = Observation.Objectives.Get())
			Objectives->OnTrainingProgressChanged.RemoveDynamic(this, &ThisClass::HandleTrainingProgressChanged);
	Observations.Reset();
	KnownMembers.Reset();
	Super::EndPlay(EndPlayReason);
}

void ARPGTrainingGameMode::RestartPlayer(AController* NewPlayer)
{
	Super::RestartPlayer(NewPlayer);
	// Bind before the new Pawn can receive its first player input; timer discovery also handles replacement.
	if (!bTrainingEnding && !bCompletionSubmitted && HasAuthority() && IsRunningDedicatedServer()) RefreshBindings();
}

bool ARPGTrainingGameMode::IsTrainingScopeCurrent(const UHttpWebManager*& OutWeb) const
{
	const auto* World = GetWorld();
	OutWeb = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UHttpWebManager>() : nullptr;
	return !bTrainingEnding && !bCompletionSubmitted && HasAuthority() && IsRunningDedicatedServer()
		&& IsValid(World) && !World->bIsTearingDown && World->GetAuthGameMode() == this
		&& World->GetNetMode() == NM_DedicatedServer && IsBackendDungeonDifficultyReady()
		&& GetConfirmedDungeonContentId() == TEXT("Progression.Training")
		&& GetCurrentGameDifficulty() == ERPGGameDifficulty::Normal && bGiveReward
		&& OutWeb && OutWeb->IsConfiguredForDungeonServer();
}

bool ARPGTrainingGameMode::IsObservedLifetimeCurrent(const FMemberObservation& Observation) const
{
	const UHttpWebManager* Web = nullptr;
	if (!IsTrainingScopeCurrent(Web)) return false;
	auto* Pawn = Observation.Pawn.Get();
	const auto* State = IsValid(Pawn) ? Pawn->GetPlayerState<ARPGPlayerState>() : nullptr;
	const auto* Controller = IsValid(Pawn) ? Pawn->GetController() : nullptr;
	const auto* Growth = IsValid(Pawn) ? Pawn->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	const auto* Class = Growth ? Growth->GetPlayerClassDefinition() : nullptr;
	return IsValid(Pawn) && Pawn->GetWorld() == GetWorld() && !Pawn->IsActorBeingDestroyed()
		&& IsValid(State) && State->GetWorld() == GetWorld() && State == Observation.PlayerState.Get() && !State->IsInactive()
		&& State->HasAuthenticatedCharacter() && State->GetBackendConnectionGeneration() > 0
		&& State->GetBackendConnectionGeneration() == Observation.ConnectionGeneration
		&& State->GetBackendDungeonSessionId() == Observation.SessionId
		&& State->GetBackendDungeonSessionId() == Web->GetConfiguredDungeonSessionId()
		&& State->GetBackendCharacterId() == Observation.CharacterId && State->GetAuthenticatedSteamId() == Observation.SteamId
		&& IsValid(Controller) && Controller == Observation.Controller.Get() && Controller->GetWorld() == GetWorld()
		&& Controller->GetPawn() == Pawn && Controller->PlayerState == State && State->GetOwner() == Controller
		&& !IsDungeonGameplayRevoked(Controller)
		&& Growth && Growth->GetSkillLoadState() == ERPGPlayerSkillLoadState::Ready && !Growth->IsConfigurationMutable()
		&& Growth->GetSkillRestoreGeneration() == Observation.RestoreGeneration
		&& Class && Class->ClassId == FName(TEXT("GreatSwordWarrior"))
		&& Observation.Objectives.IsValid() && Observation.Objectives->GetOwner() == Pawn
		&& Pawn->FindComponentByClass<URPGTrainingObjectiveComponent>() == Observation.Objectives.Get()
		&& !IsDungeonPartyMemberRevoked(Observation.SessionId, Observation.CharacterId, Observation.SteamId)
		&& Observation.Lifetime.IsCurrent(Pawn, Pawn);
}

void ARPGTrainingGameMode::CaptureObservedLifetime(FMemberObservation& Observation)
{
	auto* Pawn = Observation.Pawn.Get();
	auto* State = Pawn ? Pawn->GetPlayerState<ARPGPlayerState>() : nullptr;
	const auto* Growth = Pawn ? Pawn->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	Observation.PlayerState = State;
	Observation.Controller = Pawn ? Pawn->GetController() : nullptr;
	Observation.SessionId = State ? State->GetBackendDungeonSessionId() : FString();
	Observation.CharacterId = State ? State->GetBackendCharacterId() : FString();
	Observation.SteamId = State ? State->GetAuthenticatedSteamId() : FString();
	Observation.ConnectionGeneration = State ? State->GetBackendConnectionGeneration() : 0;
	Observation.RestoreGeneration = Growth ? Growth->GetSkillRestoreGeneration() : 0;
	Observation.Lifetime = FRPGApprovedHitContext(Pawn, Pawn);
	Observation.CompletedAtSeconds = 0.0;
}

void ARPGTrainingGameMode::RefreshBindings()
{
	auto* World = GetWorld();
	if (!IsValid(World) || World->bIsTearingDown || World->GetAuthGameMode() != this) return;
	TSet<URPGTrainingObjectiveComponent*> Present;
	for (auto It = World->GetPlayerControllerIterator(); It; ++It)
	{
		auto* Controller = It->Get();
		auto* Pawn = IsValid(Controller) ? Cast<ARPGPlayer>(Controller->GetPawn()) : nullptr;
		auto* Objectives = IsValid(Pawn) ? Pawn->FindComponentByClass<URPGTrainingObjectiveComponent>() : nullptr;
		if (!IsValid(Objectives) || Pawn->IsActorBeingDestroyed()) continue;
		Present.Add(Objectives);
		if (Observations.ContainsByPredicate([Objectives](const FMemberObservation& Entry) { return Entry.Objectives.Get() == Objectives; })) continue;
		FMemberObservation Observation;
		Observation.Pawn = Pawn;
		Observation.Objectives = Objectives;
		const auto Progress = Objectives->GetTrainingProgress();
		Observation.bLastComplete = Progress.IsComplete();
		// Never manufacture a completion event for a component discovered already complete.
		if (Progress.bActive && !Progress.IsComplete()) CaptureObservedLifetime(Observation);
		Observations.Add(MoveTemp(Observation));
		Objectives->OnTrainingProgressChanged.AddUniqueDynamic(this, &ThisClass::HandleTrainingProgressChanged);
	}
	for (int32 Index = Observations.Num() - 1; Index >= 0; --Index)
	{
		if (Present.Contains(Observations[Index].Objectives.Get())) continue;
		if (auto* Objectives = Observations[Index].Objectives.Get())
			Objectives->OnTrainingProgressChanged.RemoveDynamic(this, &ThisClass::HandleTrainingProgressChanged);
		Observations.RemoveAtSwap(Index);
	}
}

void ARPGTrainingGameMode::HandleTrainingProgressChanged(const FRPGTrainingObjectiveProgress&)
{
	// The delegate has no sender. Inspect bound sources, and only record their new complete transitions.
	// Defer settlement until the timer, outside the GAS/delegate notification stack.
	ReadProgress(true);
}

void ARPGTrainingGameMode::HandleTrainingSessionUpdated(const FRPGDungeonSession& Session, bool bSuccess)
{
	const auto* Web = ObservedWeb.Get();
	if (!bSuccess || bTrainingEnding || bCompletionSubmitted || !HasAuthority() || !IsRunningDedicatedServer()
		|| !Web || !Web->IsConfiguredForDungeonServer() || Session.DungeonSessionId != Web->GetConfiguredDungeonSessionId()
		|| Session.ServerId != Web->GetGameServerId() || Session.DungeonId != TEXT("Progression.Training")
		|| Session.Difficulty != TEXT("Normal") || Session.State != TEXT("InProgress")) return;
	if ((!KnownSessionId.IsEmpty() && KnownSessionId != Session.DungeonSessionId)
		|| (!KnownServerId.IsEmpty() && KnownServerId != Session.ServerId)) { bRosterConflict = true; return; }
	KnownSessionId = Session.DungeonSessionId;
	KnownServerId = Session.ServerId;
	TSet<FString> Characters;
	for (const auto& Member : Session.Members)
	{
		FGuid Character;
		if (!FGuid::Parse(Member.CharacterId, Character) || !Character.IsValid() || Characters.Contains(Member.CharacterId))
		{ bRosterConflict = true; return; }
		Characters.Add(Member.CharacterId);
		// Session responses have no SteamId. Only the authenticated membership snapshot fills it in.
		KnownMembers.FindOrAdd(Member.CharacterId);
	}
	if (Characters.IsEmpty() || Characters.Num() > 4) { bRosterConflict = true; return; }
	bHasStartRoster = true;
}

void ARPGTrainingGameMode::ReadProgress(bool bFromProgressEvent)
{
	if (bTrainingEnding || bCompletionSubmitted || bReadingProgress) return;
	TGuardValue<bool> Guard(bReadingProgress, true);
	for (auto& Observation : Observations)
	{
		const auto* Objectives = Observation.Objectives.Get();
		if (!Objectives) { Observation.CompletedAtSeconds = 0.0; continue; }
		const auto Progress = Objectives->GetTrainingProgress();
		bool bCurrent = IsObservedLifetimeCurrent(Observation);
		if (!Progress.IsComplete()) Observation.CompletedAtSeconds = 0.0;
		if (!bCurrent)
		{
			Observation.CompletedAtSeconds = 0.0;
			// Rebind a new observation lifetime only at its empty, active start. Old partial/full progress cannot migrate.
			if (Progress.bActive && !Progress.bBasicAttackCompleted && !Progress.bEvadeCompleted
				&& !Progress.bAssaultCompleted && !Progress.bChargeCompleted)
			{
				CaptureObservedLifetime(Observation);
				bCurrent = IsObservedLifetimeCurrent(Observation);
			}
		}
		if (bFromProgressEvent && bCurrent && !Observation.bLastComplete && Progress.IsComplete())
			Observation.CompletedAtSeconds = FPlatformTime::Seconds();
		Observation.bLastComplete = Progress.IsComplete();
	}
}

void ARPGTrainingGameMode::ReportConfiguredDungeonClear()
{
	const UHttpWebManager* Web = nullptr;
	if (!IsTrainingScopeCurrent(Web)) return;
	RefreshBindings();
	ReadProgress(false);
	FRPGDungeonMembershipPresentation Membership;
	if (!GetDungeonMembershipPresentation(Web->GetConfiguredDungeonSessionId(), Membership) || Membership.bStale
		|| Membership.Snapshot.ServerId != Web->GetGameServerId()) return;
	const auto& Snapshot = Membership.Snapshot;
	if (!bHasStartRoster || bRosterConflict || KnownSessionId != Snapshot.DungeonSessionId || KnownServerId != Snapshot.ServerId) return;
	TMap<FString, FString> CurrentMembers;
	TSet<FString> SteamIds;
	for (const auto& Member : Snapshot.Members)
	{
		if (Member.CharacterId.IsEmpty() || Member.SteamId.IsEmpty() || CurrentMembers.Contains(Member.CharacterId)
			|| SteamIds.Contains(Member.SteamId)) return;
		const auto* KnownSteam = KnownMembers.Find(Member.CharacterId);
		if (KnownSteam && !KnownSteam->IsEmpty() && *KnownSteam != Member.SteamId) return;
		CurrentMembers.Add(Member.CharacterId, Member.SteamId);
		SteamIds.Add(Member.SteamId);
		KnownMembers.Add(Member.CharacterId, Member.SteamId);
	}
	// A member removed before the first GET still needs an exact immutable exclusion receipt.
	for (const auto& Abandonment : Snapshot.Abandonments)
	{
		auto* KnownSteam = KnownMembers.Find(Abandonment.Receipt.CharacterId);
		if (!KnownSteam || !Snapshot.IsExplicitlyAbandoned(KnownSessionId, KnownServerId,
			Abandonment.Receipt.CharacterId, Abandonment.SteamId)) continue;
		if (!KnownSteam->IsEmpty() && *KnownSteam != Abandonment.SteamId) return;
		*KnownSteam = Abandonment.SteamId;
	}
	for (const auto& Expulsion : Snapshot.DisconnectionExpulsions)
	{
		auto* KnownSteam = KnownMembers.Find(Expulsion.CharacterId);
		if (!KnownSteam || !Snapshot.IsDisconnectedExpelled(KnownSessionId, KnownServerId, Expulsion.CharacterId, Expulsion.SteamId)) continue;
		if (!KnownSteam->IsEmpty() && *KnownSteam != Expulsion.SteamId) return;
		*KnownSteam = Expulsion.SteamId;
	}
	for (const auto& Known : KnownMembers)
	{
		if (CurrentMembers.Contains(Known.Key)) continue;
		if (!Snapshot.IsExplicitlyAbandoned(KnownSessionId, KnownServerId, Known.Key, Known.Value)
			&& !Snapshot.IsDisconnectedExpelled(KnownSessionId, KnownServerId, Known.Key, Known.Value)) return;
	}
	if (Snapshot.State != TEXT("InProgress") || CurrentMembers.IsEmpty() || CurrentMembers.Num() > 4
		|| !Membership.bHasRequestClock || !FMath::IsFinite(Membership.RequestStartedAtSeconds)
		|| Membership.RequestStartedAtSeconds <= 0.0 || !FMath::IsFinite(Membership.ReceivedAtSeconds)
		|| Membership.ReceivedAtSeconds < Membership.RequestStartedAtSeconds) return;
	double LatestCompletion = 0.0;
	for (const auto& Member : Snapshot.Members)
	{
		// Optional presentation fields never grant admission. Unconfirmed/missing generation or reconnect grace blocks clear.
		if (!Member.bHasConnectionPresentation || Member.ConnectionGeneration <= 0 || !Member.DisconnectedAt.IsEmpty()) return;
		const FMemberObservation* Match = nullptr;
		for (const auto& Observation : Observations)
		{
			if (Observation.CharacterId != Member.CharacterId || Observation.SteamId != Member.SteamId
				|| Observation.ConnectionGeneration != Member.ConnectionGeneration || !IsObservedLifetimeCurrent(Observation)) continue;
			if (Match) return; // Ambiguous live identity must not collapse to one participant.
			Match = &Observation;
		}
		if (!Match || !Match->Objectives->IsControlPracticeComplete() || Match->CompletedAtSeconds <= 0.0
			|| !FMath::IsFinite(Match->CompletedAtSeconds)) return;
		LatestCompletion = FMath::Max(LatestCompletion, Match->CompletedAtSeconds);
	}
	// Require a successful existing membership GET started after every current member's completion event.
	if (Membership.RequestStartedAtSeconds < LatestCompletion || !Web->CanDispatchConfiguredDungeonConnectionMutation()) return;
	bCompletionSubmitted = true;
	GetWorldTimerManager().ClearTimer(TrainingCompletionTimer);
	UE_LOG(LogTemp, Log, TEXT("RPG_TRAINING_PARTY_COMPLETE Session=%s Members=%d"), *KnownSessionId, CurrentMembers.Num());
	Super::ReportConfiguredDungeonClear(); // Existing pinned rewards, idempotency, retries and XP persistence remain the owners.
}
