using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;

internal static class DungeonServerEndpointTests
{
    public static async Task Run()
    {
        using ProfileHost host = new("Development");
        using HttpClient http = new() { BaseAddress = await host.StartAsync(), Timeout = TimeSpan.FromSeconds(10) };
        async Task<JsonElement> Send(HttpMethod method, string path, object? body, string? token, HttpStatusCode expected,
            string? failure = null)
        {
            using HttpRequestMessage request = new(method, path);
            if (body is not null) request.Content = JsonContent.Create(body);
            if (token is not null) request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
            using HttpResponseMessage response = await http.SendAsync(request);
            Require(response.StatusCode == expected, failure ?? $"Unexpected endpoint status for {method} {path}: {(int)response.StatusCode}.");
            return expected is HttpStatusCode.OK or HttpStatusCode.Created
                ? await response.Content.ReadFromJsonAsync<JsonElement>() : default;
        }
        int index = 0;
        async Task<(string Session, string Token)> Waiting()
        {
            string steam = (76561198000000100UL + (ulong)index++).ToString(System.Globalization.CultureInfo.InvariantCulture);
            JsonElement login = await Send(HttpMethod.Post, "/api/auth/steam-ticket", new { ticket = "dev:" + steam }, null, HttpStatusCode.OK);
            string token = login.GetProperty("accessToken").GetString()!;
            JsonElement character = await Send(HttpMethod.Post, "/api/characters", new
            {
                name = "Endpoint" + Guid.NewGuid().ToString("N")[..8], classId = "RPGPlayerClassDefinition:GreatSwordWarrior"
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
            return (id, token);
        }
        var waiting = await Waiting();
        string path = $"/api/dungeon-sessions/{waiting.Session}";
        JsonElement before = await Send(HttpMethod.Get, path, null, waiting.Token, HttpStatusCode.OK);
        await Send(HttpMethod.Post, path + "/activate", new
        {
            serverId = "endpoint-check", serverAddress = "user:fixture-password@127.0.0.1:7777"
        }, host.AdminToken, HttpStatusCode.BadRequest,
            "Backend accepted user-info in a server host:port address.");

        string?[] invalid = [null, "", "@127.0.0.1:7777", "user@localhost:7777",
            "127.0.0.1:7777\\other", "127.0.0.1:7777/", "host:7777?game=other", "host:7777#part",
            "host", "host:0", "host:65536", "::1:7777", "[::1]", "[::1]:0", "[::1]:65536",
            "host name:7777", "host:77\n77", "udp://host:7777"];
        foreach (string? address in invalid)
        {
            var body = new { serverId = "endpoint-check", serverAddress = address };
            await Send(HttpMethod.Post, path + "/activate", body, host.AdminToken, HttpStatusCode.BadRequest);
            await Send(HttpMethod.Post, "/api/dungeon-sessions/claim", body, host.AdminToken, HttpStatusCode.BadRequest);
        }
        JsonElement unchanged = await Send(HttpMethod.Get, path, null, waiting.Token, HttpStatusCode.OK);
        Require(unchanged.GetRawText() == before.GetRawText(), "Bad endpoint changed queue, lease, party or assignment.");
        Console.WriteLine("PASS server endpoint: user-info/path/control/missing-port/range errors rejected by activate and claim without mutation");

        var valid = new { serverId = "endpoint-check", serverAddress = "127.0.0.1:7777" };
        await Send(HttpMethod.Post, path + "/activate", valid, waiting.Token, HttpStatusCode.Forbidden);
        await Send(HttpMethod.Post, path + "/activate", valid, null, HttpStatusCode.Unauthorized);
        await Send(HttpMethod.Post, "/api/dungeon-sessions/claim", valid, waiting.Token, HttpStatusCode.Forbidden);
        Require((await Send(HttpMethod.Get, path, null, waiting.Token, HttpStatusCode.OK)).GetRawText() == before.GetRawText(),
            "Unauthorized assignment changed the waiting session.");
        Console.WriteLine("PASS server endpoint: valid address does not bypass administrator-only allocation");

        foreach (string address in new[] { "127.0.0.1:7777", "game.example.invalid:65535", "[::1]:7777", "[2001:db8::1]:1" })
        {
            // Metadata-only assignments: never launch/connect to these endpoints.
            JsonElement assignment = await Send(HttpMethod.Post, path + "/activate", new
            {
                serverId = "endpoint-valid-" + index, serverAddress = address
            }, host.AdminToken, HttpStatusCode.OK);
            Require(assignment.GetProperty("serverAddress").GetString() == address
                && assignment.GetProperty("dungeonSessionId").GetString() == waiting.Session
                && assignment.GetProperty("state").GetString() == "Loading", "Valid endpoint was altered during assignment.");
            JsonElement view = await Send(HttpMethod.Get, path, null, waiting.Token, HttpStatusCode.OK);
            Require(view.GetProperty("serverAddress").GetString() == address, "Assigned endpoint did not round-trip through session GET.");
            waiting = await Waiting();
            path = $"/api/dungeon-sessions/{waiting.Session}";
        }
        Console.WriteLine("PASS server endpoint: IPv4/DNS/bracketed IPv6 and port bounds retain exact assignment metadata");
    }

    private static void Require(bool condition, string message) { if (!condition) throw new TestFailure(message); }
}
