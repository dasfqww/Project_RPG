using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

public sealed class InMemoryTransactionGate(TimeProvider? clock = null)
{
    internal object SyncRoot { get; } = new();
    internal DateTimeOffset UtcNow => (clock ?? TimeProvider.System).GetUtcNow();
    internal Dictionary<Guid, TradeAssetReservation> TradeReservations { get; } = [];
    // Stored JSON isolates callers from mutable collections in deserialized receipts; instances sharing this gate share state.
    internal Dictionary<Guid, string> TradeSessions { get; } = [];
    internal Dictionary<Guid, (string Fingerprint, string Result)> TradeSessionReceipts { get; } = [];

    // All callers hold SyncRoot, the same gate used by every item/currency mutation.
    internal bool IsItemReserved(Guid itemId) => TradeReservations.Values.Any(value =>
        value.At(UtcNow).Status == TradeReservationStatus.Active && value.Command.Offer.Items.Any(item => item.ItemId == itemId));

    internal long ReservedCurrency(CurrencyOwnerRef owner, string currencyCode) => TradeReservations.Values
        .Where(value => value.At(UtcNow).Status == TradeReservationStatus.Active
            && value.WalletOwner == owner && value.Command.CurrencyCode == currencyCode)
        .Sum(value => value.Command.Offer.CurrencyAmount);
}
