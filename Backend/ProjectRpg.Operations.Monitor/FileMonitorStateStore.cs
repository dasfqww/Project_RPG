using System.Text.Json;
using ProjectRpg.Operations.Core.Monitoring;

namespace ProjectRpg.Operations.Monitor;

public sealed class FileMonitorStateStore : IMonitorStateStore, IDisposable
{
    private readonly string root, path, environment, service;
    private readonly FileStream ownership;
    public FileMonitorStateStore(string root,string environment,string service)
    {
        if (!Path.IsPathFullyQualified(root)) throw new ArgumentException("Absolute private state directory required.");
        this.root=Path.GetFullPath(root); this.environment=environment; this.service=service;
        RejectLinks(this.root); Directory.CreateDirectory(this.root); RejectLinks(this.root);
        if (!OperatingSystem.IsWindows()) File.SetUnixFileMode(this.root,UnixFileMode.UserRead|UnixFileMode.UserWrite|UnixFileMode.UserExecute);
        path=Path.Combine(this.root,"state.json"); RejectLinks(path);
        string lockPath=Path.Combine(this.root,"monitor.lock"); RejectLinks(lockPath);
        ownership=new FileStream(lockPath,FileMode.OpenOrCreate,FileAccess.ReadWrite,FileShare.None);
    }
    public async Task<MonitorState> LoadAsync(CancellationToken ct)
    {
        RejectLinks(path);
        if (!File.Exists(path)) return new() { Environment=environment,Service=service };
        await using FileStream stream=new(path,FileMode.Open,FileAccess.Read,FileShare.Read);
        if(stream.Length>64*1024)throw new InvalidDataException("Monitor state exceeded its bounded contract.");
        var state=await JsonSerializer.DeserializeAsync<MonitorState>(stream,MonitoringJson.Options,ct)
            ?? throw new InvalidDataException("Invalid monitor state.");
        if(state.Version!=1||state.Environment!=environment||state.Service!=service||state.Incidents is null||state.Outbox is null
            ||state.Incidents.Count>MonitorPolicy.IncidentKeys.Count||state.Incidents.Keys.Any(k=>!MonitorPolicy.IncidentKeys.Contains(k))
            ||state.Outbox.Count>MonitorPolicy.IncidentKeys.Count||state.Outbox.Any(p=>p is null||p.Event is null||p.Event.EventId==Guid.Empty
                ||p.Event.Environment!=environment||p.Event.Service!=service||!MonitorPolicy.IncidentKeys.Contains(p.Event.IncidentKey)
                ||p.Attempts is <0 or >3||p.Event.ReferenceIds is null||p.Event.ReferenceIds.Count!=0))
            throw new InvalidDataException("Invalid or foreign monitor state.");
        return state;
    }
    public async Task SaveAsync(MonitorState state,CancellationToken ct)
    {
        RejectLinks(path);
        byte[] bytes=JsonSerializer.SerializeToUtf8Bytes(state,MonitoringJson.Options);
        if(bytes.Length>64*1024)throw new InvalidDataException("Monitor state exceeded its bounded contract.");
        string temporary=Path.Combine(root,"state."+Guid.NewGuid().ToString("N")+".tmp");
        try
        {
            await using(var stream=new FileStream(temporary,FileMode.CreateNew,FileAccess.Write,FileShare.None))
            {
                if(!OperatingSystem.IsWindows()) File.SetUnixFileMode(temporary,UnixFileMode.UserRead|UnixFileMode.UserWrite);
                await stream.WriteAsync(bytes,ct); await stream.FlushAsync(ct); stream.Flush(flushToDisk:true);
            }
            RejectLinks(path); File.Move(temporary,path,overwrite:true);
        }
        finally { if(File.Exists(temporary)) File.Delete(temporary); }
    }
    private static void RejectLinks(string path)
    {
        for(string? current=Path.GetFullPath(path);current is not null;current=Path.GetDirectoryName(current))
            if((File.Exists(current)||Directory.Exists(current))&&(File.GetAttributes(current)&FileAttributes.ReparsePoint)!=0)
                throw new IOException("Monitor state cannot traverse reparse points.");
    }
    public void Dispose()=>ownership.Dispose();
}
