#include "Narrative/RPGQuestManager.h"
#include "Narrative/RPGScenarioManager.h"
#include "Narrative/RPGDialogueManager.h"
#include "Manager/HttpWebManager.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"

namespace
{
    bool Current(const UGameInstance* Instance, const FString& Endpoint, const FString& SteamId)
    {
        if (!IsInGameThread() || !IsValid(Instance) || Endpoint.IsEmpty() || SteamId.IsEmpty()) return false;
        const auto* Web = Instance->GetSubsystem<UHttpWebManager>();
        FString CurrentEndpoint, CurrentSteam;
        return IsValid(Web) && Web->GetPlayerEquipmentContext(CurrentEndpoint, CurrentSteam)
            && CurrentEndpoint == Endpoint && CurrentSteam == SteamId;
    }
}
bool URPGQuestManager::ShouldCreateSubsystem(UObject* Outer) const
{
    return Super::ShouldCreateSubsystem(Outer) && !IsRunningDedicatedServer();
}
void URPGQuestManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection); Collection.InitializeDependency<UHttpWebManager>();
}
void URPGQuestManager::Deinitialize()
{
    Views.Reset(); BoundEndpoint.Reset(); BoundSteamId.Reset(); Super::Deinitialize();
}
TArray<FRPGQuestProgress> URPGQuestManager::GetQuests(const FGuid CharacterId) const
{
    const auto* View = Current(GetGameInstance(), BoundEndpoint, BoundSteamId) ? Views.Find(CharacterId) : nullptr;
    return View ? View->Progress : TArray<FRPGQuestProgress>();
}
bool URPGQuestManager::IsQuestCompleted(const FGuid CharacterId, const FString& QuestId) const
{
    const auto* View = Current(GetGameInstance(), BoundEndpoint, BoundSteamId) ? Views.Find(CharacterId) : nullptr;
    return View && View->Progress.ContainsByPredicate([&](const auto& Q)
        { return Q.QuestId == QuestId && Q.Status == ERPGQuestStatus::Completed; });
}
bool URPGQuestManager::HasRevision(const FGuid CharacterId, const int64 Revision) const
{
    const auto* View = Current(GetGameInstance(), BoundEndpoint, BoundSteamId) ? Views.Find(CharacterId) : nullptr;
    return View && View->Revision == Revision;
}
bool URPGQuestManager::Accepts(const FGuid CharacterId, const int64 Revision,
    const FString& Endpoint, const FString& SteamId) const
{
    if (!CharacterId.IsValid() || Revision < 0 || !Current(GetGameInstance(), Endpoint, SteamId)) return false;
    const auto* View = BoundEndpoint == Endpoint && BoundSteamId == SteamId ? Views.Find(CharacterId) : nullptr;
    return !View || Revision >= View->Revision;
}
void URPGQuestManager::Apply(const FRPGNarrativeSnapshot& Snapshot, const FString& Endpoint, const FString& SteamId)
{
    if (BoundEndpoint != Endpoint || BoundSteamId != SteamId) Views.Reset();
    BoundEndpoint = Endpoint; BoundSteamId = SteamId;
    Views.Add(Snapshot.CharacterId, {Snapshot.Revision, Snapshot.Quests});
}
bool URPGScenarioManager::ShouldCreateSubsystem(UObject* Outer) const
{
    return Super::ShouldCreateSubsystem(Outer) && !IsRunningDedicatedServer();
}
void URPGScenarioManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection); Collection.InitializeDependency<UHttpWebManager>();
}
void URPGScenarioManager::Deinitialize()
{
    Views.Reset(); BoundEndpoint.Reset(); BoundSteamId.Reset(); Super::Deinitialize();
}
bool URPGScenarioManager::IsChapterCompleted(const FGuid CharacterId, const FString& ChapterId) const
{
    const auto* View = Current(GetGameInstance(), BoundEndpoint, BoundSteamId) ? Views.Find(CharacterId) : nullptr;
    return View && View->CompletedChapters.Contains(ChapterId);
}
bool URPGScenarioManager::IsStoryUnlocked(const FGuid CharacterId, const FString& UnlockId) const
{
    const auto* View = Current(GetGameInstance(), BoundEndpoint, BoundSteamId) ? Views.Find(CharacterId) : nullptr;
    return View && View->StoryUnlocks.Contains(UnlockId);
}
bool URPGScenarioManager::HasRevision(const FGuid CharacterId, const int64 Revision) const
{
    const auto* View = Current(GetGameInstance(), BoundEndpoint, BoundSteamId) ? Views.Find(CharacterId) : nullptr;
    return View && View->Revision == Revision;
}
bool URPGScenarioManager::Accepts(const FGuid CharacterId, const int64 Revision,
    const FString& Endpoint, const FString& SteamId) const
{
    if (!CharacterId.IsValid() || Revision < 0 || !Current(GetGameInstance(), Endpoint, SteamId)) return false;
    const auto* View = BoundEndpoint == Endpoint && BoundSteamId == SteamId ? Views.Find(CharacterId) : nullptr;
    return !View || Revision >= View->Revision;
}
void URPGScenarioManager::Apply(const FRPGNarrativeSnapshot& Snapshot, const FString& Endpoint, const FString& SteamId)
{
    if (BoundEndpoint != Endpoint || BoundSteamId != SteamId) Views.Reset();
    BoundEndpoint = Endpoint; BoundSteamId = SteamId;
    Views.Add(Snapshot.CharacterId, {Snapshot.Revision, Snapshot.CompletedChapters, Snapshot.StoryUnlocks});
}
bool URPGDialogueManager::Readable() const
{
    const auto* Player = GetLocalPlayer(); const auto* Instance = Player ? Player->GetGameInstance() : nullptr;
    if (Revision < 0 || !Current(Instance, BoundEndpoint, BoundSteamId)) return false;
    const auto* Quests = Instance->GetSubsystem<URPGQuestManager>();
    const auto* Scenarios = Instance->GetSubsystem<URPGScenarioManager>();
    return Quests && Scenarios && Quests->HasRevision(CharacterId, Revision) && Scenarios->HasRevision(CharacterId, Revision);
}
void URPGDialogueManager::Reset()
{
    Dialogues.Reset(); ActiveId.Reset(); BoundEndpoint.Reset(); BoundSteamId.Reset(); CharacterId.Invalidate(); Revision = -1;
}
void URPGDialogueManager::Deinitialize() { Reset(); Super::Deinitialize(); }
TArray<FRPGDialogueNode> URPGDialogueManager::GetAvailableDialogues() const
{
    return Readable() ? Dialogues : TArray<FRPGDialogueNode>();
}
bool URPGDialogueManager::GetActiveDialogue(FRPGDialogueNode& OutDialogue) const
{
    if (!Readable()) return false;
    const auto* Found = Dialogues.FindByPredicate([&](const auto& D) { return D.Id == ActiveId; });
    if (!Found) return false;
    OutDialogue = *Found; return true;
}
