using ProjectRpg.Operations.Core.Diagnostics;

namespace ProjectRpg.Backend.Operations.Web;

public enum LogSpanRelation { ParentOnPage, ParentOutsidePage, NoParentRecorded, InvalidContext, AmbiguousContext }
public sealed record LogSpanRow(string RowId, LogSpanRelation Relation, string? ParentRowId = null, int ParentEventCount = 0);

/// <summary>Links only explicit span identities within one loaded result page. No I/O, inferred parents or cross-page cache.</summary>
public static class OperationsSpanNavigation
{
    private readonly record struct Key(string Environment, string Trace, string Span);
    private sealed record Group(int FirstRow, int Count, string? Parent, bool Ambiguous);

    public static IReadOnlyList<LogSpanRow> Build(IReadOnlyList<LogEvent> events)
    {
        bool Context(LogEvent e) => !string.IsNullOrEmpty(e.Environment) && Id(e.TraceId,32) && Id(e.SpanId,16);
        Key Identity(LogEvent e) => new(e.Environment,e.TraceId!,e.SpanId!);
        var groups=events.Select((e,i)=>(Event:e,Index:i)).Where(x=>Context(x.Event)).GroupBy(x=>Identity(x.Event))
            .ToDictionary(g=>g.Key,g=>new Group(g.First().Index,g.Count(),g.First().Event.ParentSpanId,
                g.Select(x=>(x.Event.Service,x.Event.InstanceId,x.Event.BuildVersion,x.Event.ParentSpanId)).Distinct().Skip(1).Any()));
        bool Cyclic(Key key)
        {
            HashSet<Key> visited=[];
            while(groups.TryGetValue(key,out var group) && !group.Ambiguous && Id(group.Parent,16))
            {
                if(!visited.Add(key))return true;
                key=key with {Span=group.Parent!};
            }
            return false;
        }
        var rows=new LogSpanRow[events.Count];
        for(int i=0;i<events.Count;i++)
        {
            var entry=events[i];string rowId="log-event-"+i;
            if(!Context(entry) || entry.ParentSpanId is not null && !Id(entry.ParentSpanId,16))
            { rows[i]=new(rowId,LogSpanRelation.InvalidContext);continue; }
            var key=Identity(entry);
            if(groups[key].Ambiguous || Cyclic(key))
            { rows[i]=new(rowId,LogSpanRelation.AmbiguousContext);continue; }
            if(entry.ParentSpanId is null)
            { rows[i]=new(rowId,LogSpanRelation.NoParentRecorded);continue; }
            if(!groups.TryGetValue(key with {Span=entry.ParentSpanId},out var parent))
            { rows[i]=new(rowId,LogSpanRelation.ParentOutsidePage);continue; }
            rows[i]=parent.Ambiguous ? new(rowId,LogSpanRelation.AmbiguousContext)
                : new(rowId,LogSpanRelation.ParentOnPage,"log-event-"+parent.FirstRow,parent.Count);
        }
        return Array.AsReadOnly(rows);
    }

    private static bool Id(string? value,int length) => value is not null && value.Length==length
        && value.Any(c=>c!='0') && value.All(c=>c is >= '0' and <= '9' or >= 'a' and <= 'f');
}
