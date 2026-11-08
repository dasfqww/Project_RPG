using System.Collections.Frozen;

namespace ProjectRpg.Backend.Narrative;

/// <summary>Immutable authored definitions; no per-character mutable state.</summary>
public sealed class NarrativeCatalog
{
    public int Version { get; }
    public FrozenDictionary<string, QuestDefinition> Quests { get; }
    public FrozenDictionary<string, ChapterDefinition> Chapters { get; }
    public FrozenDictionary<string, DialogueDefinition> Dialogues { get; }

    public NarrativeCatalog(int version, IEnumerable<QuestDefinition> quests,
        IEnumerable<ChapterDefinition> chapters, IEnumerable<DialogueDefinition> dialogues)
    {
        if (version < 1) throw new ArgumentOutOfRangeException(nameof(version));
        Version = version;
        Quests = quests.ToFrozenDictionary(q => q.Id, StringComparer.Ordinal);
        Chapters = chapters.ToFrozenDictionary(c => c.Id, StringComparer.Ordinal);
        Dialogues = dialogues.ToFrozenDictionary(d => d.Id, StringComparer.Ordinal);
        Require(Quests.Count <= 256 && Chapters.Count <= 256 && Dialogues.Count <= 256,
            "narrative_catalog_limit_exceeded");
        foreach (QuestDefinition q in Quests.Values)
        {
            Require(NarrativeCommand.ValidId(q.Id) && q.Version > 0 && !q.Objectives.IsDefaultOrEmpty
                && !q.Prerequisites.IsDefault && q.Objectives.Length == q.Objectives.Distinct().Count()
                && q.Objectives.All(NarrativeCommand.ValidId)
                && q.Prerequisites.Length == q.Prerequisites.Distinct().Count()
                && q.Objectives.Length <= 256 && q.Prerequisites.Length <= 256
                && q.Prerequisites.All(Quests.ContainsKey), "invalid_quest_definition");
            Require((q.Title is null || !string.IsNullOrWhiteSpace(q.Title) && q.Title.Length <= 256)
                && (q.ObjectiveTexts is null || q.ObjectiveTexts.All(pair => q.Objectives.Contains(pair.Key)
                    && !string.IsNullOrWhiteSpace(pair.Value) && pair.Value.Length <= 1024)),
                "invalid_quest_presentation");
            Require(q.ObjectivePrerequisites is null || q.ObjectivePrerequisites.Count <= q.Objectives.Length
                && q.ObjectivePrerequisites.All(pair => q.Objectives.Contains(pair.Key)
                    && !pair.Value.IsDefault && pair.Value.Length <= 256
                    && pair.Value.Length == pair.Value.Distinct().Count()
                    && pair.Value.All(id => q.Objectives.Contains(id) && id != pair.Key)),
                "invalid_objective_prerequisites");
            Require(q.DungeonObjectives is null || q.DungeonObjectives.Count <= q.Objectives.Length
                && q.DungeonObjectives.All(pair => q.Objectives.Contains(pair.Key) && pair.Value is not null
                    && NarrativeCommand.ValidId(pair.Value.DungeonId) && pair.Value.DungeonId.Length <= 64
                    && pair.Value.Difficulty is "Easy" or "Normal" or "Hard" or "Hell"),
                "invalid_dungeon_objective");
            Require(q.InteractionObjectives is null || q.InteractionObjectives.Count <= q.Objectives.Length
                && q.InteractionObjectives.All(pair => q.Objectives.Contains(pair.Key)
                    && q.DungeonObjectives?.ContainsKey(pair.Key) != true && pair.Value is not null
                    && NarrativeCommand.ValidId(pair.Value.DungeonId) && pair.Value.DungeonId.Length <= 64
                    && pair.Value.Difficulty is "Easy" or "Normal" or "Hard" or "Hell"
                    && NarrativeCommand.ValidId(pair.Value.TargetId)), "invalid_interaction_objective");
            Require(q.KillObjectives is null || q.KillObjectives.Count <= q.Objectives.Length
                && q.KillObjectives.All(pair => q.Objectives.Contains(pair.Key)
                    && q.DungeonObjectives?.ContainsKey(pair.Key) != true && q.InteractionObjectives?.ContainsKey(pair.Key) != true
                    && pair.Value is not null && NarrativeCommand.ValidId(pair.Value.DungeonId) && pair.Value.DungeonId.Length <= 64
                    && pair.Value.Difficulty is "Easy" or "Normal" or "Hard" or "Hell"
                    && NarrativeCommand.ValidId(pair.Value.EnemyId) && pair.Value.RequiredCount is >= 1 and <= 10000), "invalid_kill_objective");
            HashSet<string> objectiveVisiting = new(StringComparer.Ordinal), objectiveVisited = new(StringComparer.Ordinal);
            void VisitObjective(string id)
            {
                if (objectiveVisited.Contains(id)) return;
                Require(objectiveVisiting.Add(id), "cyclic_objective_prerequisites");
                foreach (string parent in q.RequiredObjectives(id)) VisitObjective(parent);
                objectiveVisiting.Remove(id); objectiveVisited.Add(id);
            }
            foreach (string id in q.Objectives) VisitObjective(id);
        }
        Require(Quests.Values.Sum(q => q.InteractionObjectives?.Count ?? 0) <= 256, "narrative_catalog_limit_exceeded");
        Require(Quests.Values.Sum(q => q.KillObjectives?.Count ?? 0) <= 256, "narrative_catalog_limit_exceeded");
        HashSet<string> visiting = new(StringComparer.Ordinal), visited = new(StringComparer.Ordinal);
        void Visit(string id)
        {
            if (visited.Contains(id)) return;
            Require(visiting.Add(id), "cyclic_quest_prerequisites");
            foreach (string parent in Quests[id].Prerequisites) Visit(parent);
            visiting.Remove(id); visited.Add(id);
        }
        foreach (string id in Quests.Keys) Visit(id);
        foreach (ChapterDefinition c in Chapters.Values)
            Require(NarrativeCommand.ValidId(c.Id) && !c.RequiredQuests.IsDefaultOrEmpty
                && c.RequiredQuests.All(Quests.ContainsKey) && !c.StoryUnlocks.IsDefault
                && c.StoryUnlocks.All(NarrativeCommand.ValidId)
                && c.RequiredQuests.Length == c.RequiredQuests.Distinct().Count()
                && c.StoryUnlocks.Length == c.StoryUnlocks.Distinct().Count(), "invalid_chapter_definition");
        Require(Chapters.Values.SelectMany(c => c.StoryUnlocks).Distinct().Count() <= 256,
            "narrative_catalog_limit_exceeded");
        foreach (DialogueDefinition d in Dialogues.Values)
        {
            var requirements = d.CompletionRequirements;
            Require(requirements.Length <= 256 && requirements.Length == requirements.Distinct().Count()
                && requirements.All(NarrativeCommand.ValidId) && requirements.All(Quests.ContainsKey)
                && (d.Informational || requirements.IsEmpty), "invalid_dialogue_requirements");
            Require(NarrativeCommand.ValidId(d.Id)
                && (d.Informational ? d.QuestId is null && d.RequiredStatus is null
                    : d.EntryPoint && d.QuestId is not null && Quests.ContainsKey(d.QuestId))
                && !string.IsNullOrWhiteSpace(d.Speaker) && d.Speaker.Length <= 256
                && !string.IsNullOrWhiteSpace(d.Text) && d.Text.Length <= 4096
                && !d.Choices.IsDefaultOrEmpty && d.Choices.Length <= 64 && d.Choices.All(c => c is not null)
                && (d.RequiredStatus is null || Enum.IsDefined(d.RequiredStatus.Value))
                && d.Choices.Length == d.Choices.Select(c => c.Id).Distinct().Count(), "invalid_dialogue_definition");
            foreach (DialogueChoice c in d.Choices)
            {
                Require(c is not null && NarrativeCommand.ValidId(c.Id) && !string.IsNullOrWhiteSpace(c.Text) && c.Text.Length <= 1024
                    && Enum.IsDefined(c.Effect), "invalid_dialogue_choice");
                if (d.Informational)
                    Require(c.Effect == DialogueEffect.None && c.ObjectiveId is null
                        && (c.NextDialogueId is null || NarrativeCommand.ValidId(c.NextDialogueId)
                            && Dialogues.TryGetValue(c.NextDialogueId, out var target) && target.Informational),
                        "invalid_information_choice");
                else Require(c.Effect != DialogueEffect.None && c.NextDialogueId is null
                    && (c.Effect == DialogueEffect.RecordObjective
                        ? d.RequiredStatus == QuestStatus.Active && c.ObjectiveId is not null
                            && Quests[d.QuestId!].Objectives.Contains(c.ObjectiveId)
                            && Quests[d.QuestId!].DungeonObjectives?.ContainsKey(c.ObjectiveId) != true
                            && Quests[d.QuestId!].InteractionObjectives?.ContainsKey(c.ObjectiveId) != true
                            && Quests[d.QuestId!].KillObjectives?.ContainsKey(c.ObjectiveId) != true
                        : c.ObjectiveId is null && (c.Effect == DialogueEffect.AcceptQuest
                            ? d.RequiredStatus is null : d.RequiredStatus == QuestStatus.ReadyToReport)),
                    "invalid_dialogue_choice");
                if (d.Informational && c.NextDialogueId is { } next)
                    Require(requirements.ToHashSet(StringComparer.Ordinal).SetEquals(Dialogues[next].CompletionRequirements),
                        "inconsistent_information_requirements");
            }
        }
        HashSet<string> reachable = new(StringComparer.Ordinal);
        Stack<string> pending = new(Dialogues.Values.Where(d => d.Informational && d.EntryPoint).Select(d => d.Id));
        while (pending.TryPop(out string? id))
        {
            if (!reachable.Add(id)) continue;
            foreach (var choice in Dialogues[id].Choices)
                if (choice.NextDialogueId is { } next) pending.Push(next);
        }
        Require(Dialogues.Values.Where(d => d.Informational).All(d => reachable.Contains(d.Id)),
            "unreachable_information_dialogue");
        foreach (QuestDefinition q in Quests.Values)
        {
            DialogueChoice[] actions = [.. Dialogues.Values.Where(d => d.QuestId == q.Id).SelectMany(d => d.Choices)];
            Require(actions.Any(c => c.Effect == DialogueEffect.AcceptQuest)
                && actions.Any(c => c.Effect == DialogueEffect.ReportQuest)
                && q.Objectives.All(id => q.DungeonObjectives?.ContainsKey(id) == true
                    || q.InteractionObjectives?.ContainsKey(id) == true
                    || q.KillObjectives?.ContainsKey(id) == true
                    || actions.Any(c => c.Effect == DialogueEffect.RecordObjective && c.ObjectiveId == id)),
                "unreachable_quest_definition");
        }
    }

    private static void Require([System.Diagnostics.CodeAnalysis.DoesNotReturnIf(false)] bool condition, string code)
    {
        if (!condition) throw new NarrativeDefinitionException(code);
    }

    public static NarrativeCatalog Empty { get; } = new(1, [], [], []);

    // Technical first-slice content, opt-in on Development/Testing hosts only.
    // These IDs are deliberately independent of the unapproved CH01 story proposal.
    private static readonly Lazy<NarrativeCatalog> DevelopmentCatalog = new(NarrativeCatalogLoader.LoadDevelopment);
    public static NarrativeCatalog Development => DevelopmentCatalog.Value;
}

internal sealed class NarrativeDefinitionException(string code) : ArgumentException(code)
{
    public string Code { get; } = code;
}
