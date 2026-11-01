using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

internal static class ProgressionCases
{
    public static async Task RunAsync()
    {
        InMemoryGameRepository game = new();
        const string steam = "76561198000000991", server = "progression-server", worker = "progression-worker";
        DateTimeOffset now = DateTimeOffset.UtcNow;
        await game.UpsertAccountAsync(new(steam, steam, false, false), now, default);
        GameCharacter character = await game.CreateCharacterAsync(steam, "ProgressionHero", now, default);
        Guid id = character.CharacterId;
        async Task<CharacterProgressionState> Read() =>
            await game.LoadProgressionAsync(id, now, default) ?? throw new Exception("Missing new-character progression.");

        async Task<DungeonSession> Start(string dungeon, string difficulty = "Normal")
        {
            now = now.AddSeconds(10);
            DungeonSession session = await game.CreateDungeonSessionAsync(steam, id, dungeon, difficulty, now, now.AddHours(1), default);
            await game.AssignDungeonServerAsync(session.DungeonSessionId, server, "127.0.0.1:7777", now, now.AddHours(1), default);
            return await game.StartDungeonSessionAsync(session.DungeonSessionId, server, now, now.AddHours(1), default);
        }

        async Task Queue(DungeonSession session, string version = FirstSliceProgression.RewardVersion)
        {
            await game.EnqueueDungeonRewardSettlementAsync(session.DungeonSessionId, server, version,
                new string('a', 64), [], [], now, default);
            Require((await game.ClaimNextDungeonRewardSettlementAsync(worker, now, now.AddMinutes(5), default))
                ?.DungeonSessionId == session.DungeonSessionId, "Worker must claim the expected settlement.");
        }

        async Task Clear(string dungeon, string difficulty = "Normal", string version = FirstSliceProgression.RewardVersion)
        {
            DungeonSession session = await Start(dungeon, difficulty);
            await Queue(session, version);
            await game.CompleteDungeonRewardSettlementAsync(session.DungeonSessionId, worker, now, default);
        }

        CharacterProgressionState state = await Read();
        GameCharacter secondCharacter = await game.CreateCharacterAsync(steam, "SecondProgressionHero", now, default);
        const string otherSteam = "76561198000000992";
        await game.UpsertAccountAsync(new(otherSteam, otherSteam, false, false), now, default);
        GameCharacter otherCharacter = await game.CreateCharacterAsync(otherSteam, "OtherProgressionHero", now, default);
        IReadOnlyDictionary<Guid, CharacterProgressionSummary> roster = await game.GetStoredProgressionSummariesAsync(steam, default);
        Require(roster.Count == 2 && roster[id].ClassId == FirstSliceProgression.ClassId && roster[id].Level == 1
            && roster[secondCharacter.CharacterId].Level == 1 && !roster.ContainsKey(otherCharacter.CharacterId),
            "Stored roster summaries must separate characters and exclude another Steam account.");
        Require((await game.GetStoredProgressionSummariesAsync("unowned", default)).Count == 0,
            "An account without characters must receive no summaries.");
        Require(CharacterProgressionSummary.FromStored(id, null) == CharacterProgressionSummary.Missing
            && CharacterProgressionSummary.FromStored(Guid.NewGuid(), state) == CharacterProgressionSummary.Unavailable
            && CharacterProgressionSummary.FromStored(id, state with { Experience = 3 }) == CharacterProgressionSummary.Unavailable
            && CharacterProgressionSummary.FromStored(id, state with { ClassId = "RPGPlayerClassDefinition:Unknown" }) == CharacterProgressionSummary.Unavailable,
            "Missing, invalid, foreign and unsupported saves must not be presented as fresh level-one characters.");
        CharacterProgressionState draft = FirstSliceProgression.Copy(state) with { ClassId = FirstSliceProgression.LegacyClassId };
        Require(CharacterProgressionSummary.FromStored(id, draft).ClassId == FirstSliceProgression.ClassId
            && draft.ClassId == FirstSliceProgression.LegacyClassId && draft.Revision == state.Revision,
            "Display normalization must not migrate or advance the stored record.");
        CharacterProgressionState migrated = FirstSliceProgression.MigrateStoredState(draft);
        Require(migrated.ClassId == FirstSliceProgression.ClassId && migrated.Revision == draft.Revision + 1
            && migrated.Experience == draft.Experience && migrated.Skills.Count == draft.Skills.Count
            && FirstSliceProgression.MigrateStoredState(migrated).Revision == migrated.Revision,
            "The known draft class ID must migrate once without resetting growth.");
        bool unknownRejected = false;
        try { FirstSliceProgression.MigrateStoredState(draft with { ClassId = "RPGPlayerClassDefinition:Unknown" }); }
        catch (ProgressionConflictException) { unknownRejected = true; }
        Require(unknownRejected, "Unknown class IDs must not be redirected to the greatsword class.");
        Require(state.Experience == 0 && FirstSliceProgression.TotalSP(state) == 0
            && state.Skills.Count == 2 && !FirstSliceProgression.IdentityUnlocked(state), "Creation recipe must not unlock later growth.");
        Require(await game.LoadProgressionAsync(Guid.NewGuid(), now, default) is null, "A missing save must not become a fresh character.");
        Require(!FirstSliceProgression.CanEnter(state, FirstSliceProgression.Intro1, "Normal")
            && !FirstSliceProgression.CanEnter(state, FirstSliceProgression.Boss, "Hard"), "Progression content must enforce prerequisites.");

        DungeonSession training = await Start(FirstSliceProgression.Training);
        await Reject(() => game.CommitSkillConfigurationAsync(id, steam,
            new(Guid.NewGuid(), state.Revision, state.Skills, state.SkillSlots), now, default), "active_dungeon_configuration_locked");
        await Queue(training);
        Require((await Read()).Experience == 0, "Pending/processing settlement is not earned experience.");
        await game.CompleteDungeonRewardSettlementAsync(training.DungeonSessionId, worker, now, default);
        Require((await game.GetStoredProgressionSummariesAsync(steam, default))[id].Level == 1
            && (await game.GetStoredProgressionSummariesAsync(steam, default))[id].Level == 1,
            "Roster reads must not reconcile a pending growth award.");
        state = await Read();
        Require(state.Experience == 100 && FirstSliceProgression.TotalSP(state) == 3
            && state.Skills.Count == 3 && FirstSliceProgression.IdentityUnlocked(state), "Training must unlock identity and whirlwind once.");
        roster = await game.GetStoredProgressionSummariesAsync(steam, default);
        Require(roster[id].Level == 2 && roster[secondCharacter.CharacterId].Level == 1,
            "Stored summaries must reflect one character's confirmed growth without leveling its sibling.");
        Require((await Read()).Revision == state.Revision && (await Read()).Experience == 100, "Repeated reads must not duplicate XP.");
        Require(state.SkillSlots[2] == "", "Acquisition must preserve deliberately empty slots.");
        ((string[])state.Skills[0].Tripods)[0] = "ForgedMutation";
        Require((await Read()).Skills[0].Tripods[0] == "", "Returned arrays must not mutate stored progression.");

        await Clear(FirstSliceProgression.Training, version: "unrelated_reward_v1");
        Require((await Read()).Experience == 100, "An unrelated reward version must not grant first-slice XP.");
        await Clear(FirstSliceProgression.Intro1);
        await Read();
        await Clear(FirstSliceProgression.Intro2);
        await Read();
        await Clear(FirstSliceProgression.Intro3);
        state = await Read();
        Require(state.Experience == 400 && state.Skills.Count == 4 && FirstSliceProgression.TotalSP(state) == 12,
            "The three intro milestones must produce the documented basic build budget.");
        Require(FirstSliceProgression.CanEnter(state, FirstSliceProgression.Boss, "Normal")
            && !FirstSliceProgression.CanEnter(state, FirstSliceProgression.Boss, "Hard"), "Normal and hard must unlock separately.");

        string FirstChoice(string tag) => tag switch
        {
            FirstSliceProgression.Assault => "LongReach", FirstSliceProgression.Charge => "FullCharge",
            FirstSliceProgression.Whirlwind => "Mobile", FirstSliceProgression.Jump => "WideArea", _ => throw new Exception()
        };
        ProgressionSkill[] baseline = state.Skills.Select(skill => skill with
            { Level = 4, Tripods = new[] { FirstChoice(skill.SkillTag), "" } }).ToArray();
        string[] slots = baseline.Select(skill => skill.SkillTag).Concat(Enumerable.Repeat("", 4)).ToArray();
        SkillConfigurationCommand command = new(Guid.NewGuid(), state.Revision, baseline, slots);
        await Reject(() => game.CommitSkillConfigurationAsync(id, "different-owner", command, now, default), "character_not_owned");
        await Reject(() => game.CommitSkillConfigurationAsync(id, steam,
            command with { Skills = baseline.Select(skill => skill with { Level = 7 }).ToArray() }, now, default), "invalid_sp_or_skill_slots");
        await Reject(() => game.CommitSkillConfigurationAsync(id, steam,
            command with { Skills = baseline.Select(skill => skill with { Tripods = new[] { "Unknown", "" } }).ToArray() }, now, default), "invalid_tripod_selection");
        await Reject(() => game.CommitSkillConfigurationAsync(id, steam,
            command with { SkillSlots = Enumerable.Repeat(FirstSliceProgression.Charge, 8).ToArray() }, now, default), "invalid_sp_or_skill_slots");
        CharacterProgressionState saved = await game.CommitSkillConfigurationAsync(id, steam, command, now, default);
        Require(FirstSliceProgression.UsedSP(saved) == 12 && saved.Revision == state.Revision + 1, "Valid 4/4/4/4 configuration must persist.");
        Require((await game.CommitSkillConfigurationAsync(id, steam, command, now, default)).Revision == saved.Revision,
            "Exact request replay must return its original receipt.");
        await Reject(() => game.CommitSkillConfigurationAsync(id, steam,
            command with { SkillSlots = Enumerable.Repeat("", 8).ToArray() }, now, default), "idempotency_conflict");
        await Reject(() => game.CommitSkillConfigurationAsync(id, steam,
            command with { RequestId = Guid.NewGuid() }, now, default), "stale_progression_revision");

        DungeonSession failed = await Start(FirstSliceProgression.Boss);
        await game.FinishDungeonSessionAsync(failed.DungeonSessionId, server, DungeonSessionState.Failed, now, default);
        Require((await Read()).Experience == 400, "A failed dungeon must not grant XP or unlock hard.");
        await Clear(FirstSliceProgression.Boss);
        state = await Read();
        Require(state.Experience == 500 && FirstSliceProgression.CanEnter(state, FirstSliceProgression.Boss, "Hard")
            && FirstSliceProgression.UsedSP(state) == 12, "Growth must preserve saved skill investments and unlock hard after normal.");
        long currentRevision = state.Revision;
        Require((await game.CommitSkillConfigurationAsync(id, steam, command, now, default)).Revision == saved.Revision
            && (await Read()).Revision == currentRevision, "Old receipt replay must not overwrite newer earned progression.");

        for (int i = 0; i < 8; i++) { await Clear(FirstSliceProgression.Boss); await Read(); }
        state = await Read();
        Require(state.Experience == 900 && FirstSliceProgression.Level(state) == 10 && FirstSliceProgression.TotalSP(state) == 18,
            "Experience and SP must stop at the first-slice cap.");
        ProgressionSkill[] focused = state.Skills.Select(skill => skill.SkillTag == FirstSliceProgression.Charge
            ? skill with { Level = 7, Tripods = new[] { "FullCharge", "PartBreak" } } : skill).ToArray();
        state = await game.CommitSkillConfigurationAsync(id, steam,
            new(Guid.NewGuid(), state.Revision, focused, slots), now, default);
        Require(FirstSliceProgression.UsedSP(state) == 18, "The documented 7/4/4/4 build must fit exactly.");
        await Clear(FirstSliceProgression.Boss, "Hard");
        state = await Read();
        Require(state.Experience == 900 && (state.Milestones & 32) != 0 && FirstSliceProgression.UsedSP(state) == 18,
            "Hard unlock must persist at XP cap without discarding the configuration.");

        long expected = state.Revision;
        async Task<bool> Race()
        {
            try { await game.CommitSkillConfigurationAsync(id, steam, new(Guid.NewGuid(), expected, focused, slots), now, default); return true; }
            catch (ProgressionConflictException exception) when (exception.Message == "stale_progression_revision") { return false; }
        }
        bool[] outcomes = await Task.WhenAll(Task.Run(Race), Task.Run(Race));
        Require(outcomes.Count(success => success) == 1 && (await Read()).Revision == expected + 1,
            "Concurrent updates to one revision must have exactly one winner.");
        Console.WriteLine("CharacterProgression: creation, settlement authority/deduplication, unlocks, save ownership, SP/tripods/slots, replay, failure, cap and concurrent revision checks passed.");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private static async Task Reject(Func<Task> action, string code)
    {
        try { await action(); }
        catch (ProgressionConflictException exception) when (exception.Message == code) { return; }
        throw new InvalidOperationException("Expected progression rejection: " + code);
    }
}
