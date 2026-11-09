#include "Narrative/RPGNarrativeKillSubsystem.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameMode/RPGGameModeBase.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IHttpRequest.h"
#include "Manager/HttpWebManager.h"
#include "Narrative/RPGNarrativeProgressComponent.h"
#include "Player/RPGPlayerState.h"
#include "RPGGameplayTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogRPGNarrativeKill, Log, All);

void URPGNarrativeKillSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<UHttpWebManager>();
    Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), 0.25f);
}
bool URPGNarrativeKillSubsystem::RecordDeathInRadius(ARPGNonPlayerCharacter* Victim, const FString& EnemyId, const float Radius)
{
    if (!IsInGameThread()) return false;
    auto* World = GetWorld(); auto* Web = GetGameInstance()->GetSubsystem<UHttpWebManager>();
    auto* Mode = World ? World->GetAuthGameMode<ARPGGameModeBase>() : nullptr;
    const auto* ASC = IsValid(Victim) ? Victim->GetRPGAbilitySystemComponent() : nullptr;
    const auto* Attributes = IsValid(Victim) ? Victim->GetRPGAttributeSet() : nullptr;
    FGuid Session;
    if (!World || World->bIsTearingDown || World->GetNetMode() == NM_Client || !Mode || !Web
        || !Web->IsConfiguredForDungeonServer() || !FGuid::Parse(Web->GetConfiguredDungeonSessionId(), Session) || !Session.IsValid()
        || !IsValid(Victim) || Victim->GetWorld() != World || !Victim->HasAuthority() || Victim->IsActorBeingDestroyed()
        || !Victim->IsNPCStartUpDataReady() || !ASC || ASC->GetAvatarActor() != Victim || !Attributes
        || !FMath::IsFinite(Attributes->GetCurrentHealth()) || Attributes->GetCurrentHealth() != 0.f
        || !ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death)
        || !FRPGNarrativeInteractionCommand::IsValidId(EnemyId) || !FMath::IsFinite(Radius) || Radius < 100.f || Radius > 100000.f
        || Victim->GetActorLocation().ContainsNaN() || RecordedVictims.Contains(Victim)) return false;
    // Reserve this actor life before creating requests. A second component/callback cannot mint a new event.
    RecordedVictims.Add(Victim);
    const FGuid Event = FGuid::NewGuid(); const FDateTime ObservedAt = FDateTime::UtcNow();
    const FVector Origin = Victim->GetActorLocation(); const double RadiusSquared = FMath::Square(static_cast<double>(Radius));
    const auto VictimTeam = Victim->GetGenericTeamId();
    TSet<FGuid> Recipients; TArray<FPending> Approved;
    for (auto It = World->GetPlayerControllerIterator(); It; ++It)
    {
        auto* Controller = It->Get(); auto* State = Controller ? Controller->GetPlayerState<ARPGPlayerState>() : nullptr;
        APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
        const auto* Team = Cast<IGenericTeamAgentInterface>(Controller);
        FGuid Character, MemberSession;
        if (!IsValid(Controller) || !IsValid(State) || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || Pawn->GetPlayerState() != State
            || !State->HasAuthenticatedCharacter() || !Mode->HasConfirmedDungeonAdmission(State)
            || !FGuid::Parse(State->GetBackendCharacterId(), Character) || !Character.IsValid()
            || !FGuid::Parse(State->GetBackendDungeonSessionId(), MemberSession) || MemberSession != Session
            || State->GetBackendConnectionGeneration() <= 0 || Recipients.Contains(Character)
            || !Team || Team->GetGenericTeamId() == FGenericTeamId::NoTeam || VictimTeam == FGenericTeamId::NoTeam
            || Team->GetGenericTeamId() == VictimTeam || Pawn->GetActorLocation().ContainsNaN()
            || FVector::DistSquared(Origin, Pawn->GetActorLocation()) > RadiusSquared) continue;
        FRPGNarrativeKillCommand Command;
        Command.SessionId = Session; Command.CharacterId = Character; Command.EventId = Event;
        Command.EnemyId = EnemyId; Command.ObservedAt = ObservedAt; Command.ConnectionGeneration = State->GetBackendConnectionGeneration();
        FPending Entry;
        if (!Entry.Attempt.Begin(Command)) continue;
        Entry.Steam = State->GetAuthenticatedSteamId(); Entry.Server = Web->GetGameServerId();
        Entry.Endpoint = Web->GetApiUrl(); Entry.World = World;
        Approved.Add(MoveTemp(Entry)); Recipients.Add(Character);
    }
    if (Outbox.Num() + Approved.Num() > 1024)
    {
        UE_LOG(LogRPGNarrativeKill, Error, TEXT("Quest kill outbox is full; death observation was not queued")); return false;
    }
    // Freeze every recipient before the first HTTP callback can run. No radius/party re-evaluation on retry.
    for (auto& Entry : Approved) Outbox.Add(FGuid::NewGuid(), MoveTemp(Entry));
    return true;
}
bool URPGNarrativeKillSubsystem::ContextMatches(const FPending& Pending) const
{
    const auto* Web = GetGameInstance()->GetSubsystem<UHttpWebManager>(); const auto* Command = Pending.Attempt.GetCommand();
    const auto* World = Pending.World.Get(); FGuid Session;
    return World && World == GetWorld() && !World->bIsTearingDown && World->GetNetMode() != NM_Client && Web && Command
        && Web->IsConfiguredForDungeonServer() && Web->GetApiUrl() == Pending.Endpoint && Web->GetGameServerId() == Pending.Server
        && FGuid::Parse(Web->GetConfiguredDungeonSessionId(), Session) && Session == Command->SessionId;
}
void URPGNarrativeKillSubsystem::Dispatch(const FGuid Key)
{
    auto* Entry = Outbox.Find(Key);
    if (!Entry || Entry->Request || !ContextMatches(*Entry) || !Entry->Attempt.StartRequest()) return;
    auto* Web = GetGameInstance()->GetSubsystem<UHttpWebManager>();
    const uint64 Generation = ++Entry->RequestGeneration;
    const TWeakObjectPtr<ThisClass> WeakThis(this);
    Entry->Request = Web->CreateServerNarrativeKillRequest(*Entry->Attempt.GetCommand(), Entry->Attempt.GetBody(),
        [WeakThis, Key, Generation](FRPGNarrativeInteractionHttpResult Result) { if (auto* Self = WeakThis.Get()) Self->Receive(Key, Generation, MoveTemp(Result)); });
    auto Request = Entry->Request;
    if (!Request || !Request->ProcessRequest()) Receive(Key, Generation, {});
}
void URPGNarrativeKillSubsystem::Receive(const FGuid Key, const uint64 Generation, FRPGNarrativeInteractionHttpResult Result)
{
    auto* Entry = Outbox.Find(Key); if (!Entry || Entry->RequestGeneration != Generation || !Entry->Attempt.IsInFlight()) return;
    const auto Command = *Entry->Attempt.GetCommand(); const FString Steam = Entry->Steam;
    Entry->Request.Reset(); Result.bCurrentServerContext &= ContextMatches(*Entry);
    const auto Outcome = Entry->Attempt.Receive(Result);
    Entry->NextRetry = FPlatformTime::Seconds() + Entry->Attempt.RetryDelay();
    if (Outcome == ERPGNarrativeInteractionOutcome::Unknown) return;
    Outbox.Remove(Key);
    if (Outcome != ERPGNarrativeInteractionOutcome::Succeeded) return;
    auto* World = GetWorld(); if (!World || World->bIsTearingDown) return;
    for (auto It = World->GetPlayerControllerIterator(); It; ++It)
    {
        auto* Controller = It->Get(); const auto* State = Controller ? Controller->GetPlayerState<ARPGPlayerState>() : nullptr;
        FGuid Character, Session;
        if (State && State->HasAuthenticatedCharacter() && State->GetAuthenticatedSteamId() == Steam
            && FGuid::Parse(State->GetBackendCharacterId(), Character) && Character == Command.CharacterId
            && FGuid::Parse(State->GetBackendDungeonSessionId(), Session) && Session == Command.SessionId)
            if (auto* Notice = Controller->FindComponentByClass<URPGNarrativeProgressComponent>()) Notice->NotifyCommitted(Character, Session);
    }
}
void URPGNarrativeKillSubsystem::Cancel(const FGuid Key)
{
    if (auto* Entry = Outbox.Find(Key); Entry && Entry->Request)
    { Entry->Request->OnProcessRequestComplete().Unbind(); Entry->Request->CancelRequest(); }
    Outbox.Remove(Key);
}
bool URPGNarrativeKillSubsystem::Tick(float DeltaSeconds)
{
    for (auto It = RecordedVictims.CreateIterator(); It; ++It) if (!It->IsValid()) It.RemoveCurrent();
    TArray<FGuid> Keys; Outbox.GenerateKeyArray(Keys);
    int32 InFlight = 0; for (const auto& Pair : Outbox) if (Pair.Value.Attempt.IsInFlight()) ++InFlight;
    for (const auto Key : Keys)
    {
        const auto* Entry = Outbox.Find(Key); if (!Entry) continue;
        if (!ContextMatches(*Entry)) { Cancel(Key); continue; }
        if (InFlight < 4 && !Entry->Attempt.IsInFlight() && FPlatformTime::Seconds() >= Entry->NextRetry)
        { Dispatch(Key); ++InFlight; }
    }
    return true;
}
void URPGNarrativeKillSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
    TArray<FGuid> Keys; Outbox.GenerateKeyArray(Keys); for (const auto Key : Keys) Cancel(Key);
    RecordedVictims.Empty(); Super::Deinitialize();
}
