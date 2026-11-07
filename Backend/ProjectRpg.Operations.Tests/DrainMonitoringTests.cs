using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Monitoring;
using ProjectRpg.Operations.Core.ServiceControl;
using ProjectRpg.Operations.Monitor;

internal static class DrainMonitoringTests
{
    private const string Key = "service_drain_long";
    internal static async Task RunAsync()
    {
        Policy();
        foreach (string? pg in new[] { (string?)null, Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_POSTGRES") }.Distinct())
            await HostAsync(pg);
        await ReadFailureAsync();
        Console.WriteLine("PASS Drain monitoring: 30-minute boundary, sample gaps/Unknown, notice edits, durable restart/retry, reminder/recovery, real HTTPS Memory/PG mode privacy and failed reads.");
    }

    private static void Policy()
    {
        var start = DateTimeOffset.Parse("2026-11-08T00:00:00Z");
        var state = new MonitorState { Environment = "Test", Service = "backend" };
        void At(int seconds, string? mode = "Drain", string boot = "11111111111111111111111111111111")
        {
            var now = start.AddSeconds(seconds);
            var status = mode is null ? null : MonitoringPolicyTests.Healthy(now) with { ServiceControl = new(mode), BootId = boot };
            MonitorPolicy.Evaluate(state, new(now, status), 1024);
        }
        At(0);
        for (int seconds = 120; seconds <= 1800; seconds += 120) At(seconds);
        Check.That(!state.Incidents[Key].Active && state.Outbox.Count == 0, "Exactly thirty minutes is not over the Drain threshold.");
        At(1801, boot: "22222222222222222222222222222222");
        var fired = state.Outbox.Single(p => p.Event.IncidentKey == Key).Event;
        Check.That(fired.State == "Firing" && fired.FirstSeenAtUtc == start, "First Drain observation anchors the event, independent of backend boot changes.");
        At(1830); Check.That(state.Outbox.Single().Event.EventId == fired.EventId, "Drain samples do not flood the webhook.");
        for (int seconds = 1950; seconds <= 3510; seconds += 120) At(seconds);
        At(3601);
        Check.That(state.Outbox.Single().Event.EventId != fired.EventId, "Persistent Drain receives a thirty-minute reminder.");
        At(3630, "Open"); At(3660, null); At(3690, "Open");
        Check.That(state.Incidents[Key].Active, "Unknown interrupts Open recovery and cannot resolve an active Drain incident.");
        At(3720, "Open");
        Check.That(!state.Incidents[Key].Active && state.Outbox.Single().Event.State == "Resolved", "Two consecutive observed Open samples resolve Drain.");
        state.Outbox.Clear();
        At(3750); At(5400);
        Check.That(state.DrainObservedSinceUtc == start.AddSeconds(5400) && !state.Incidents[Key].Active, "Unobserved gaps over two minutes restart the waiting window.");
        At(5430, null); At(5460);
        Check.That(state.DrainObservedSinceUtc == start.AddSeconds(5460), "Collection failure restarts the unconfirmed waiting period.");
        At(5500, "Open"); At(5530);
        Check.That(state.DrainObservedSinceUtc == start.AddSeconds(5530), "A short Drain after Open starts a new duration window.");
        At(5500);
        Check.That(state.DrainObservedSinceUtc == start.AddSeconds(5500), "A backwards clock cannot fabricate an elapsed duration.");
        var healthy = MonitoringPolicyTests.Healthy(start);
        Check.That(!MonitorPolicy.Valid(healthy with { ServiceControl = new("Closed") }, start)
            && !MonitorPolicy.Valid(healthy with { ServiceControl = null! }, start), "Unknown or missing service mode is not healthy Open.");
        string old = JsonSerializer.Serialize(healthy, MonitoringJson.Options);
        using var doc = JsonDocument.Parse(old);
        var fields = doc.RootElement.EnumerateObject().Where(p => p.Name != "serviceControl").ToDictionary(p => p.Name, p => p.Value.Clone());
        bool rejected = false;
        try { JsonSerializer.Deserialize<RuntimeStatus>(JsonSerializer.Serialize(fields), MonitoringJson.Options); } catch (JsonException) { rejected = true; }
        Check.That(rejected, "An older status contract missing mode fails closed rather than resolving Drain.");
    }

    private static async Task HostAsync(string? pg)
    {
        string root = Path.Combine(Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")!, pg is null ? "drain-memory" : "drain-postgres");
        var events = new List<AlertEvent>();
        bool reject = true;
        await using var host = await HostFixture.StartAsync(root, pg, clock: new MutableClock { Now = DateTimeOffset.UtcNow.AddHours(2) },
            monitoring: true, customizeRoutes: app =>
            app.MapPost("/test-drain-webhook", async (HttpContext c) =>
            {
                Check.That(c.Request.Headers.Authorization == "Bearer fixture-drain-webhook", "Drain delivery uses only webhook credentials.");
                events.Add((await c.Request.ReadFromJsonAsync<AlertEvent>(MonitoringJson.Options, c.RequestAborted))!);
                return Results.StatusCode(reject ? 503 : 204);
            }));
        var reader = host.App.Services.GetRequiredService<IServiceControlReader>();
        var writer = host.App.Services.GetRequiredService<IServiceControlCommitter>();
        async Task Change(string mode, string notice)
        {
            var current = await reader.ReadAsync(default);
            var intent = new ServiceIntent(Guid.NewGuid(), current.Value.Revision, mode, notice, null, null, "private maintenance reason fixture");
            var result = await writer.CommitAsync(new(intent, "private-operator", Guid.NewGuid(), "private-trace"), default);
            Check.That(result.IsSuccess, "Fixture commits real atomic service state.");
        }
        await Change("Open", "private-notice");
        var settings = new MonitorConfiguration("OpsTest", "backend", new(host.Origin), new('m', 48),
            new(host.Origin + "/test-drain-webhook"), "fixture-drain-webhook", Path.Combine(root, "drain-state"), long.MaxValue);
        var probe = new HttpOperationsProbe(host.Client, settings, host.Clock);
        var delivery = new HttpWebhookDelivery(host.Client, settings);
        var open = await probe.ObserveAsync(default);
        Check.That(open.Status?.ServiceControl.Mode == "Open", "Real machine endpoint reads Open from the configured store.");
        Check.That(MonitorPolicy.Valid(open.Status, host.Clock.Now), "Fixture clock is later than every persisted settlement timestamp.");
        DateTimeOffset firstDrain = host.Clock.Now;
        await Change("Drain", "private-notice");
        using (var state = new FileMonitorStateStore(settings.StateDirectory, settings.Environment, settings.Service))
        {
            var cycle = new MonitorCycle(probe, state, delivery, settings.Environment, settings.Service, settings.LogBudgetBytes);
            await cycle.ExecuteAsync(default);
            for (int i = 1; i <= 7; i++) { host.Clock.Now = firstDrain.AddMinutes(i * 2); await cycle.ExecuteAsync(default); }
            Check.That(!events.Any(p => p.IncidentKey == Key), "Short Drain is not an incident.");
        }
        // Restart before the threshold. Notice-only writes must not erase the observed duration.
        await Change("Drain", "different-private-notice");
        using (var restored = new FileMonitorStateStore(settings.StateDirectory, settings.Environment, settings.Service))
        {
            Check.That((await restored.LoadAsync(default)).DrainObservedSinceUtc == firstDrain, "Pre-alert timer survives a monitor restart.");
            var cycle = new MonitorCycle(probe, restored, delivery, settings.Environment, settings.Service, settings.LogBudgetBytes);
            for (int i = 8; i <= 15; i++) { host.Clock.Now = firstDrain.AddMinutes(i * 2); await cycle.ExecuteAsync(default); }
            Check.That(!events.Any(p => p.IncidentKey == Key), "Thirty-minute boundary remains quiet after notice edit and restart.");
            host.Clock.Now = firstDrain.AddMinutes(30).AddSeconds(30); await cycle.ExecuteAsync(default);
            var drain = events.Single(p => p.IncidentKey == Key);
            Check.That(drain.FirstSeenAtUtc == firstDrain && drain.State == "Firing", "Real HTTPS observation fires after thirty minutes.");
            Check.That((await restored.LoadAsync(default)).Outbox.Any(p => p.Event.EventId == drain.EventId && p.Attempts == 1), "Failed Drain webhook is persisted for retry.");
        }
        using (var restarted = new FileMonitorStateStore(settings.StateDirectory, settings.Environment, settings.Service))
        {
            var cycle = new MonitorCycle(probe, restarted, delivery, settings.Environment, settings.Service, settings.LogBudgetBytes);
            reject = false; host.Clock.Now = host.Clock.Now.AddMinutes(1); await cycle.ExecuteAsync(default);
            var drains = events.Where(p => p.IncidentKey == Key).ToArray();
            Check.That(drains.Length == 2 && drains[0].EventId == drains[1].EventId, "Restart retries the exact Drain event ID over real HTTPS.");
            await Change("Open", "private-notice");
            host.Clock.Now = host.Clock.Now.AddSeconds(30); await cycle.ExecuteAsync(default);
            Check.That((await restarted.LoadAsync(default)).Incidents[Key].Active, "One real Open sample is not enough to resolve Drain.");
            host.Clock.Now = host.Clock.Now.AddSeconds(30); await cycle.ExecuteAsync(default);
            Check.That(events.Last(p => p.IncidentKey == Key).State == "Resolved", "Confirmed real Open sends a recovery webhook.");
        }
        string? before = pg is null ? null : await HostTests.FingerprintAsync(host);
        var final = await probe.ObserveAsync(default);
        if (before is not null) Check.That(before == await HostTests.FingerprintAsync(host), "Mode collection changes no gameplay state.");
        string json = JsonSerializer.Serialize(final.Status, MonitoringJson.Options);
        Check.That(!json.Contains("private-") && !json.Contains("revision") && !json.Contains("actionRequestId"), "Machine payload contains mode only, without notice, operator, audit or revision.");
        using var parsed = JsonDocument.Parse(json);
        Check.That(parsed.RootElement.GetProperty("serviceControl").EnumerateObject().Single().Name == "mode", "Service health exposes exactly one field.");
        // Additive fields preserve pre-upgrade v1 state files without replacing pending events.
        string legacyRoot = Path.Combine(root, "legacy-state"); Directory.CreateDirectory(legacyRoot);
        var legacyState = new MonitorState { Environment = "OpsTest", Service = "backend" };
        legacyState.Incidents.Add("backend_unavailable", new() { Active = true, FirstSeenAtUtc = host.Clock.Now });
        var oldEvent = new AlertEvent(Guid.NewGuid(), "OpsTest", "backend", "backend_unavailable", "Warning", "Firing", host.Clock.Now, host.Clock.Now, "Fixed summary.", []);
        legacyState.Outbox.Add(new() { Event = oldEvent, Attempts = 1, NextAttemptAtUtc = host.Clock.Now.AddMinutes(1) });
        using var legacyDoc = JsonDocument.Parse(JsonSerializer.Serialize(legacyState, MonitoringJson.Options));
        var legacyFields = legacyDoc.RootElement.EnumerateObject().Where(p => p.Name is not "drainObservedSinceUtc" and not "drainLastObservedAtUtc")
            .ToDictionary(p => p.Name, p => p.Value.Clone());
        await File.WriteAllTextAsync(Path.Combine(legacyRoot, "state.json"), JsonSerializer.Serialize(legacyFields));
        using var legacy = new FileMonitorStateStore(legacyRoot, "OpsTest", "backend");
        var loadedLegacy = await legacy.LoadAsync(default);
        Check.That(loadedLegacy.DrainObservedSinceUtc is null && loadedLegacy.Incidents["backend_unavailable"].Active
            && loadedLegacy.Outbox.Single().Event.EventId == oldEvent.EventId && loadedLegacy.Outbox[0].Attempts == 1,
            "Existing v1 monitor state upgrades without resetting unrelated incidents or delivery attempts.");
    }

    private static async Task ReadFailureAsync()
    {
        string root = Path.Combine(Environment.GetEnvironmentVariable("PROJECT_RPG_OPS_TEST_ROOT")!, "drain-read-failed");
        await using var host = await HostFixture.StartAsync(root, monitoring: true,
            customize: s => s.AddSingleton<IServiceControlReader, UnavailableService>());
        using var request = new HttpRequestMessage(HttpMethod.Get, "/internal/operations/status");
        request.Headers.Authorization = new("Bearer", new string('m', 48));
        using var response = await host.Client.SendAsync(request);
        string body = await response.Content.ReadAsStringAsync();
        Check.That(response.StatusCode == HttpStatusCode.ServiceUnavailable && body.Contains("Unknown") && !body.Contains("Open"), "Service storage failure returns Unknown/503, never Open.");
    }

    private sealed class UnavailableService : IServiceControlReader
    {
        public Task<Result<ServiceSnapshot>> ReadAsync(CancellationToken ct) => Task.FromResult(Result<ServiceSnapshot>.Fail(Failures.StorageUnavailable));
        public Task<Result<ServiceReceipt>> FindActionAsync(Guid id, CancellationToken ct) => throw new NotSupportedException();
    }
}
