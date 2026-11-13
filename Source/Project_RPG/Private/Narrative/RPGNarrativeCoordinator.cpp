#include "Narrative/RPGNarrativeCoordinator.h"
#include "Narrative/RPGQuestManager.h"
#include "Narrative/RPGScenarioManager.h"
#include "Narrative/RPGDialogueManager.h"
#include "Narrative/RPGNarrativeTransport.h"
#include "Narrative/RPGNarrativeHistory.h"
#include "Narrative/RPGNarrativeStorySkip.h"
#include "Narrative/RPGNarrativeQuestTracking.h"
#include "Narrative/RPGNarrativeTranscript.h"
#include "UI/Narrative/RPGNarrativeQuestTrackerWidget.h"
#include "GameFramework/PlayerController.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Manager/HttpWebManager.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void URPGNarrativeCoordinator::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection); bShuttingDown = false;
    Collection.InitializeDependency<URPGDialogueManager>();
    BindManagers();
    if (DungeonFlow)
    {
        DungeonFlow->OnDungeonSessionChanged.AddDynamic(this, &ThisClass::HandleDungeonSessionChanged);
        DungeonFlow->OnFlowStateChanged.AddDynamic(this, &ThisClass::HandleDungeonFlowChanged);
        HandleDungeonSessionChanged(DungeonFlow->GetCurrentDungeonSession());
    }
}
bool URPGNarrativeCoordinator::BindManagers()
{
    auto* Player = GetLocalPlayer(); auto* Instance = Player ? Player->GetGameInstance() : nullptr;
    Quests = Instance ? Instance->GetSubsystem<URPGQuestManager>() : nullptr;
    Scenarios = Instance ? Instance->GetSubsystem<URPGScenarioManager>() : nullptr;
    Dialogues = Player ? Player->GetSubsystem<URPGDialogueManager>() : nullptr;
    DungeonFlow = Instance ? Instance->GetSubsystem<URPGDungeonSessionSubsystem>() : nullptr;
    return Quests && Scenarios && Dialogues;
}
bool URPGNarrativeCoordinator::EnsureTransport()
{
    if (Transport) return true;
    auto* Player = GetLocalPlayer(); auto* Instance = Player ? Player->GetGameInstance() : nullptr;
    auto* Web = Instance ? Instance->GetSubsystem<UHttpWebManager>() : nullptr;
    if (!Web) return false;
    Transport = MakeShared<FRPGHttpNarrativeTransport>(Web); return true;
}
bool URPGNarrativeCoordinator::ContextMatches() const
{
    FString CurrentEndpoint, CurrentSteam;
    return !bShuttingDown && IsInGameThread() && Transport && SelectedCharacter.IsValid()
        && Transport->GetContext(CurrentEndpoint, CurrentSteam) && CurrentEndpoint == Endpoint && CurrentSteam == SteamId;
}
bool URPGNarrativeCoordinator::HasAuthenticatedViewContext() const
{
    if (!ContextMatches()) return false;
    const auto* Player = GetLocalPlayer();
    const auto* Instance = Player ? Player->GetGameInstance() : nullptr;
    const auto* Web = Instance ? Instance->GetSubsystem<UHttpWebManager>() : nullptr;
    FString CurrentEndpoint, CurrentSteam;
    // Stable identity survives invalid credentials so uncertain commands can be recovered.
    // Showing cached personal text additionally requires a currently usable player token.
    return Web && Web->HasAccessToken() && Web->GetPlayerEquipmentContext(CurrentEndpoint, CurrentSteam)
        && CurrentEndpoint == Endpoint && CurrentSteam == SteamId;
}
TSharedPtr<FRPGNarrativeHistoryClient> URPGNarrativeCoordinator::GetAccountHistoryClient()
{
    if (bShuttingDown || !EnsureTransport()) return nullptr;
    if (!HistoryClient)
    {
        auto* Player = GetLocalPlayer(); auto* Instance = Player ? Player->GetGameInstance() : nullptr;
        auto* Web = Instance ? Instance->GetSubsystem<UHttpWebManager>() : nullptr;
        if (!Web) return nullptr;
        const TWeakObjectPtr<ThisClass> WeakThis(this);
        HistoryClient = MakeShared<FRPGNarrativeHistoryClient>(MakeShared<FRPGHttpNarrativeTransport>(Web),
            [WeakThis](const FGuid Id)
            {
                const auto* Owner = WeakThis.Get(); FGuid ActualSelection;
                return Owner && Owner->SelectedCharacter == Id && Owner->HasAuthenticatedViewContext() && Owner->DungeonFlow
                    && FGuid::Parse(Owner->DungeonFlow->GetSelectedCharacterId(), ActualSelection) && ActualSelection == Id;
            });
    }
    return HistoryClient;
}
bool URPGNarrativeCoordinator::HasCurrentSnapshot() const
{
    return bHasView && HasAuthenticatedViewContext() && Quests && Scenarios && Dialogues
        && Quests->HasRevision(SelectedCharacter, Revision) && Scenarios->HasRevision(SelectedCharacter, Revision)
        && Dialogues->Readable();
}
bool URPGNarrativeCoordinator::IsBusy() const { return bBusy || (SkipClient && SkipClient->IsBusy()); }
bool URPGNarrativeCoordinator::HasPendingChoice() const
{ return PendingChoice.IsSet() || PendingDungeon.IsSet() || (SkipClient && SkipClient->HasPending()); }
TSharedPtr<FRPGNarrativeSkipClient> URPGNarrativeCoordinator::GetStorySkipClient()
{
    if (bShuttingDown || !EnsureTransport()) return nullptr;
    if (!SkipClient)
    {
        auto* Player = GetLocalPlayer(); auto* Instance = Player ? Player->GetGameInstance() : nullptr;
        auto* Web = Instance ? Instance->GetSubsystem<UHttpWebManager>() : nullptr;
        if (!Web) return nullptr;
        const TWeakObjectPtr<ThisClass> WeakThis(this);
        SkipClient = MakeShared<FRPGNarrativeSkipClient>(MakeShared<FRPGHttpNarrativeTransport>(Web),
            [WeakThis](FGuid Id, int64& OutRevision, int32& OutCatalog, bool bNew)
            {
                auto* Owner = WeakThis.Get(); FGuid Selected;
                if (!Owner || Owner->SelectedCharacter != Id || !Owner->ContextMatches() || !Owner->DungeonFlow
                    || !FGuid::Parse(Owner->DungeonFlow->GetSelectedCharacterId(), Selected) || Selected != Id
                    || Owner->bBusy || Owner->PendingChoice || Owner->PendingDungeon) return false;
                OutRevision = Owner->Revision; OutCatalog = Owner->CatalogVersion;
                return !bNew || (Owner->HasCurrentSnapshot() && !Owner->DungeonFlow->HasActiveDungeonSession()
                    && !Owner->DungeonFlow->IsRequestInFlight());
            },
            [WeakThis](FGuid Id, const FString& Chapter)
            {
                const auto* Owner = WeakThis.Get();
                return Owner && Owner->Scenarios && Owner->Scenarios->IsChapterCompleted(Id, Chapter);
            },
            [WeakThis](FRPGNarrativeSnapshot Snapshot)
            {
                if (auto* Owner = WeakThis.Get())
                {
                    const FGuid Id = Snapshot.CharacterId; Owner->Apply(MoveTemp(Snapshot));
                    if (Owner->SelectedCharacter == Id && Owner->ContextMatches()) Owner->ScheduleDungeonSync(true, true);
                }
            });
        SkipClient->OnChanged.AddLambda([WeakThis]
        {
            if (auto* Owner = WeakThis.Get(); Owner && !Owner->bShuttingDown)
            {
                Owner->OnChanged.Broadcast();
                // The one-shot dungeon ticker may have deferred work while this client was busy.
                if (!Owner->IsBusy() && !Owner->HasPendingChoice()
                    && (Owner->bDungeonRefreshNeeded || !Owner->DungeonCandidates.IsEmpty())) Owner->ScheduleDungeonSync(false);
            }
        });
    }
    return SkipClient;
}
TSharedPtr<FRPGNarrativeTranscript> URPGNarrativeCoordinator::GetTranscript()
{
    if (bShuttingDown) return nullptr;
    if (!Transcript)
    {
        const TWeakObjectPtr<ThisClass> WeakThis(this);
        Transcript = MakeShared<FRPGNarrativeTranscript>([WeakThis](FRPGNarrativeTranscriptContext& Out)
        {
            const auto* Owner = WeakThis.Get(); FGuid Selected;
            if (!Owner || !Owner->HasAuthenticatedViewContext() || !Owner->DungeonFlow
                || !FGuid::Parse(Owner->DungeonFlow->GetSelectedCharacterId(), Selected) || Selected != Owner->SelectedCharacter) return false;
            Out.Character = Selected; Out.Account = Owner->SteamId; Out.Endpoint = Owner->Endpoint;
            Out.bHasActive = Owner->HasCurrentSnapshot() && Owner->Dialogues->GetActiveDialogue(Out.Active);
            return true;
        });
    }
    return Transcript;
}
TSharedPtr<FRPGNarrativeQuestTracking> URPGNarrativeCoordinator::GetQuestTracking()
{
    if (bShuttingDown) return nullptr;
    if (!QuestTracking)
    {
        const TWeakObjectPtr<ThisClass> WeakThis(this);
        QuestTracking = MakeShared<FRPGNarrativeQuestTracking>([WeakThis](FRPGNarrativeTrackingContext& Out)
        {
            const auto* Owner = WeakThis.Get(); FGuid Selected;
            if (!Owner || !Owner->HasAuthenticatedViewContext() || !Owner->DungeonFlow
                || !FGuid::Parse(Owner->DungeonFlow->GetSelectedCharacterId(), Selected) || Selected != Owner->SelectedCharacter) return false;
            Out.Character = Selected; Out.Endpoint = Owner->Endpoint; Out.Account = Owner->SteamId;
            Out.bReadable = Owner->HasCurrentSnapshot();
            if (Out.bReadable) Out.Quests = Owner->Quests->GetQuests(Selected);
            return true;
        });
        TrackingTick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickQuestTracking), .25f);
    }
    return QuestTracking;
}
bool URPGNarrativeCoordinator::TickQuestTracking(float)
{
    if (bShuttingDown || !QuestTracking) return false;
    FRPGQuestProgress Quest;
    auto* Player = GetLocalPlayer();
    auto* PC = Player ? Player->GetPlayerController(Player->GetWorld()) : nullptr;
    if (TrackingHUD && (!PC || TrackingHUD->GetOwningPlayer() != PC || !QuestTracking->Read(Quest)))
    { TrackingHUD->RemoveFromParent(); TrackingHUD = nullptr; }
    if (PC && QuestTracking->Read(Quest) && !TrackingHUD)
    {
        TrackingHUD = CreateWidget<URPGNarrativeQuestTrackerWidget>(PC);
        if (TrackingHUD)
        {
            TrackingHUD->Configure(QuestTracking, FGuid(), false);
            if (!TrackingHUD->AddToPlayerScreen()) TrackingHUD = nullptr;
        }
    }
    // Travel can remove a viewport widget while the LocalPlayer/model survives.
    if (TrackingHUD && !TrackingHUD->IsInViewport())
    { TrackingHUD->Configure(QuestTracking, FGuid(), false); TrackingHUD->AddToPlayerScreen(); }
    return true;
}
void URPGNarrativeCoordinator::ClearView()
{
    bHasView = false; Revision = 0; CatalogVersion = 0; DungeonCandidates.Reset();
    if (Dialogues) Dialogues->Reset();
}
bool URPGNarrativeCoordinator::SelectCharacter(const FGuid CharacterId)
{
    if (!IsInGameThread() || bShuttingDown || IsBusy() || !CharacterId.IsValid() || !EnsureTransport()) return false;
    FString CurrentEndpoint, CurrentSteam;
    if (!Transport->GetContext(CurrentEndpoint, CurrentSteam)) return false;
    // Resolve an uncertain command before switching within the same account/endpoint.
    // Another account cannot inherit this account's display or pending request.
    if (HasPendingChoice() && CurrentEndpoint == Endpoint && CurrentSteam == SteamId) return false;
    if (!BindManagers()) return false;
    if (HistoryClient) HistoryClient->Reset();
    if (SkipClient) SkipClient->Reset();
    ++RequestGeneration; Transport->Cancel(); PendingChoice.Reset(); PendingDungeon.Reset(); PendingBody.Reset();
    FTSTicker::GetCoreTicker().RemoveTicker(DungeonSyncTick); DungeonSyncTick.Reset(); bDungeonRefreshNeeded = false; LastObservedDungeonResult.Invalidate();
    SelectedCharacter = CharacterId; Endpoint = MoveTemp(CurrentEndpoint); SteamId = MoveTemp(CurrentSteam);
    ClearView(); OnChanged.Broadcast(); return true;
}
bool URPGNarrativeCoordinator::Refresh()
{
    if (IsBusy() || HasPendingChoice() || !ContextMatches()) return false;
    DungeonSyncBudget = 256;
    bDungeonRefreshNeeded = false;
    return Dispatch(false);
}
bool URPGNarrativeCoordinator::BeginDialogue(const FString& DialogueId)
{
    if (!HasCurrentSnapshot() || IsBusy() || HasPendingChoice()
        || !Dialogues->Dialogues.ContainsByPredicate([&](const auto& D) { return D.Id == DialogueId && D.bEntryPoint; })) return false;
    Dialogues->ActiveId = DialogueId; OnChanged.Broadcast(); return true;
}
void URPGNarrativeCoordinator::CloseDialogue()
{
    if (!IsInGameThread() || bShuttingDown) return;
    if (Dialogues) { Dialogues->ActiveId.Reset(); OnChanged.Broadcast(); }
}
bool URPGNarrativeCoordinator::Choose(const FString& ChoiceId)
{
    FRPGDialogueNode Active;
    if (!HasCurrentSnapshot() || IsBusy() || HasPendingChoice() || !Dialogues->GetActiveDialogue(Active)
        || !Active.Choices.ContainsByPredicate([&](const auto& C) { return C.Id == ChoiceId; })) return false;
    if (Active.bInformational)
    {
        const auto* Choice = Active.Choices.FindByPredicate([&](const auto& C) { return C.Id == ChoiceId; });
        if (Choice->NextDialogueId.IsEmpty()) { CloseDialogue(); return true; }
        if (!Dialogues->Dialogues.ContainsByPredicate([&](const auto& D) { return D.Id == Choice->NextDialogueId && D.bInformational; })) return false;
        Dialogues->ActiveId = Choice->NextDialogueId; OnChanged.Broadcast(); return true;
    }
    FRPGNarrativeCommand Command{FGuid::NewGuid(), Revision, CatalogVersion, Active.Id, ChoiceId};
    if (!FRPGNarrativeCodec::Encode(Command, PendingBody)) return false;
    PendingChoice = MoveTemp(Command); return Dispatch(true);
}
bool URPGNarrativeCoordinator::RetryPendingChoice()
{
    if (IsBusy() || !ContextMatches()) return false;
    if (SkipClient && SkipClient->HasPending()) return SkipClient->Retry(SelectedCharacter);
    return HasPendingChoice() && Dispatch(true, PendingDungeon.IsSet());
}
bool URPGNarrativeCoordinator::Dispatch(const bool bWrite, const bool bDungeon)
{
    if (!Transport || !ContextMatches()) return false;
    const uint64 Generation = ++RequestGeneration;
    bBusy = true;
    const TWeakObjectPtr<URPGNarrativeCoordinator> WeakThis(this);
    // The previous HTTP callback has returned before a new operation begins.
    Transport->Cancel();
    TFunction<void(FRPGNarrativeHttpResult)> Completion =
        [WeakThis, Generation, bWrite](FRPGNarrativeHttpResult Result)
        {
            if (auto* Owner = WeakThis.Get()) Owner->Receive(Generation, bWrite, MoveTemp(Result));
        };
    const bool bSent = bDungeon ? Transport->SendDungeon(SelectedCharacter, PendingBody, MoveTemp(Completion))
        : Transport->Send(SelectedCharacter, bWrite ? PendingBody : FString(), MoveTemp(Completion));
    if (!bSent)
    {
        bBusy = false;
        OnRequestFinished.Broadcast(false, TEXT("narrative_not_dispatched"), bWrite);
    }
    return bSent;
}
void URPGNarrativeCoordinator::Receive(const uint64 Generation, const bool bWrite, FRPGNarrativeHttpResult Result)
{
    if (bShuttingDown || Generation != RequestGeneration || !bBusy) return;
    bBusy = false;
    FRPGNarrativeSnapshot Snapshot;
    const bool bContext = Result.bCurrentPlayerContext && ContextMatches();
    const bool bDecoded = bContext && Result.bTransportSuccessful && Result.StatusCode == 200
        && FRPGNarrativeCodec::Decode(Result.Body, SelectedCharacter, Snapshot);
    if (bDecoded)
    {
        const bool bWasDungeon = PendingDungeon.IsSet();
        if (bWrite) { PendingChoice.Reset(); PendingDungeon.Reset(); PendingBody.Reset(); if (HistoryClient) HistoryClient->Reset(); }
        Apply(MoveTemp(Snapshot)); OnRequestFinished.Broadcast(true, FString(), false);
        if (bWrite) ScheduleDungeonSync(true, !bWasDungeon);
        else if (bDungeonRefreshNeeded || !DungeonCandidates.IsEmpty()) ScheduleDungeonSync(false);
        return;
    }
    const bool bUncertain = bWrite && (!bContext || !Result.bTransportSuccessful
        || Result.StatusCode >= 500 || Result.StatusCode == 200 || Result.StatusCode == 401 || Result.StatusCode == 403);
    const bool bDungeonConflict = PendingDungeon.IsSet() && bContext && Result.bTransportSuccessful && Result.StatusCode == 409;
    if (bWrite && !bUncertain) { PendingChoice.Reset(); PendingDungeon.Reset(); PendingBody.Reset(); }
    FString Error = bContext ? TEXT("narrative_request_failed") : TEXT("narrative_context_changed");
    TSharedPtr<FJsonObject> Object; FString Code;
    if (bContext && Result.Body.Len() <= 8192
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Result.Body), Object) && Object
        && Object->TryGetStringField(TEXT("error"), Code) && Code.Len() <= 96) Error = MoveTemp(Code);
    OnRequestFinished.Broadcast(false, Error, bUncertain);
    if (bDungeonConflict) ScheduleDungeonSync(true);
}
void URPGNarrativeCoordinator::Apply(FRPGNarrativeSnapshot Snapshot)
{
    if (bHasView && Snapshot.Revision < Revision) return;
    if (!BindManagers() || !Quests->Accepts(Snapshot.CharacterId, Snapshot.Revision, Endpoint, SteamId)
        || !Scenarios->Accepts(Snapshot.CharacterId, Snapshot.Revision, Endpoint, SteamId)) return;
    // Validate both services before publishing either; notify UI only after all views agree.
    Quests->Apply(Snapshot, Endpoint, SteamId);
    Scenarios->Apply(Snapshot, Endpoint, SteamId);
    Dialogues->Dialogues = MoveTemp(Snapshot.AvailableDialogues);
    Dialogues->BoundEndpoint = Endpoint; Dialogues->BoundSteamId = SteamId;
    Dialogues->CharacterId = Snapshot.CharacterId; Dialogues->Revision = Snapshot.Revision;
    if (!Dialogues->Dialogues.ContainsByPredicate([&](const auto& D) { return D.Id == Dialogues->ActiveId; })) Dialogues->ActiveId.Reset();
    Revision = Snapshot.Revision; CatalogVersion = Snapshot.CatalogVersion; bHasView = true;
    DungeonCandidates = MoveTemp(Snapshot.DungeonObjectiveCandidates);
    OnChanged.Broadcast();
}
void URPGNarrativeCoordinator::Deinitialize()
{
    bShuttingDown = true; ++RequestGeneration;
    if (Transcript) Transcript->Reset(); Transcript.Reset();
    FTSTicker::GetCoreTicker().RemoveTicker(TrackingTick); TrackingTick.Reset();
    if (TrackingHUD) TrackingHUD->RemoveFromParent(); TrackingHUD = nullptr;
    if (QuestTracking) QuestTracking->Reset(); QuestTracking.Reset();
    FTSTicker::GetCoreTicker().RemoveTicker(DungeonSyncTick); DungeonSyncTick.Reset();
    if (DungeonFlow) DungeonFlow->OnDungeonSessionChanged.RemoveDynamic(this, &ThisClass::HandleDungeonSessionChanged);
    if (DungeonFlow) DungeonFlow->OnFlowStateChanged.RemoveDynamic(this, &ThisClass::HandleDungeonFlowChanged);
    if (Transport) Transport->Cancel();
    if (HistoryClient) HistoryClient->Reset();
    if (SkipClient) SkipClient->Reset();
    SkipClient.Reset();
    HistoryClient.Reset();
    Transport.Reset(); bBusy = false; PendingChoice.Reset(); PendingDungeon.Reset(); PendingBody.Reset(); ClearView();
    DungeonFlow = nullptr;
    Quests = nullptr; Scenarios = nullptr; Dialogues = nullptr;
    OnChanged.Clear(); OnRequestFinished.Clear(); Super::Deinitialize();
}

