using System.Collections.Immutable;

namespace ProjectRpg.Backend.Narrative;

/// <summary>Authored choice eligibility and presentation; no independent quest state.</summary>
public sealed class DialogueManager(QuestManager quests)
{
    private bool Available(NarrativeState state, NarrativeCatalog catalog, DialogueDefinition definition)
    {
        QuestProgress? progress = state.Quests.SingleOrDefault(q => q.QuestId == definition.QuestId);
        return definition.RequiredStatus is { } status ? progress?.Status == status
            : quests.CanAccept(state, catalog.Quests[definition.QuestId]);
    }

    private static bool ChoiceAvailable(NarrativeState state, DialogueDefinition definition, DialogueChoice choice)
        => choice.Effect != DialogueEffect.RecordObjective
            || !state.Quests.Single(q => q.QuestId == definition.QuestId).CompletedObjectives.Contains(choice.ObjectiveId!);

    public (DialogueDefinition Dialogue, DialogueChoice Choice) Resolve(NarrativeState state,
        NarrativeCatalog catalog, NarrativeCommand command)
    {
        if (!catalog.Dialogues.TryGetValue(command.DialogueId, out DialogueDefinition? definition)
            || !Available(state, catalog, definition)) throw new NarrativeConflictException("dialogue_not_available");
        DialogueChoice? choice = definition.Choices.SingleOrDefault(c => c.Id == command.ChoiceId);
        if (choice is null || !ChoiceAvailable(state, definition, choice))
            throw new NarrativeConflictException("dialogue_choice_not_available");
        return (definition, choice);
    }

    public ImmutableArray<DialogueView> GetAvailable(NarrativeState state, NarrativeCatalog catalog) =>
        [.. catalog.Dialogues.Values.OrderBy(d => d.Id, StringComparer.Ordinal)
            .Where(d => Available(state, catalog, d))
            .Select(d => new DialogueView(d.Id, d.Speaker, d.Text,
                [.. d.Choices.Where(c => ChoiceAvailable(state, d, c)).Select(c => new DialogueChoiceView(c.Id, c.Text))]))
            .Where(d => d.Choices.Length != 0)];
}
