using ProjectRpg.Operations.Core.Access;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Diagnostics;
using ProjectRpg.Operations.Core.Evidence;

internal static class EvidenceCoreTests
{
    internal static async Task RunAsync()
    {
        var store=new Probe();
        var now=DateTimeOffset.UtcNow;
        var valid=new EvidenceQuery(store.Target.CharacterId,now.AddDays(-7),now);
        var denied=await new GetCharacterEvidence(new Access(false),store,store).ExecuteAsync(valid with {Limit=999},default);
        Check.That(denied.Error==Failures.Forbidden&&store.Reads==0,"Evidence authorizes before invalid input or storage.");
        var useCase=new GetCharacterEvidence(new Access(true),store,store);
        Check.That(!(await useCase.ExecuteAsync(valid with {FromUtc=now.AddDays(-31)},default)).IsSuccess&&store.Reads==0,"Evidence rejects unbounded periods before storage.");
        var result=await useCase.ExecuteAsync(valid,default);
        Check.That(result.IsSuccess&&result.Value.Items.Count==1&&result.Value.Coverage.Any(c=>c.Source=="ItemReceipt"&&c.Coverage==LogCoverage.Unknown),
            "Unavailable source does not hide confirmed evidence from healthy sources.");
        Check.That(result.Value.Next is null&&!result.Value.HasMore,"Partial source failure cannot issue a cursor that silently skips missing evidence.");
        store.AllUnavailable=true;
        Check.That((await useCase.ExecuteAsync(valid,default)).Error==Failures.StorageUnavailable,"Total source outage is unavailable, never an empty success.");
        Console.WriteLine("PASS Evidence Core: ReadEvidence authorization, bounded period, independent source failure and safe cursor stop.");
    }
    private sealed class Access(bool allowed):IOperatorAccess
    {
        public Task<Result<OperatorActor>> AuthorizeAsync(OpsCapability capability,CancellationToken ct)
        {
            Check.That(capability==OpsCapability.ReadEvidence,"Evidence requests its dedicated capability.");
            return Task.FromResult(allowed?Result<OperatorActor>.Success(new("github:1")):Result<OperatorActor>.Fail(Failures.Forbidden));
        }
    }
    private sealed class Probe:ICharacterReadStore,IEvidenceReadStore
    {
        internal int Reads;
        internal bool AllUnavailable;
        internal CharacterHeader Target=new(Guid.NewGuid(),"1",Guid.NewGuid(),"Test",DateTimeOffset.UtcNow,new(ProgressionState.Missing,null,null));
        public Task<Result<CharacterHeader>> FindAsync(Guid id,CancellationToken ct){Reads++;return Task.FromResult(Result<CharacterHeader>.Success(Target));}
        public Task<Result<Page<CharacterHeader>>> SearchAsync(CharacterLookup q,CancellationToken ct)=>throw new NotSupportedException();
        public Task<Result<Page<EvidenceItem>>> ReadAsync(CharacterHeader target,EvidenceSourceKind source,EvidenceQuery query,CancellationToken ct)
        {
            Reads++;
            if(AllUnavailable||source==EvidenceSourceKind.ItemReceipt)return Task.FromResult(Result<Page<EvidenceItem>>.Fail(Failures.StorageUnavailable));
            EvidenceItem[] items=source==EvidenceSourceKind.CurrencyReceipt?[new(source,Guid.NewGuid(),query.FromUtc,Target.CharacterId,
                "Character",Target.CharacterId.ToString(),null,Guid.NewGuid(),[],EvidenceStatus.Delivered,EvidenceLinkReason.DirectReceipt,"Test",[])]:[];
            return Task.FromResult(Result<Page<EvidenceItem>>.Success(new(items,source==EvidenceSourceKind.CurrencyReceipt,null)));
        }
        public Task<Result<EvidenceItem>> FindAsync(CharacterHeader target,EvidenceSourceKind kind,Guid id,CancellationToken ct)=>throw new NotSupportedException();
    }
}
