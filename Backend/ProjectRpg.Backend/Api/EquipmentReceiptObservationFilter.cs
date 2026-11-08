using ProjectRpg.Backend.Application;
using ProjectRpg.Backend.Contracts;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Api;

/// <summary>Observes confirmed, server-produced equipment receipts without participating in the command.</summary>
public sealed class EquipmentReceiptObservationFilter(IGameTransactionObserver? observer = null) : IEndpointFilter
{
    public async ValueTask<object?> InvokeAsync(EndpointFilterInvocationContext context, EndpointFilterDelegate next)
    {
        object? result = await next(context); // Business failures/cancellation retain the original endpoint behavior.
        if (observer is null) return result;
        try
        {
            if (result is not IValueHttpResult { Value: ItemCommitResponse receipt }
                || result is not IStatusCodeHttpResult { StatusCode: 200 }
                || receipt.Status is not ("Committed" or "AlreadyCommitted") || receipt.RequestId == Guid.Empty
                || receipt.Actor.Type != "Character"
                || !Guid.TryParseExact(receipt.Actor.OwnerId,"D",out Guid character) || character == Guid.Empty) return result;
            GameTransactionKind? kind = receipt.Operation switch
            {
                FirstSliceEquipmentCraftingPlanner.CraftOperation => GameTransactionKind.EquipmentCraft,
                FirstSliceEquipmentCraftingPlanner.DismantleOperation => GameTransactionKind.EquipmentDismantle,
                FirstSliceEquipmentCommandPlanner.UpgradeOperation => GameTransactionKind.EquipmentUpgrade,
                FirstSliceEquipmentCommandPlanner.TransferOperation => GameTransactionKind.EquipmentTransfer,
                _ => null
            };
            if (kind is { } confirmedKind)
                observer.Observe(new(confirmedKind,receipt.Status == "Committed" ? GameTransactionDisposition.Committed : GameTransactionDisposition.Replayed,
                    receipt.RequestId,character));
        }
        catch (Exception) { /* Optional diagnostics cannot change the already confirmed response. */ }
        return result;
    }
}
