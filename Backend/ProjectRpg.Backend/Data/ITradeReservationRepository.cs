using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Data;

/// <summary>
/// Internal application port. Callers authenticate participants and control offer generations.
/// No asset moves, bilateral consent, lobby eligibility or settlement are implied by a reservation.
/// </summary>
public interface ITradeReservationRepository
{
    Task<TradeAssetReservation> ReserveAsync(TradeReservationCommand command, CancellationToken cancellationToken);
    // Also seals a never-created reservation against delayed Reserve commands.
    Task<TradeAssetReservation> ReleaseAsync(TradeReservationCommand command, CancellationToken cancellationToken);
    Task<TradeAssetReservation?> FindAsync(Guid reservationId, TradeParticipant participant, CancellationToken cancellationToken);
}
