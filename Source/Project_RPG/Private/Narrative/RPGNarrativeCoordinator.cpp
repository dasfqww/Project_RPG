#include "Narrative/RPGNarrativeCoordinator.h"
#include "Narrative/RPGQuestManager.h"
#include "Narrative/RPGScenarioManager.h"
#include "Narrative/RPGDialogueManager.h"
#include "Narrative/RPGNarrativeTransport.h"
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
}
bool URPGNarrativeCoordinator::BindManagers()
{
    auto* Player = GetLocalPlayer(); auto* Instance = Player ? Player->GetGameInstance() : nullptr;
    Quests = Instance ? Instance->GetSubsystem<URPGQuestManager>() : nullptr;
    Scenarios = Instance ? Instance->GetSubsystem<URPGScenarioManager>() : nullptr;
    Dialogues = Player ? Player->GetSubsystem<URPGDialogueManager>() : nullptr;
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
bool URPGNarrativeCoordinator::HasCurrentSnapshot() const
{
    return bHasView && ContextMatches() && Quests && Scenarios && Dialogues
        && Quests->HasRevision(SelectedCharacter, Revision) && Scenarios->HasRevision(SelectedCharacter, Revision)
        && Dialogues->Readable();
}
void URPGNarrativeCoordinator::ClearView()
{
    bHasView = false; Revision = 0; CatalogVersion = 0;
    if (Dialogues) Dialogues->Reset();
}
bool URPGNarrativeCoordinator::SelectCharacter(const FGuid CharacterId)
{
    if (!IsInGameThread() || bShuttingDown || bBusy || !CharacterId.IsValid() || !EnsureTransport()) return false;
    FString CurrentEndpoint, CurrentSteam;
    if (!Transport->GetContext(CurrentEndpoint, CurrentSteam)) return false;
    // Resolve an uncertain command before switching within the same account/endpoint.
    // Another account cannot inherit this account's display or pending request.
    if (PendingChoice && CurrentEndpoint == Endpoint && CurrentSteam == SteamId) return false;
    if (!BindManagers()) return false;
    ++RequestGeneration; Transport->Cancel(); PendingChoice.Reset(); PendingBody.Reset();
    SelectedCharacter = CharacterId; Endpoint = MoveTemp(CurrentEndpoint); SteamId = MoveTemp(CurrentSteam);
    ClearView(); OnChanged.Broadcast(); return true;
}
bool URPGNarrativeCoordinator::Refresh()
{
    return !bBusy && !PendingChoice && ContextMatches() && Dispatch(false);
}
bool URPGNarrativeCoordinator::BeginDialogue(const FString& DialogueId)
{
    if (!HasCurrentSnapshot() || bBusy || PendingChoice
        || !Dialogues->Dialogues.ContainsByPredicate([&](const auto& D) { return D.Id == DialogueId; })) return false;
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
    if (!HasCurrentSnapshot() || bBusy || PendingChoice || !Dialogues->GetActiveDialogue(Active)
        || !Active.Choices.ContainsByPredicate([&](const auto& C) { return C.Id == ChoiceId; })) return false;
    FRPGNarrativeCommand Command{FGuid::NewGuid(), Revision, CatalogVersion, Active.Id, ChoiceId};
    if (!FRPGNarrativeCodec::Encode(Command, PendingBody)) return false;
    PendingChoice = MoveTemp(Command); return Dispatch(true);
}
bool URPGNarrativeCoordinator::RetryPendingChoice()
{
    return !bBusy && PendingChoice && ContextMatches() && Dispatch(true);
}
bool URPGNarrativeCoordinator::Dispatch(const bool bWrite)
{
    if (!Transport || !ContextMatches()) return false;
    const uint64 Generation = ++RequestGeneration;
    bBusy = true;
    const TWeakObjectPtr<URPGNarrativeCoordinator> WeakThis(this);
    // The previous HTTP callback has returned before a new operation begins.
    Transport->Cancel();
    const bool bSent = Transport->Send(SelectedCharacter, bWrite ? PendingBody : FString(),
        [WeakThis, Generation, bWrite](FRPGNarrativeHttpResult Result)
        {
            if (auto* Owner = WeakThis.Get()) Owner->Receive(Generation, bWrite, MoveTemp(Result));
        });
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
        if (bWrite) { PendingChoice.Reset(); PendingBody.Reset(); }
        Apply(MoveTemp(Snapshot)); OnRequestFinished.Broadcast(true, FString(), false); return;
    }
    const bool bUncertain = bWrite && (!bContext || !Result.bTransportSuccessful
        || Result.StatusCode >= 500 || Result.StatusCode == 200 || Result.StatusCode == 401 || Result.StatusCode == 403);
    if (bWrite && !bUncertain) { PendingChoice.Reset(); PendingBody.Reset(); }
    FString Error = bContext ? TEXT("narrative_request_failed") : TEXT("narrative_context_changed");
    TSharedPtr<FJsonObject> Object; FString Code;
    if (bContext && Result.Body.Len() <= 8192
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Result.Body), Object) && Object
        && Object->TryGetStringField(TEXT("error"), Code) && Code.Len() <= 96) Error = MoveTemp(Code);
    OnRequestFinished.Broadcast(false, Error, bUncertain);
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
    OnChanged.Broadcast();
}
void URPGNarrativeCoordinator::Deinitialize()
{
    bShuttingDown = true; ++RequestGeneration;
    if (Transport) Transport->Cancel();
    Transport.Reset(); bBusy = false; PendingChoice.Reset(); PendingBody.Reset(); ClearView();
    Quests = nullptr; Scenarios = nullptr; Dialogues = nullptr;
    OnChanged.Clear(); OnRequestFinished.Clear(); Super::Deinitialize();
}