void URPGNarrativeCoordinator::HandleDungeonSessionChanged(const FRPGDungeonSession& Session)
{
    if (!DungeonFlow) return;
    FGuid Character;
    if (!FGuid::Parse(DungeonFlow->GetSelectedCharacterId(), Character) || !Character.IsValid()) return;
    // The game-instance flow belongs to the primary local player. Never rebind another local player's view.
    auto* Instance = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr;
    if (!Instance || Instance->GetFirstGamePlayer() != GetLocalPlayer()) return;
    const bool bRebind = !ContextMatches() || SelectedCharacter != Character;
    const bool bFinished = Session.State == TEXT("Cleared") || Session.State == TEXT("Failed");
    if (!bFinished && !bRebind && bHasView) return;
    if (bRebind && !SelectCharacter(Character)) return;
    if (bFinished)
    {
        FGuid SessionId;
        if (!FGuid::Parse(Session.DungeonSessionId, SessionId) || !SessionId.IsValid()
            || (!bRebind && LastObservedDungeonResult == SessionId)) return;
        LastObservedDungeonResult = SessionId;
    }
    ScheduleDungeonSync(true, true);
}
void URPGNarrativeCoordinator::HandleDungeonFlowChanged(const ERPGDungeonClientFlowState State, const FString&)
{
    if (DungeonFlow && (State == ERPGDungeonClientFlowState::Idle || State == ERPGDungeonClientFlowState::InSession
        || State == ERPGDungeonClientFlowState::ResumingSession))
        HandleDungeonSessionChanged(DungeonFlow->GetCurrentDungeonSession());
}
void URPGNarrativeCoordinator::ScheduleDungeonSync(const bool bRefresh, const bool bResetBudget)
{
    if (bShuttingDown) return;
    bDungeonRefreshNeeded |= bRefresh;
    if (bResetBudget) DungeonSyncBudget = 256;
    if (!DungeonSyncTick.IsValid()) DungeonSyncTick = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateWeakLambda(this, [this](float Delta) { return TickDungeonSync(Delta); }));
}
bool URPGNarrativeCoordinator::TickDungeonSync(float)
{
    DungeonSyncTick.Reset();
    if (IsBusy() || HasPendingChoice() || !ContextMatches() || DungeonSyncBudget <= 0) return false;
    if (bDungeonRefreshNeeded)
    {
        bDungeonRefreshNeeded = false; Dispatch(false); return false;
    }
    if (!HasCurrentSnapshot() || DungeonCandidates.IsEmpty()) return false;
    FRPGNarrativeDungeonCommand Command{FGuid::NewGuid(), Revision, CatalogVersion, DungeonCandidates[0]};
    if (!FRPGNarrativeCodec::EncodeDungeon(Command, PendingBody)) return false;
    --DungeonSyncBudget; PendingDungeon = MoveTemp(Command); Dispatch(true, true); return false;
}
