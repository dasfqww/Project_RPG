using ProjectRpg.Backend.Data;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Evidence;

namespace ProjectRpg.Backend.Operations.Infrastructure.Reads;

public sealed class MemoryEvidenceReadStore(InMemoryGameRepository game, InMemoryEconomyRepository economy,
    InMemoryItemRepository items) : IEvidenceReadStore
{
    private IEnumerable<EvidenceItem> Rows(CharacterHeader target, EvidenceSourceKind source) => source switch
    {
        EvidenceSourceKind.CurrencyReceipt => economy.CaptureSnapshot().Receipts.Values
            .Where(r => r.CharacterId == target.CharacterId).Select(EvidenceMapping.Currency),
        EvidenceSourceKind.ItemReceipt => items.CaptureSnapshot().Receipts.Values
            .Where(r => EvidenceMapping.Matches(r, target)).Select(r => EvidenceMapping.Item(r, target)),
        _ => game.ReadOperationsSettlements(target.CharacterId)
    };
    public Task<Result<Page<EvidenceItem>>> ReadAsync(CharacterHeader target, EvidenceSourceKind source, EvidenceQuery query, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        var rows = Rows(target, source).Where(i => EvidenceMapping.InPage(i, query, source))
            .OrderByDescending(i => i.OccurredAtUtc).ThenByDescending(i => i.SourceKey).Take(query.Limit + 1).ToArray();
        return Task.FromResult(Result<Page<EvidenceItem>>.Success(ReadMapping.Page(rows, query.Limit, i => new(i.SourceKey, i.OccurredAtUtc))));
    }
    public Task<Result<EvidenceItem>> FindAsync(CharacterHeader target, EvidenceSourceKind source, Guid key, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();
        var row = Rows(target, source).SingleOrDefault(i => i.SourceKey == key);
        return Task.FromResult(row is null ? Result<EvidenceItem>.Fail(EvidenceMapping.Missing) : Result<EvidenceItem>.Success(row));
    }
}
