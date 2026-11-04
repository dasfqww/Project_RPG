using System.Text;
using System.Text.Json;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Diagnostics;

internal static class LogReaderTests
{
    internal static async Task RunAsync(string root)
    {
        Directory.CreateDirectory(root);
        MutableClock clock=new();
        OperationsOptions options=new() { EnvironmentId="LogTest",LogRoot=root,SourceId="backend" };
        using OperationsLogStore store=new(options,clock);
        LocalJsonlOperationLogReader reader=new(store,options);
        LogEvent Event() => new(1,Guid.NewGuid(),"ops.http.completed",clock.Now,"Information","LogTest","Backend","fixture","fixture",
            Guid.NewGuid(),new string('a',32),new string('b',16),null,"Test",1,"github:1",null,null,null,null,null,null,null,"Completed",null,200,"Operations");
        string Line(LogEvent value) => JsonSerializer.Serialize(new {Properties=new {OpsEvent=JsonSerializer.SerializeToElement(value,OperationsHttp.Json)}})+"\n";
        LogEvent first=Event(),second=Event();
        string path=Path.Combine(root,"ops-20990101.jsonl");
        await File.WriteAllTextAsync(path,Line(first)+"{broken}\n"+new string('x',40000)+"\n"+Line(second)+"{unfinished",new UTF8Encoding(false));
        OperationLogQuery query=new(clock.Now.AddSeconds(-1),clock.Now.AddSeconds(1),"backend",
            new Dictionary<string,string>{{"eventName","ops.http.completed"}},1);
        var page=await reader.SearchAsync(query,default);
        Check.That(page.IsSuccess && page.Value.Events.Single().EventId==second.EventId && page.Value.Next is not null,"Reverse JSONL skips incomplete tail and pages newest complete event.");
        // Snapshot EOF prevents newly appended lines from leaking into an existing page chain.
        await File.AppendAllTextAsync(path,"}\n"+Line(Event()));
        var next=await reader.SearchAsync(query with {After=page.Value.Next,Limit=10},default);
        Check.That(next.IsSuccess && next.Value.Events.Single().EventId==first.EventId && next.Value.Coverage==LogCoverage.Partial
            && next.Value.TruncationReason==LogTruncation.CorruptRecord,"Corrupt/oversized records are bounded and observable; append does not change the snapshot.");
        var wrongSource=await reader.SearchAsync(query with {SourceId="../elsewhere"},default);
        Check.That(wrongSource.Error==Failures.InvalidQuery,"Source IDs cannot select a filesystem path.");
        var traversal=await reader.SearchAsync(query with {After=new([new("../ops-secret.jsonl",1,1,0)],0)},default);
        Check.That(traversal.Error==Failures.InvalidCursor,"Cursor cannot traverse the log root.");
        File.Delete(path);
        Check.That((await reader.SearchAsync(query with {After=page.Value.Next},default)).Error==Failures.CursorExpired,"Removed rotated source expires the cursor.");
        var afterLoss=await reader.SearchAsync(query with {FromUtc=clock.Now},default);
        Check.That(afterLoss.IsSuccess && afterLoss.Value.Coverage==LogCoverage.Partial,"Source loss never becomes a false complete zero-match answer.");
        await store.StopAsync(default);
        Console.WriteLine("PASS JSONL: open writer, incomplete/corrupt/oversized lines, bounded pages, frozen EOF, traversal and retention loss.");
    }
}
