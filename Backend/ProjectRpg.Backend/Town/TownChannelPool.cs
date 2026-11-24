using System.Diagnostics;
using System.Net;

namespace ProjectRpg.Backend.Town;

internal sealed class TownOptions
{
    public string Executable { get; set; } = "";
    public string Project { get; set; } = "";
    public string Address { get; set; } = "127.0.0.1";
    public int FirstPort { get; set; } = 17840;
    public int Capacity { get; set; } = 16;
    public int MaxProcesses { get; set; } = 4;
    public int IdleSeconds { get; set; } = 120;
    public bool DevelopmentNoSteam { get; set; }
}

internal sealed record TownAdmission(int Channel, string Address, Guid Ticket);

// One pool owns one host's port range. Both the game server and the pool run as
// the same OS account. Renaming pending to active competes atomically with expiry.
internal sealed class TownChannelPool : BackgroundService
{
    private sealed class Worker(int port, int number, string world, string directory, Process process)
    {
        public readonly int Port = port;
        public readonly int Number = number;
        public readonly string World = world;
        public readonly string Directory = directory;
        public readonly Process Process = process;
        public readonly Dictionary<Guid, Guid> Tickets = [];
        public readonly DateTime Started = DateTime.UtcNow;
        public DateTime LastOccupied = DateTime.UtcNow;
        public bool Ready;
        public DateTime? Closing;
    }

    private readonly object gate = new();
    private readonly List<Worker> workers = [];
    private readonly TownOptions options;
    private readonly ILogger<TownChannelPool> log;
    private bool stopping;

    public TownChannelPool(IConfiguration configuration, IHostEnvironment environment, ILogger<TownChannelPool> log)
    {
        this.log = log;
        options = configuration.GetSection("TownChannels").Get<TownOptions>() ?? new();
        if (Enabled && (!Path.IsPathFullyQualified(options.Executable) || !File.Exists(options.Executable)
            || (options.Project.Length > 0 && (!Path.IsPathFullyQualified(options.Project) || !File.Exists(options.Project)))
            || !IPAddress.TryParse(options.Address, out var ip) || ip.AddressFamily != System.Net.Sockets.AddressFamily.InterNetwork
            || options.MaxProcesses is < 1 or > 128 || options.Capacity is < 1 or > 1024
            || options.FirstPort < 1024 || options.FirstPort > 65536 - options.MaxProcesses || options.IdleSeconds < 1
            || (options.DevelopmentNoSteam && !environment.IsDevelopment())))
            throw new InvalidOperationException("Invalid TownChannels configuration.");
    }

    public bool Enabled => options.Executable.Length > 0;

    public async Task<IResult> AdmitAsync(Guid character, string steamId, string name, string world, CancellationToken ct)
    {
        Worker worker;
        Guid ticket;
        lock (gate)
        {
            if (!Enabled || stopping) return Results.StatusCode(503);
            var existing = workers.Find(w => w.Tickets.ContainsKey(character));
            if (existing is { Ready: true })
            {
                ReapSeat(existing, character, existing.Tickets[character], DateTime.UtcNow);
                if (!existing.Tickets.ContainsKey(character)) existing = null;
            }
            if (existing != null)
            {
                worker = existing;
                ticket = worker.Tickets[character];
                if (File.Exists(Seat(worker, ticket, "active"))) return Results.Conflict();
            }
            else
            {
                var available = workers.Where(w => w.World == world && w.Closing == null && w.Tickets.Count < options.Capacity)
                    .OrderBy(w => w.Number).FirstOrDefault();
                if (available == null && workers.Count >= options.MaxProcesses)
                {
                    var idle = workers.FirstOrDefault(w => w.World != world && w.Ready && w.Closing == null && w.Tickets.Count == 0 &&
                        (DateTime.UtcNow - w.LastOccupied).TotalSeconds >= options.IdleSeconds);
                    if (idle != null) BeginClose(idle);
                    return Results.StatusCode(503);
                }
                worker = available ?? Launch(world);
                ticket = Guid.NewGuid();
                worker.Tickets.Add(character, ticket);
                try { File.WriteAllText(Seat(worker, ticket, "pending"), $"{steamId}\n{name}"); }
                catch { worker.Tickets.Remove(character); throw; }
                worker.LastOccupied = DateTime.UtcNow;
                if (worker.Tickets.Count == options.Capacity && workers.Count < options.MaxProcesses &&
                    !workers.Any(w => w.World == world && w.Closing == null && w.Tickets.Count < options.Capacity))
                {
                    // Keep the accepted seat even if starting the spare fails.
                    try { Launch(world); }
                    catch (Exception e) { log.LogWarning(e, "Could not start the next town channel"); }
                }
            }
        }
        while (true)
        {
            lock (gate)
            {
                if (stopping || !workers.Contains(worker) || worker.Process.HasExited ||
                    !worker.Tickets.TryGetValue(character, out var current) || current != ticket)
                    return Results.StatusCode(503);
                if (worker.Ready)
                    return Results.Ok(new TownAdmission(worker.Number, $"{options.Address}:{worker.Port}", ticket));
            }
            await Task.Delay(250, ct);
        }
    }

