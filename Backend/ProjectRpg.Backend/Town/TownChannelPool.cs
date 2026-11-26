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
    internal enum Region { Town, Field, Bellmarket }

    private sealed class Worker(int port, int number, string world, Region region, string directory, Process process)
    {
        public readonly int Port = port;
        public readonly int Number = number;
        public readonly string World = world;
        public readonly Region Region = region;
        public readonly string Directory = directory;
        public readonly Process Process = process;
        public readonly Dictionary<Guid, Guid> Tickets = [];
        public readonly Dictionary<Guid, Transfer> Transfers = [];
        public readonly DateTime Started = DateTime.UtcNow;
        public DateTime LastOccupied = DateTime.UtcNow;
        public bool Ready;
        public DateTime? Closing;
    }

    private sealed record Transfer(Guid Request, Guid Character, Worker Destination, Guid Ticket)
    {
        public bool Published { get; set; }
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

    public async Task<IResult> AdmitAsync(Guid character, string steamId, string name, string world, CancellationToken ct, Region region = Region.Town)
    {
        Worker worker;
        Guid ticket;
        lock (gate)
        {
            if (!Enabled || stopping) return Results.StatusCode(503);
            Worker? existing = null;
            foreach (var candidate in workers.Where(w => w.Tickets.ContainsKey(character)))
            {
                var current = candidate.Tickets[character];
                if (candidate.Ready) ReapSeat(candidate, character, current, DateTime.UtcNow);
                if (!candidate.Tickets.ContainsKey(character)) continue;
                if (candidate.Region != region || candidate.World != world || File.Exists(Seat(candidate, current, "active")))
                    return Results.Conflict();
                existing = candidate;
            }
            if (existing != null)
            {
                worker = existing;
                ticket = worker.Tickets[character];
            }
            else
            {
                var available = FindAvailable(world, region);
                if (available == null && !CanLaunch())
                {
                    var idle = workers.FirstOrDefault(w => (w.World != world || w.Region != region) && w.Ready && w.Closing == null &&
                        w.Tickets.Count == 0 && (DateTime.UtcNow - w.LastOccupied).TotalSeconds >= options.IdleSeconds);
                    if (idle != null) BeginClose(idle);
                    return Results.StatusCode(503);
                }
                worker = available ?? Launch(world, region);
                ticket = Reserve(worker, character, $"{steamId}\n{name}");
                if (worker.Tickets.Count == options.Capacity && CanLaunch() &&
                    FindAvailable(world, region) == null)
                {
                    // Keep the accepted seat even if starting the spare fails.
                    try { Launch(world, region); }
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

    private Worker? FindAvailable(string world, Region region) =>
        workers.Where(w => w.World == world && w.Region == region && w.Closing == null && !w.Process.HasExited &&
            w.Tickets.Count < options.Capacity).OrderBy(w => w.Number).FirstOrDefault();

    private bool CanLaunch() => workers.Count < options.MaxProcesses;

    private static Guid Reserve(Worker worker, Guid character, string identity)
    {
        var ticket = Guid.NewGuid();
        worker.Tickets.Add(character, ticket);
        try { File.WriteAllText(Seat(worker, ticket, "pending"), identity); }
        catch { worker.Tickets.Remove(character); throw; }
        worker.LastOccupied = DateTime.UtcNow;
        return ticket;
    }

    private Worker Launch(string world, Region region)
    {
        int port = Enumerable.Range(options.FirstPort, options.MaxProcesses).First(p => workers.All(w => w.Port != p));
        int number = Enumerable.Range(1, options.MaxProcesses)
            .First(n => workers.All(w => w.World != world || w.Region != region || w.Number != n));
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
            string map = region switch
            {
                Region.Town => "/Game/Maps/RegionTravel/L_Town",
                Region.Field => "/Game/Maps/RegionTravel/L_PortalField",
                Region.Bellmarket => "/Game/Maps/L_Bellmarket",
                _ => throw new ArgumentOutOfRangeException(nameof(region))
            };
            start.ArgumentList.Add($"{map}?MaxPlayers={options.Capacity}");
            foreach (var argument in new[] { "-server", "-unattended", "-NoLiveCoding", "-nosound", $"-port={port}",
                "-RPGManagedTown", $"-RPGRegionChannel={number}" })
                start.ArgumentList.Add(argument);
            if (options.DevelopmentNoSteam) start.ArgumentList.Add("-nosteam");
            var process = Process.Start(start) ?? throw new InvalidOperationException("Town server failed to launch.");
            var worker = new Worker(port, number, world, region, dir, process);
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
                {
                    foreach (var worker in workers.ToArray())
                        try { Maintain(worker); }
                        catch (Exception e) { log.LogError(e, "Town channel maintenance failed on port {Port}", worker.Port); }
                    foreach (var worker in workers.Where(w => w.Ready && w.Closing == null).ToArray())
                        try { MaintainTransfers(worker); }
                        catch (Exception e) { log.LogError(e, "Region handoff failed on port {Port}", worker.Port); }
                }
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
        else if ((worker.Region != Region.Town || worker.Number != 1 || workers.Any(w => w.World != worker.World)) &&
            now - worker.LastOccupied >= TimeSpan.FromSeconds(options.IdleSeconds)) BeginClose(worker);
    }

    private void MaintainTransfers(Worker source)
    {
        foreach (var (character, ticket) in source.Tickets)
        {
            string active = Seat(source, ticket, "active");
            string requestPath = Seat(source, ticket, "travel");
            if (!File.Exists(active) || !File.Exists(requestPath) ||
                !Guid.TryParseExact(File.ReadAllText(requestPath), "N", out var request)) continue;

            source.Transfers.TryGetValue(ticket, out var transfer);
            if (transfer != null && transfer.Request != request)
            {
                CancelPending(transfer);
                source.Transfers.Remove(ticket);
                transfer = null;
            }
            string reply = Seat(source, request, "arrival");
            if (transfer == null)
            {
                if (File.Exists(reply)) continue;
                if (source.Region == Region.Bellmarket)
                {
                    WriteReply(reply, "unavailable");
                    continue;
                }
                var region = source.Region == Region.Town ? Region.Field : Region.Town;
                if (workers.Any(w => w != source && w.Tickets.ContainsKey(character)))
                {
                    WriteReply(reply, "unavailable");
                    continue;
                }
                var destination = FindAvailable(source.World, region);
                if (destination == null && CanLaunch()) destination = Launch(source.World, region);
                if (destination == null)
                {
                    if (workers.Any(w => w.Closing != null)) continue;
                    var idle = workers.FirstOrDefault(w => w != source && w.Ready &&
                        w.Tickets.Count == 0 && (w.World != source.World || w.Region != region));
                    if (idle != null)
                    {
                        BeginClose(idle);
                        continue;
                    }
                    WriteReply(reply, "unavailable");
                    continue;
                }
                string identity = File.ReadAllText(active);
                if (!identity.EndsWith("\nregion", StringComparison.Ordinal)) identity += "\nregion";
                var nextTicket = Reserve(destination, character, identity);
                transfer = new Transfer(request, character, destination, nextTicket);
                source.Transfers.Add(ticket, transfer);
            }
            if (transfer.Published) continue;
            if (!workers.Contains(transfer.Destination) || transfer.Destination.Process.HasExited)
            {
                WriteReply(reply, "unavailable");
                source.Transfers.Remove(ticket);
            }
            else if (transfer.Destination.Ready)
            {
                WriteReply(reply, $"{options.Address}:{transfer.Destination.Port}\n{transfer.Ticket:D}");
                transfer.Published = true;
            }
        }

        foreach (var (ticket, transfer) in source.Transfers.ToArray())
        {
            if (File.Exists(Seat(source, ticket, "active")) && File.Exists(Seat(source, ticket, "travel"))) continue;
            if (!transfer.Published) CancelPending(transfer);
            File.Delete(Seat(source, transfer.Request, "arrival"));
            source.Transfers.Remove(ticket);
        }
    }

    private void CancelPending(Transfer transfer)
    {
        var destination = transfer.Destination;
        if (!workers.Contains(destination)) return;
        File.Delete(Seat(destination, transfer.Ticket, "pending"));
        if (!File.Exists(Seat(destination, transfer.Ticket, "active")) &&
            destination.Tickets.TryGetValue(transfer.Character, out var current) && current == transfer.Ticket)
            destination.Tickets.Remove(transfer.Character);
    }

    private static void WriteReply(string path, string content)
    {
        string pending = path + ".writing";
        File.WriteAllText(pending, content);
        File.Move(pending, path, overwrite: true);
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
        foreach (var transfer in worker.Transfers.Values)
            if (!transfer.Published) CancelPending(transfer);
        worker.Process.Dispose();
    }
}
