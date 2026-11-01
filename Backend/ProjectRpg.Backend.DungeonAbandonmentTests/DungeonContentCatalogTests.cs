using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using static Tests;

static class DungeonContentCatalogTests
{
    private static DungeonContentEntryOptions Entry(string id = "Dungeon.A", string map = "/Game/Maps/A",
        params string[] difficulties) => new()
        { DungeonId = id, MapPackage = map, Difficulties = difficulties.Length == 0 ? ["Normal"] : [.. difficulties] };

    public static void CheckPolicy()
    {
        DungeonContentEntryOptions a = Entry();
        DungeonContentCatalog catalog = new(new() { Entries = [a, Entry("Dungeon.B", "/Game/Maps/B", "Hard")] });
        Require(catalog.GetMapPackage("Dungeon.A", "Normal") == "/Game/Maps/A"
            && catalog.GetMapPackage("Dungeon.B", "Hard") == "/Game/Maps/B", "Exact content map resolution failed.");
        foreach (string id in new[] { "Dungeon.Unknown", "dungeon.A", " Dungeon.A", "Dungeon.A " })
            Require(catalog.GetSelectionError(id, "Normal") == "dungeon_content_not_available", "Unknown/aliased ID admitted.");
        foreach (string difficulty in new[] { "Hard", "normal", " Normal", "Nightmare", "" })
            Require(catalog.GetSelectionError("Dungeon.A", difficulty) == "dungeon_difficulty_not_available", "Unsupported difficulty admitted.");
        a.Difficulties.Clear();
        Require(catalog.GetMapPackage("Dungeon.A", "Normal") == "/Game/Maps/A"
            && catalog.AllowedSelections.Count == 2, "Catalog must snapshot mutable options.");
        Require(catalog.GetSelectionError(null, null) is not null, "Null selection admitted.");
        Require(new DungeonContentCatalog(new() { Entries = [Entry(map: "/Engine/Maps/Entry")] })
            .GetMapPackage("Dungeon.A", "Normal") == "/Engine/Maps/Entry", "Engine fixture map was not preserved.");
        Console.WriteLine("PASS content catalog: exact IDs/difficulties, per-ID maps, frozen snapshot");

        List<DungeonContentOptions> invalid = [new(), new() { Entries = [null] },
            new() { Entries = [Entry(), Entry()] },
            new() { Entries = [Entry(" invalid")] },
            new() { Entries = [Entry(difficulties: ["Normal", "Normal"])] },
            new() { Entries = [Entry(difficulties: ["Nightmare"])] },
            new() { Entries = [new() { DungeonId = "Dungeon.A", MapPackage = "/Game/Maps/A" }] },
            new() { Entries = Enumerable.Range(0, 65).Select(i => (DungeonContentEntryOptions?)Entry($"Dungeon.{i}")).ToList() }];
        foreach (string map in new[] { "/Game/Maps/A?listen", "/Game/Maps/A.A", "/Game/../A", "/Game//A",
            "/Unknown/Maps/A", "/game/Maps/A", "/Game/Maps/A -exec=quit", "/Game/Maps/A\n", "", new string('a', 257) })
            invalid.Add(new() { Entries = [Entry(map: map)] });
        foreach (DungeonContentOptions options in invalid)
        {
            bool rejected = false;
            try { _ = new DungeonContentCatalog(options); }
            catch (InvalidOperationException) { rejected = true; }
            Require(rejected, "Malformed/duplicate/empty catalog did not fail closed.");
        }
        Console.WriteLine($"PASS content catalog: {invalid.Count} invalid configurations fail closed");
    }

    public static async Task RunSuite(string provider, IGameRepository repository)
    {
        DateTimeOffset now = DateTimeOffset.UtcNow;
        async Task<DungeonSession> Waiting(string id, string difficulty, int order)
        {
            string steam = BitConverter.ToUInt64(Guid.NewGuid().ToByteArray()).ToString("D20");
            await repository.UpsertAccountAsync(new(steam, steam, false, false), now, default);
            GameCharacter character = await repository.CreateCharacterAsync(steam,
                "C" + Guid.NewGuid().ToString("N")[..18], now, default);
            return await repository.CreateDungeonSessionAsync(steam, character.CharacterId, id, difficulty,
                now.AddSeconds(order), now.AddHours(1), default);
        }

        DungeonSession removed = await Waiting("Dungeon.Removed", "Normal", -5);
        DungeonSession wrongPair = await Waiting("Dungeon.A", "Hard", -4);
        DungeonSession b = await Waiting("Dungeon.B", "Hard", -3);
        DungeonSession a1 = await Waiting("Dungeon.A", "Normal", -2);
        DungeonSession a2 = await Waiting("Dungeon.A", "Normal", -1);
        IReadOnlyList<DungeonContentSelection> allowed = Array.AsReadOnly(new DungeonContentSelection[]
            { new("Dungeon.A", "Normal"), new("Dungeon.B", "Hard") });
        Require(await repository.ClaimNextDungeonSessionAsync("empty", "127.0.0.1:7777", now,
            now.AddMinutes(5), default, []) is null, "Explicit empty allowlist must claim nothing.");
        DungeonSession? first = await repository.ClaimNextDungeonSessionAsync("first", "127.0.0.1:7777", now,
            now.AddMinutes(5), default, allowed);
        Require(first?.DungeonSessionId == b.DungeonSessionId && first.State == DungeonSessionState.Loading,
            "Claim must skip older removed content and wrong ID/difficulty pairs, preserving supported FIFO.");
        Console.WriteLine($"PASS {provider} content claim: paired filter, empty deny, unsupported FIFO bypass");

        DungeonSession?[] concurrent = await Task.WhenAll(Enumerable.Range(0, 4).Select(i =>
            repository.ClaimNextDungeonSessionAsync($"concurrent-{i}", $"127.0.0.1:{7780 + i}", now,
                now.AddMinutes(5), default, allowed)));
        Guid[] claimed = concurrent.Where(value => value is not null).Select(value => value!.DungeonSessionId).ToArray();
        Require(claimed.Length == 2 && claimed.Distinct().Count() == 2
            && claimed.Contains(a1.DungeonSessionId) && claimed.Contains(a2.DungeonSessionId),
            "Concurrent catalog claims must not double-assign or take unsupported sessions.");
        foreach (DungeonSession original in new[] { removed, wrongPair })
        {
            DungeonSession? restored = await repository.GetDungeonSessionAsync(original.DungeonSessionId, now, default);
            Require(restored is not null && restored.State == DungeonSessionState.Waiting && restored.ServerId is null
                && restored.Members.Count == 1 && restored.ExpiresAt == original.ExpiresAt,
                "Skipped legacy session/membership/lease was changed or lost.");
        }
        // Internal callers retain their pre-catalog signature; production HTTP
        // always passes the validated catalog instead of this compatibility path.
        Require((await repository.ClaimNextDungeonSessionAsync("internal", "127.0.0.1:7790", now,
            now.AddMinutes(5), default))?.DungeonSessionId == removed.DungeonSessionId,
            "Legacy repository overload behavior changed.");
        Console.WriteLine($"PASS {provider} content claim: concurrent unique assignment and legacy session preservation");
    }
}
