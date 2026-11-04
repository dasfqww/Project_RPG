using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace ProjectRpg.Backend.Narrative;

public enum QuestStatus { Active, ReadyToReport, Completed }
public enum DialogueEffect { AcceptQuest, RecordObjective, ReportQuest, None }

public sealed record QuestDefinition(string Id, int Version, ImmutableArray<string> Prerequisites,
    ImmutableArray<string> Objectives, string? Title = null, ImmutableDictionary<string, string>? ObjectiveTexts = null);
public sealed record ChapterDefinition(string Id, ImmutableArray<string> RequiredQuests,
    ImmutableArray<string> StoryUnlocks);
public sealed record DialogueChoice(string Id, string Text, DialogueEffect Effect, string? ObjectiveId = null,
    string? NextDialogueId = null);
public sealed record DialogueDefinition(string Id, string Speaker, string Text, string? QuestId,
    QuestStatus? RequiredStatus, ImmutableArray<DialogueChoice> Choices, bool Informational = false, bool EntryPoint = true,
    ImmutableArray<string> RequiredCompletedQuests = default)
{
    internal ImmutableArray<string> CompletionRequirements => RequiredCompletedQuests.IsDefault ? [] : RequiredCompletedQuests;
}
public sealed record QuestProgress(string QuestId, int DefinitionVersion, QuestStatus Status,
    ImmutableArray<string> CompletedObjectives);
public sealed record NarrativeState(Guid CharacterId, int SchemaVersion, long Revision,
    ImmutableArray<QuestProgress> Quests, ImmutableArray<string> CompletedChapters,
    ImmutableArray<string> StoryUnlocks)
{
    public static NarrativeState Create(Guid characterId) => new(characterId, 1, 0, [], [], []);
}

// Only authored dialogue actions are accepted. Clients cannot submit objective counts,
// completion flags, unlock IDs, XP or item grants.
public sealed record NarrativeCommand(Guid RequestId, long ExpectedRevision, int CatalogVersion,
    string DialogueId, string ChoiceId)
{
    public void Validate()
    {
        if (RequestId == Guid.Empty || ExpectedRevision < 0 || CatalogVersion < 1
            || !ValidId(DialogueId) || !ValidId(ChoiceId))
            throw new NarrativeConflictException("invalid_narrative_command");
    }

    public string Fingerprint()
    {
        Validate();
        return Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(
            JsonSerializer.Serialize(new { ExpectedRevision, CatalogVersion, DialogueId, ChoiceId }))));
    }

    internal static bool ValidId(string? value) => !string.IsNullOrWhiteSpace(value)
        && value.Length <= 128 && value.All(c => char.IsAsciiLetterOrDigit(c) || c is '.' or '_' or '-');
}

public sealed class NarrativeConflictException(string code) : Exception(code);
public sealed record DialogueView(string Id, string Speaker, string Text,
    ImmutableArray<DialogueChoiceView> Choices, bool Informational = false, bool EntryPoint = true);
public sealed record DialogueChoiceView(string Id, string Text, string? NextDialogueId = null);
public sealed record QuestObjectiveView(string Id, string? Text, bool Completed);
public sealed record QuestJournalEntry(string QuestId, int DefinitionVersion, string Status,
    ImmutableArray<string> CompletedObjectives, string? Title, ImmutableArray<QuestObjectiveView> Objectives)
{
    public static QuestJournalEntry From(QuestProgress progress, QuestDefinition definition) => new(
        progress.QuestId, progress.DefinitionVersion, progress.Status.ToString(), progress.CompletedObjectives,
        definition.Title, [.. definition.Objectives.Select(id => new QuestObjectiveView(id,
            definition.ObjectiveTexts?.GetValueOrDefault(id), progress.CompletedObjectives.Contains(id)))]);
}
public sealed record NarrativeView(int CatalogVersion, NarrativeState State,
    ImmutableArray<DialogueView> AvailableDialogues, ImmutableArray<QuestJournalEntry> Journal);
