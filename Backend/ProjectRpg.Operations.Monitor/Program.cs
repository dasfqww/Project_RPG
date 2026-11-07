using ProjectRpg.Operations.Core.Monitoring;
using ProjectRpg.Operations.Monitor;

if(args.Length>1 || args.Length==1 && args[0]!="--once") { Console.Error.WriteLine("Usage: ProjectRpg.Operations.Monitor [--once]"); return 2; }
using var stopping=new CancellationTokenSource();
Console.CancelKeyPress+=(_,eventArgs)=>{eventArgs.Cancel=true;stopping.Cancel();};
try
{
    var configuration=MonitorConfiguration.FromEnvironment();
    using var storage=new FileMonitorStateStore(configuration.StateDirectory,configuration.Environment,configuration.Service);
    using var handler=new HttpClientHandler { AllowAutoRedirect=false };
    using var http=new HttpClient(handler) { Timeout=Timeout.InfiniteTimeSpan };
    IMonitorProbe probe=new HttpOperationsProbe(http,configuration,TimeProvider.System);
    if(configuration.BackupStatusPath.Length>0) probe=new BackupAwareProbe(probe,new FileBackupStatusReader(configuration.BackupStatusPath));
    var cycle=new MonitorCycle(probe,storage,
        new HttpWebhookDelivery(http,configuration),configuration.Environment,configuration.Service,configuration.LogBudgetBytes);
    using var timer=new PeriodicTimer(TimeSpan.FromSeconds(30));
    do
    {
        await cycle.ExecuteAsync(stopping.Token);
        if(args.Length==1)break;
    } while(await timer.WaitForNextTickAsync(stopping.Token));
    return 0;
}
catch(OperationCanceledException) when(stopping.IsCancellationRequested) { return 0; }
catch(Exception) { Console.Error.WriteLine("Operations monitor stopped: configuration, state or runtime failure. No secret values are logged."); return 1; }
