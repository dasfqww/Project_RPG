using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using ProjectRpg.Backend.Api;
using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Authentication;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Narrative;

internal static class NarrativeStoryAdmissionTests
{
    private const string Dungeon = "Test.StoryGate";
    private static DungeonContentCatalog Content(List<string> required) => new(new DungeonContentOptions
    {
        Entries = [new() { DungeonId = Dungeon, MapPackage = "/Game/Test", Difficulties = ["Normal", "Hard"], RequiredStoryUnlocks = required }]
    });

    public static async Task RunAsync(IGameRepository game, string provider)
    {
        var baseline = NarrativeCatalog.Development;
        var quest = baseline.Quests.Values.Single();
        var second = quest with { Id = "DEV.Second" };
        var catalog = new NarrativeCatalog(1, [quest, second], [.. baseline.Chapters.Values,
            new("DEV.SecondChapter", [second.Id], ["DEV.Story.Second"])],
            baseline.Dialogues.Values.Concat(baseline.Dialogues.Values.Select(d => d with { Id = "SECOND." + d.Id, QuestId = second.Id })));
        foreach (List<string> invalid in new List<string>[] { ["missing"], ["DEV.Story.Next", "DEV.Story.Next"], [" bad"], null!, Enumerable.Repeat("DEV.Story.Next", 257).ToList() })
        {
            try { _ = new DungeonStoryAdmission(Content(invalid), catalog); }
            catch (InvalidOperationException) { continue; }
            throw new TestFailure("Invalid story admission configuration accepted.");
        }
        var mutable = new List<string> { "DEV.Story.Next" };
        var admission = new DungeonStoryAdmission(Content(mutable), catalog);
        mutable.Clear(); // Options cannot remove a gate from the process snapshot.
        var both = new DungeonStoryAdmission(Content(["DEV.Story.Next", "DEV.Story.Second"]), catalog);
        var unrestricted = new DungeonStoryAdmission(Content([]), NarrativeCatalog.Empty);
        var repository = (INarrativeRepository)game;
        var now = DateTimeOffset.UtcNow;
        async Task<GameCharacter> Character(string owner)
        {
            await game.UpsertAccountAsync(new(owner, owner, false, false), now, default);
            return await game.CreateCharacterAsync(owner, "Story" + Guid.NewGuid().ToString("N")[..10], now, default);
        }
        var leader = await Character("76561198000000011");
        var member = await Character("76561198000000012");
        var alt = await Character(leader.SteamId);
        var damaged = await Character("76561198000000013");
        var quests = new QuestManager();
        var coordinator = new NarrativeCoordinator(repository, catalog, quests, new ScenarioManager(), new DialogueManager(quests), TimeProvider.System);
        Task<string?> Error(GameCharacter who, DungeonStoryAdmission? gate = null) => (gate ?? admission).GetErrorAsync(game, who.CharacterId, who.SteamId, Dungeon, default);
        async Task Choice(GameCharacter who, long revision, string node, string choice) => await coordinator.ChooseAsync(who.CharacterId,
            who.SteamId, new(Guid.NewGuid(), revision, 1, node, choice), default);
        async Task Complete(GameCharacter who, long revision = 0, string prefix = "")
        {
            await Choice(who, revision, prefix + "DEV.Contact.Offer", "accept");
            await Choice(who, revision + 1, prefix + "DEV.Contact.Explain", "confirm");
            await Choice(who, revision + 2, prefix + "DEV.Instructor.Explain", "confirm");
            await Choice(who, revision + 3, prefix + "DEV.Contact.Report", "report");
        }
        Require(await Error(leader) == "story_prerequisite_not_met", "Unstarted story entered.");
        Require((await repository.LoadNarrativeAsync(leader.CharacterId, leader.SteamId, default)).Revision == 0, "Gate wrote initial progress.");
        string growth = JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(leader.CharacterId, now, default));
        await Choice(leader, 0, "DEV.Contact.Offer", "accept");
        Require(await Error(leader) == "story_prerequisite_not_met", "Active story entered.");
        await Choice(leader, 1, "DEV.Contact.Explain", "confirm");
        await Choice(leader, 2, "DEV.Instructor.Explain", "confirm");
        Require(await Error(leader) == "story_prerequisite_not_met", "ReadyToReport story entered.");
        await Choice(leader, 3, "DEV.Contact.Report", "report");
        Require(await Error(leader) is null && await Error(leader, both) == "story_prerequisite_not_met", "Completed/AND qualification incorrect.");
        await Complete(leader, 4, "SECOND.");
        Require(await Error(leader, both) is null, "All story requirements did not open entry.");
        Require(await Error(alt) == "story_prerequisite_not_met" && await Error(member) == "story_prerequisite_not_met", "Qualification borrowed from leader/account.");
        Require(await Error(alt, unrestricted) is null, "Legacy ungated content changed.");
        await repository.CommitNarrativeAsync(damaged.CharacterId, damaged.SteamId,
            new(Guid.NewGuid(), 0, 1, "test", "test"), state => state with { Revision = 1, StoryUnlocks = ["DEV.Story.Next"] }, now, default);
        Require(await Error(damaged) == "story_progress_unavailable", "Contradictory save authorized entry.");
        Require(await admission.GetErrorAsync(game, leader.CharacterId, member.SteamId, Dungeon, default) == "story_progress_unavailable", "Foreign owner authorized entry.");

