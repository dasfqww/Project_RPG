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
        foreach (QuestDefinition q in Quests.Values)
        {
            Require(NarrativeCommand.ValidId(q.Id) && q.Version > 0 && !q.Objectives.IsDefaultOrEmpty
                && !q.Prerequisites.IsDefault && q.Objectives.Length == q.Objectives.Distinct().Count()
                && q.Objectives.All(NarrativeCommand.ValidId)
                && q.Prerequisites.Length == q.Prerequisites.Distinct().Count()
                && q.Prerequisites.All(Quests.ContainsKey), "invalid_quest_definition");
        }
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
        foreach (DialogueDefinition d in Dialogues.Values)
        {
            Require(NarrativeCommand.ValidId(d.Id) && Quests.ContainsKey(d.QuestId)
                && !string.IsNullOrWhiteSpace(d.Speaker) && !string.IsNullOrWhiteSpace(d.Text)
                && !d.Choices.IsDefaultOrEmpty
                && (d.RequiredStatus is null || Enum.IsDefined(d.RequiredStatus.Value))
                && d.Choices.Length == d.Choices.Select(c => c.Id).Distinct().Count(), "invalid_dialogue_definition");
            foreach (DialogueChoice c in d.Choices)
                Require(NarrativeCommand.ValidId(c.Id) && !string.IsNullOrWhiteSpace(c.Text)
                    && Enum.IsDefined(c.Effect)
                    && (c.Effect == DialogueEffect.RecordObjective
                        ? d.RequiredStatus == QuestStatus.Active && c.ObjectiveId is not null
                            && Quests[d.QuestId].Objectives.Contains(c.ObjectiveId)
                        : c.ObjectiveId is null && (c.Effect == DialogueEffect.AcceptQuest
                            ? d.RequiredStatus is null : d.RequiredStatus == QuestStatus.ReadyToReport)),
                    "invalid_dialogue_choice");
        }
    }

    private static void Require(bool condition, string code)
    {
        if (!condition) throw new ArgumentException(code);
    }

    public static NarrativeCatalog Empty { get; } = new(1, [], [], []);

    // Technical first-slice content, opt-in on Development/Testing hosts only.
    // These IDs are deliberately independent of the unapproved CH01 story proposal.
    public static NarrativeCatalog Development { get; } = new(1,
        [new("DEV.Introduction", 1, [], ["contact", "preparation"])],
        [new("DEV.Prologue", ["DEV.Introduction"], ["DEV.Story.Next"])],
        [new("DEV.Contact.Offer", "Contact", "Speak with the contact and instructor.",
            "DEV.Introduction", null, [new("accept", "Accept the introduction", DialogueEffect.AcceptQuest)]),
         new("DEV.Contact.Explain", "Contact", "Find out why preparation is needed.",
            "DEV.Introduction", QuestStatus.Active,
            [new("confirm", "Confirm the situation", DialogueEffect.RecordObjective, "contact")]),
         new("DEV.Instructor.Explain", "Instructor", "Learn what to prepare before departure.",
            "DEV.Introduction", QuestStatus.Active,
            [new("confirm", "Confirm the preparation", DialogueEffect.RecordObjective, "preparation")]),
         new("DEV.Contact.Report", "Contact", "Report that the preparations are understood.",
            "DEV.Introduction", QuestStatus.ReadyToReport,
            [new("report", "Report completion", DialogueEffect.ReportQuest)])]);
}