    private static string Seat(Worker worker, Guid ticket, string state) =>
        Path.Combine(worker.Directory, $"{ticket:N}.{state}");

    private Worker Launch(string world)
    {
        int port = Enumerable.Range(options.FirstPort, options.MaxProcesses).First(p => workers.All(w => w.Port != p));
        int number = Enumerable.Range(1, options.MaxProcesses).First(n => workers.All(w => w.World != world || w.Number != n));
        string dir = Directory.CreateTempSubdirectory("ProjectRpg-Town-").FullName;
        try
        {
            File.WriteAllText(Path.Combine(dir, "owner"), "");
            var start = new ProcessStartInfo(options.Executable) { UseShellExecute = false, CreateNoWindow = true };
            start.Environment.Remove("PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN");
            start.Environment.Remove("PROJECT_RPG_GAME_SERVER_ID");
            start.Environment.Remove("PROJECT_RPG_DUNGEON_SESSION_ID");
            start.Environment["PROJECT_RPG_TOWN_DIRECTORY"] = dir;
            if (options.Project.Length > 0) start.ArgumentList.Add(options.Project);
            start.ArgumentList.Add($"/Game/Maps/RegionTravel/L_Town?MaxPlayers={options.Capacity}");
            foreach (var argument in new[] { "-server", "-unattended", "-NoLiveCoding", "-nosound", $"-port={port}",
                "-RPGManagedTown", $"-RPGRegionChannel={number}" })
                start.ArgumentList.Add(argument);
            if (options.DevelopmentNoSteam) start.ArgumentList.Add("-nosteam");
            var process = Process.Start(start) ?? throw new InvalidOperationException("Town server failed to launch.");
            var worker = new Worker(port, number, world, dir, process);
            workers.Add(worker);
            return worker;
        }
        catch { Directory.Delete(dir, true); throw; }
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(1));
        try
        {
            while (await timer.WaitForNextTickAsync(stoppingToken))
                lock (gate)
                    foreach (var worker in workers.ToArray())
                        try { Maintain(worker); }
                        catch (Exception e) { log.LogError(e, "Town channel maintenance failed on port {Port}", worker.Port); }
        }
        finally
        {
            Worker[] remaining;
            lock (gate)
            {
                stopping = true;
                remaining = workers.ToArray();
                foreach (var worker in remaining) BeginClose(worker);
            }
            await Task.WhenAll(remaining.Select(async worker =>
            {
                using var grace = new CancellationTokenSource(TimeSpan.FromSeconds(12));
                try { await worker.Process.WaitForExitAsync(grace.Token); }
                catch (OperationCanceledException) { }
                lock (gate) StopWorker(worker);
            }));
        }
    }

    private void Maintain(Worker worker)
    {
        var now = DateTime.UtcNow;
        if (worker.Process.HasExited || now - worker.Closing > TimeSpan.FromSeconds(12) ||
            (!worker.Ready && now - worker.Started > TimeSpan.FromSeconds(120)))
        {
            StopWorker(worker);
            return;
        }
        if (worker.Closing != null) return;
        File.SetLastWriteTimeUtc(Path.Combine(worker.Directory, "owner"), now);
        if (!worker.Ready)
        {
            if (!File.Exists(Path.Combine(worker.Directory, "ready"))) return;
            foreach (var ticket in worker.Tickets.Values)
                File.SetLastWriteTimeUtc(Seat(worker, ticket, "pending"), now);
            worker.Ready = true;
            worker.LastOccupied = now;
        }
        foreach (var (character, ticket) in worker.Tickets.ToArray())
            ReapSeat(worker, character, ticket, now);
        if (worker.Tickets.Count > 0) worker.LastOccupied = now;
        else if ((worker.Number != 1 || workers.Any(w => w.World != worker.World)) &&
            now - worker.LastOccupied >= TimeSpan.FromSeconds(options.IdleSeconds)) BeginClose(worker);
    }

    private static void ReapSeat(Worker worker, Guid character, Guid ticket, DateTime now)
    {
        var pending = Seat(worker, ticket, "pending");
        if (File.Exists(pending))
        {
            if (now - File.GetLastWriteTimeUtc(pending) < TimeSpan.FromSeconds(90)) return;
            File.Delete(pending);
        }
        if (!File.Exists(Seat(worker, ticket, "active"))) worker.Tickets.Remove(character);
    }

    private static void BeginClose(Worker worker)
    {
        // Removing the manager heartbeat asks the game to exit on its next tick.
        File.Delete(Path.Combine(worker.Directory, "owner"));
        worker.Closing ??= DateTime.UtcNow;
    }

    private void StopWorker(Worker worker)
    {
        if (!worker.Process.HasExited)
        {
            worker.Process.Kill();
            if (!worker.Process.WaitForExit(5000)) return;
        }
        if (Directory.Exists(worker.Directory)) Directory.Delete(worker.Directory, true);
        workers.Remove(worker);
        worker.Process.Dispose();
    }
}
