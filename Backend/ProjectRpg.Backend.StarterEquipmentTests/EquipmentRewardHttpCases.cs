using System.Collections.Concurrent;
using System.Diagnostics;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.Json.Nodes;
using ProjectRpg.Backend.Domain;

internal static class EquipmentRewardHttpCases
{
    internal static async Task RunAsync(List<string> groups)
    {
        // Run the actual Program/endpoint/middleware/worker, not a copied handler.
        string assembly = typeof(DungeonItemReward).Assembly.Location;
        ProcessStartInfo start = new("dotnet")
        {
            WorkingDirectory = AppContext.BaseDirectory, UseShellExecute = false,
            RedirectStandardOutput = true, RedirectStandardError = true, CreateNoWindow = true
        };
        start.ArgumentList.Add(assembly);
        start.ArgumentList.Add("--urls"); start.ArgumentList.Add("http://127.0.0.1:0");
        string admin = Guid.NewGuid().ToString("N") + Guid.NewGuid().ToString("N");
        start.Environment["ASPNETCORE_ENVIRONMENT"] = "Development";
        start.Environment["DOTNET_ENVIRONMENT"] = "Development";
        start.Environment["Storage__Provider"] = "Memory";
        start.Environment["Auth__AdminToken"] = admin;
        start.Environment["Steam__AllowDevelopmentAuthentication"] = "true";
        start.Environment["DungeonContent__DevelopmentProfile"] = "Training";
        start.Environment["RateLimits__Enabled"] = "false";
        start.Environment["Logging__LogLevel__Default"] = "Warning";
        start.Environment["Logging__LogLevel__Microsoft.Hosting.Lifetime"] = "Information";
        TaskCompletionSource<string> address = new(TaskCreationOptions.RunContinuationsAsynchronously);
        ConcurrentQueue<string> tail = new();
        void Observe(object sender, DataReceivedEventArgs line)
        {
            if (line.Data is not string value) return;
            tail.Enqueue(value);
            while (tail.Count > 40) tail.TryDequeue(out _);
            const string marker = "Now listening on: ";
            int index = value.IndexOf(marker, StringComparison.Ordinal);
            if (index >= 0) address.TrySetResult(value[(index + marker.Length)..].Trim());
        }
        using Process process = new() { StartInfo = start };
        process.OutputDataReceived += Observe; process.ErrorDataReceived += Observe;
        Require(process.Start(), "Could not start own backend child.");
        process.BeginOutputReadLine(); process.BeginErrorReadLine();
        try
        {
            string url = await address.Task.WaitAsync(TimeSpan.FromSeconds(20));
            Require(new Uri(url).IsLoopback, "Backend did not bind loopback.");
            using HttpClient client = new() { BaseAddress = new(url), Timeout = TimeSpan.FromSeconds(10) };
            string suffix = Guid.NewGuid().ToString("N")[..8];
            string steam = "765614" + Random.Shared.NextInt64(10000000000, 99999999999);
            JsonObject auth = await Post(client, "/api/auth/steam-ticket", new { ticket = "dev:" + steam }, 200);
            string player = auth["accessToken"]!.GetValue<string>();
            client.DefaultRequestHeaders.Authorization = new("Bearer", player);
            JsonObject character = await Post(client, "/api/characters", new { name = "HttpRw" + suffix }, 201);
            string characterId = character["characterId"]!.GetValue<string>();
            JsonObject session = await Post(client, "/api/dungeon-sessions", new { characterId, dungeonId = "Progression.Training", difficulty = "Normal" }, 201);
            string id = session["dungeonSessionId"]!.GetValue<string>();
            string route = $"/api/dungeon-sessions/{id}";
            await Post(client, route + "/departure", new { characterId, requestId = Guid.NewGuid(), expectedLobbyRevision = session["lobbyRevision"]!.GetValue<long>() }, 200);
            client.DefaultRequestHeaders.Authorization = new("Bearer", admin);
            const string server = "reward-growth-http";
            JsonObject assigned = await Post(client, route + "/activate", new { serverId = server, serverAddress = "127.0.0.1:7777" }, 200);
            string serverToken = assigned["gameServerAccessToken"]!.GetValue<string>();
            client.DefaultRequestHeaders.Authorization = new("Bearer", serverToken);
            await Post(client, route + "/start", new { serverId = server }, 200);
            JsonObject payload = JsonSerializer.SerializeToNode(new
            {
                serverId = server, rewardVersion = "RewardGrowthHttp.v1", changes = Array.Empty<object>(),
                itemRewards = new[] { new { definitionType = "RPGItemDefinition", definitionName = EquipmentRewardCases.Target,
                    definitionVersion = 1, quantity = 1, maxStackSize = 1, bindState = "CharacterBound", durabilityCurrent = 0, durabilityMaximum = 0,
                    instanceTags = Array.Empty<string>(), statValues = Array.Empty<object>(), equipmentProgression = new { growthBandId = "FirstSlice", ruleVersion = 1, level = 0 } } }
            })!.AsObject();
            client.DefaultRequestHeaders.Authorization = null;
            await Status(client, route + "/settle-rewards", payload, 401);
            client.DefaultRequestHeaders.Authorization = new("Bearer", player);
            await Status(client, route + "/settle-rewards", payload, 403);
            client.DefaultRequestHeaders.Authorization = new("Bearer", serverToken);
            string[] malformed = ["{}", "{\"growthBandId\":\"FirstSlice\",\"ruleVersion\":1}",
                "{\"growthBandId\":\"FirstSlice\",\"level\":0}",
                "{\"growthBandId\":\"Other\",\"ruleVersion\":1,\"level\":0}",
                "{\"growthBandId\":\"FirstSlice\",\"ruleVersion\":2,\"level\":0}",
                "{\"growthBandId\":\"FirstSlice\",\"ruleVersion\":1,\"level\":4}",
                "{\"growthBandId\":\"FirstSlice\",\"ruleVersion\":1,\"level\":0.5}", "[]", "42"];
            foreach (string growth in malformed)
            {
                JsonObject bad = payload.DeepClone().AsObject();
                bad["itemRewards"]![0]!["equipmentProgression"] = JsonNode.Parse(growth);
                await Status(client, route + "/settle-rewards", bad, 400);
            }
            foreach ((string field, JsonNode? value) in new (string, JsonNode?)[]
                { ("maxStackSize", JsonValue.Create(0)), ("definitionName", JsonValue.Create(StarterEquipment.DefinitionName)), ("bindState", JsonValue.Create("AccountBound")) })
            {
                JsonObject bad = payload.DeepClone().AsObject(); bad["itemRewards"]![0]![field] = value;
                await Status(client, route + "/settle-rewards", bad, 400);
            }
            await Status(client, route + "/settle-rewards", payload, 202, 200);
            JsonObject ready;
            using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(10));
            while (true)
            {
                using HttpResponseMessage response = await client.GetAsync(route + "/reward-snapshot/" + characterId, timeout.Token);
                if ((int)response.StatusCode == 200) { ready = (await response.Content.ReadFromJsonAsync<JsonObject>(timeout.Token))!; break; }
                Require((int)response.StatusCode == 202, "Reward snapshot did not remain pending/ready.");
                await Task.Delay(25, timeout.Token);
            }
            JsonNode item = ready["items"]!.AsArray().Single(x => x!["definitionName"]!.GetValue<string>() == EquipmentRewardCases.Target)!;
            Require(item["state"]!["equipmentProgression"]!["level"]!.GetValue<int>() == 0
                && item["state"]!["equipmentProgression"]!["growthBandId"]!.GetValue<string>() == "FirstSlice"
                && item["location"]!["containerType"]!.GetValue<string>() == "Inventory", "Actual HTTP worker/snapshot lost explicit +0.");
            JsonObject different = payload.DeepClone().AsObject(); different["itemRewards"]![0]!["equipmentProgression"]!["level"] = 1;
            // Settlement-complete credentials retain only scoped snapshot/audit
            // access. Keep this existing boundary; repository tests cover replay.
            await Status(client, route + "/settle-rewards", different, 403);
            await Status(client, route + "/settle-rewards", payload, 403);
            client.DefaultRequestHeaders.Authorization = new("Bearer", admin);
            await Status(client, route + "/settle-rewards", payload, 403);
            groups.Add("actual_backend_process_bearer_settlement_worker_and_reward_snapshot_preserve_plus_zero");
            groups.Add("http_12_invalid_growth_shapes_and_player_admin_post_settlement_mutation_denied");
        }
        catch (Exception error)
        {
            throw new InvalidOperationException("Actual backend HTTP case failed: " + error.Message + "\n" + string.Join("\n", tail), error);
        }
        finally
        {
            if (!process.HasExited) process.Kill(entireProcessTree: true);
            await process.WaitForExitAsync();
        }
    }

    private static async Task<JsonObject> Post(HttpClient client, string route, object payload, int expected)
    {
        using HttpResponseMessage response = await client.PostAsJsonAsync(route, payload);
        Require((int)response.StatusCode == expected, $"Expected {expected}, got {(int)response.StatusCode} at {route}");
        return (await response.Content.ReadFromJsonAsync<JsonObject>())!;
    }
    private static async Task Status(HttpClient client, string route, object payload, params int[] expected)
    {
        using HttpResponseMessage response = await client.PostAsJsonAsync(route, payload);
        Require(expected.Contains((int)response.StatusCode), $"Unexpected {(int)response.StatusCode} at {route}; expected {string.Join('/', expected)}");
    }
    private static void Require(bool condition, string message) => EquipmentRewardCases.Require(condition, message);
}
