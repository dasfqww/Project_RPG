using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace ProjectRpg.Backend.Narrative;

public enum QuestStatus { Active, ReadyToReport, Completed }
public enum DialogueEffect { AcceptQuest, RecordObjective, ReportQuest }

public sealed record QuestDefinition(string Id, int Version, ImmutableArray<string> Prerequisites,
    ImmutableArray<string> Objectives);
public sealed record ChapterDefinition(string Id, ImmutableArray<string> RequiredQuests,
    ImmutableArray<string> StoryUnlocks);
public sealed record DialogueChoice(string Id, string Text, DialogueEffect Effect, string? ObjectiveId = null);
public sealed record DialogueDefinition(string Id, string Speaker, string Text, string QuestId,
    QuestStatus? RequiredStatus, ImmutableArray<DialogueChoice> Choices);
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
    ImmutableArray<DialogueChoiceView> Choices);
public sealed record DialogueChoiceView(string Id, string Text);
public sealed record NarrativeView(int CatalogVersion, NarrativeState State,
    ImmutableArray<DialogueView> AvailableDialogues);