        // A pre-policy/legacy party and already issued ticket must still be gated
        // at final confirmation, while consumed-ticket recovery keeps its receipt.
        var session = await game.CreateDungeonSessionAsync(leader.SteamId, leader.CharacterId, Dungeon, "Normal", now, now.AddMinutes(5), default);
        await game.JoinDungeonSessionAsync(session.DungeonSessionId, member.SteamId, member.CharacterId, 4, now, now.AddMinutes(5), default);
        await game.AssignDungeonServerAsync(session.DungeonSessionId, "story-test", "127.0.0.1:17777", now, now.AddMinutes(5), default);
        var tokens = new AccessTokenService(TimeProvider.System);
        var player = tokens.Issue(TimeSpan.FromMinutes(5)); var server = tokens.Issue(TimeSpan.FromMinutes(5));
        var legacy = tokens.Issue(TimeSpan.FromMinutes(5));
        await game.StoreSessionAsync(player.TokenHash, member.SteamId, player.ExpiresAt, default);
        await game.StoreGameServerCredentialAsync(server.TokenHash, "story-test", session.DungeonSessionId, server.ExpiresAt, default);
        Require(await game.StoreJoinTicketAsync(legacy.TokenHash, member.SteamId, member.CharacterId, session.DungeonSessionId, legacy.ExpiresAt, default) == JoinTicketIssueResult.Issued, "Legacy ticket fixture failed.");
        var builder = WebApplication.CreateBuilder(new WebApplicationOptions { EnvironmentName = "Testing" });
        builder.Configuration.Sources.Clear(); builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders(); builder.WebHost.UseUrls("http://127.0.0.1:0");
        builder.Services.AddSingleton(game); builder.Services.AddSingleton(tokens); builder.Services.AddSingleton<TimeProvider>(TimeProvider.System);
        builder.Services.AddSingleton(admission); builder.Services.AddScoped<BearerAuthenticator>();
        builder.Services.Configure<AuthOptions>(_ => { }); builder.Services.Configure<SecurityTelemetryOptions>(_ => { });
        await using var app = builder.Build(); app.UseMiddleware<ApiAuthenticationMiddleware>(); app.MapJoinTicketApi(); app.MapDungeonAdmissionApi();
        await app.StartAsync();
        try
        {
            using var client = new HttpClient { BaseAddress = new Uri(app.Services.GetRequiredService<IServer>().Features.Get<IServerAddressesFeature>()!.Addresses.Single()) };
            client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", player.Token);
            var denied = await client.PostAsJsonAsync("/api/join-tickets", new CreateJoinTicketRequest(member.CharacterId, session.DungeonSessionId));
            Require(denied.StatusCode == HttpStatusCode.Conflict && (await denied.Content.ReadAsStringAsync()).Contains("story_prerequisite_not_met"), "Locked legacy member obtained ticket.");
            client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", server.Token);
            var consumption = Guid.NewGuid();
            var consumeBody = new { joinTicket = legacy.Token, serverId = "story-test", consumptionRequestId = consumption };
            var consumed = await client.PostAsJsonAsync("/api/join-tickets/consume", consumeBody);
            Require(consumed.IsSuccessStatusCode, "Consumed-ticket recovery was blocked by story policy.");
            long generation = (await consumed.Content.ReadFromJsonAsync<JsonElement>()).GetProperty("connectionGeneration").GetInt64();
            string confirmRoute = $"/api/dungeon-sessions/{session.DungeonSessionId}/members/{member.CharacterId}/admission-confirmation";
            var confirm = new ConfirmDungeonAdmissionRequest(member.SteamId, generation);
            denied = await client.PostAsJsonAsync(confirmRoute, confirm);
            Require(denied.StatusCode == HttpStatusCode.Conflict && (await denied.Content.ReadAsStringAsync()).Contains("story_prerequisite_not_met"), "Old ticket bypassed final story gate.");
            Require((await game.GetDungeonSessionAsync(session.DungeonSessionId, now, default))!.Members.Single(m => m.CharacterId == member.CharacterId).ConfirmedConnectionGeneration == 0, "Rejected admission mutated confirmation.");
            await Complete(member);
            Require((await client.PostAsJsonAsync(confirmRoute, confirm)).IsSuccessStatusCode, "Qualified member could not retry same confirmation.");
            Require((await client.PostAsJsonAsync("/api/join-tickets/consume", consumeBody)).IsSuccessStatusCode, "Consumed-ticket receipt replay changed.");
        }
        finally { await app.StopAsync(); }
        Require(growth == JsonSerializer.Serialize(await ((ICharacterProgressionRepository)game).LoadProgressionAsync(leader.CharacterId, now, default)), "Story qualification changed growth.");
        Require((await repository.LoadNarrativeAsync(leader.CharacterId, leader.SteamId, default)).Revision == 8, "Admission checks mutated story progress.");
        Console.WriteLine($"PASS Narrative story admission {provider}: config references/limits, immutable gates, Active/Ready/Completed, AND, leader/alt isolation, corrupt save, legacy ticket/final confirmation, replay and growth unchanged.");
    }

    private static void Require(bool condition, string message) { if (!condition) throw new TestFailure(message); }
}
