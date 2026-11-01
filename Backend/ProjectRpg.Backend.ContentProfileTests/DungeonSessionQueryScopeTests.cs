using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;

internal static class DungeonSessionQueryScopeTests
{
    public static async Task Run()
    {
        // Runs the real backend entrypoint, not a duplicate of its GET handler.
        // ProfileHost forces private Memory/Development Training and owns its child.
        using ProfileHost host = new("Development");
        using HttpClient http = new() { BaseAddress = await host.StartAsync(), Timeout = TimeSpan.FromSeconds(10) };
        async Task<JsonElement> Send(HttpMethod method, string path, object? body, string? token,
            HttpStatusCode expected, bool noStore = false)
        {
            using HttpRequestMessage request = new(method, path);
            if (body is not null) request.Content = JsonContent.Create(body);
            if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
            using HttpResponseMessage response = await http.SendAsync(request);
            Require(response.StatusCode == expected, $"Unexpected HTTP status for {method} {path}: {(int)response.StatusCode}.");
            if (noStore) Require(response.Headers.CacheControl?.NoStore == true, "Private session response was cacheable.");
            return expected is HttpStatusCode.OK or HttpStatusCode.Created
                ? await response.Content.ReadFromJsonAsync<JsonElement>() : default;
        }
        async Task<(string Token, string Character, string Session, string ServerToken)> AssignedPlayer(string steam, string suffix, int port)
        {
            JsonElement auth = await Send(HttpMethod.Post, "/api/auth/steam-ticket", new { ticket = "dev:" + steam }, null, HttpStatusCode.OK);
            string token = auth.GetProperty("accessToken").GetString()!;
            JsonElement character = await Send(HttpMethod.Post, "/api/characters", new
            {
                name = "Scope" + suffix + Guid.NewGuid().ToString("N")[..6], classId = "RPGPlayerClassDefinition:GreatSwordWarrior"
            }, token, HttpStatusCode.Created);
            string characterId = character.GetProperty("characterId").GetString()!;
            JsonElement session = await Send(HttpMethod.Post, "/api/dungeon-sessions", new
            {
                characterId, dungeonId = "Progression.Training", difficulty = "Normal"
            }, token, HttpStatusCode.Created);
            string id = session.GetProperty("dungeonSessionId").GetString()!;
            await Send(HttpMethod.Post, $"/api/dungeon-sessions/{id}/departure", new
            {
                characterId, requestId = Guid.NewGuid(), expectedLobbyRevision = session.GetProperty("lobbyRevision").GetInt64()
            }, token, HttpStatusCode.OK);
            JsonElement assignment = await Send(HttpMethod.Post, $"/api/dungeon-sessions/{id}/activate", new
            {
                serverId = "same-server-label", serverAddress = "127.0.0.1:" + port
            }, host.AdminToken, HttpStatusCode.OK);
            return (token, characterId, id, assignment.GetProperty("gameServerAccessToken").GetString()!);
        }

        var a = await AssignedPlayer("76561198000000001", "A", 19991);
        var b = await AssignedPlayer("76561198000000002", "B", 19992);
        Require(a.Session != b.Session && a.ServerToken != b.ServerToken, "Assignments were not independently scoped.");
        string Path(string id) => "/api/dungeon-sessions/" + id;
        JsonElement beforeA = await Send(HttpMethod.Get, Path(a.Session), null, a.ServerToken, HttpStatusCode.OK);
        JsonElement beforeB = await Send(HttpMethod.Get, Path(b.Session), null, b.ServerToken, HttpStatusCode.OK);
        Require(beforeA.GetProperty("serverId").GetString() == "same-server-label"
            && beforeB.GetProperty("serverId").GetString() == "same-server-label", "Server label collision was not reproduced.");
        Console.WriteLine("PASS session query: real player/departure/admin assignments issue distinct tokens for the same server label");

        // The old route authorizes by server label alone and returns 200 here.
        using (HttpRequestMessage request = new(HttpMethod.Get, Path(b.Session)))
        {
            request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", a.ServerToken);
            using HttpResponseMessage response = await http.SendAsync(request);
            Require(response.StatusCode == HttpStatusCode.Forbidden,
                "Cross-session server token was accepted when ServerId labels matched.");
        }
        await Send(HttpMethod.Get, Path(a.Session), null, b.ServerToken, HttpStatusCode.Forbidden, noStore: true);
        await Send(HttpMethod.Get, Path(Guid.NewGuid().ToString("D")), null, a.ServerToken, HttpStatusCode.Forbidden, noStore: true);
        Console.WriteLine("PASS session query: foreign/unknown sessions denied before lookup despite identical server IDs");

        foreach (var player in new[] { a, b })
        {
            JsonElement owner = await Send(HttpMethod.Get, Path(player.Session), null, player.Token, HttpStatusCode.OK, noStore: true);
            JsonElement server = await Send(HttpMethod.Get, Path(player.Session), null, player.ServerToken, HttpStatusCode.OK, noStore: true);
            JsonElement admin = await Send(HttpMethod.Get, Path(player.Session), null, host.AdminToken, HttpStatusCode.OK, noStore: true);
            Require(owner.GetRawText() == server.GetRawText() && server.GetRawText() == admin.GetRawText(), "Authorized views diverged.");
            Require(!server.TryGetProperty("gameServerAccessToken", out JsonElement credential)
                || credential.ValueKind == JsonValueKind.Null, "Session GET returned an assignment credential.");
        }
        await Send(HttpMethod.Get, Path(a.Session), null, b.Token, HttpStatusCode.Forbidden);
        await Send(HttpMethod.Get, Path(b.Session), null, a.Token, HttpStatusCode.Forbidden);
        await Send(HttpMethod.Get, Path(a.Session), null, null, HttpStatusCode.Unauthorized);
        await Send(HttpMethod.Get, Path(a.Session), null, "invalid-token", HttpStatusCode.Unauthorized);
        await Send(HttpMethod.Get, Path(Guid.NewGuid().ToString("D")), null, host.AdminToken, HttpStatusCode.NotFound);
        Console.WriteLine("PASS session query: own server/member/admin reads retained; foreign players and unauthenticated reads rejected");

        await Send(HttpMethod.Get, Path(b.Session) + "/membership-snapshot", null, a.ServerToken, HttpStatusCode.Forbidden);
        await Send(HttpMethod.Get, Path(b.Session) + "/reward-snapshot/" + b.Character, null, a.ServerToken, HttpStatusCode.Forbidden);
        await Send(HttpMethod.Post, Path(b.Session) + "/start", new { serverId = "same-server-label" }, a.ServerToken, HttpStatusCode.Forbidden);
        JsonElement afterA = await Send(HttpMethod.Get, Path(a.Session), null, host.AdminToken, HttpStatusCode.OK);
        JsonElement afterB = await Send(HttpMethod.Get, Path(b.Session), null, host.AdminToken, HttpStatusCode.OK);
        Require(afterA.GetRawText() == beforeA.GetRawText() && afterB.GetRawText() == beforeB.GetRawText(),
            "Queries/denials changed party, assignment, revision or lease.");
        Console.WriteLine("PASS session query: existing snapshot/start fences and full session state preserved");
    }

    private static void Require(bool condition, string message) { if (!condition) throw new TestFailure(message); }
}
