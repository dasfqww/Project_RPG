using Microsoft.Extensions.Options;
using ProjectRpg.Backend.Configuration;
using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Domain.Trading;

namespace ProjectRpg.Backend.Application;

public sealed class TradeApplication(IGameRepository game, ITradeSessionRepository store,
    IOptions<TradingOptions> options, TradeRequestLimiter limiter, ITradeCommandObserver? observer = null)
{
    public async Task<TradeParticipant> ActorAsync(AuthenticatedPrincipal principal, Guid characterId, CancellationToken ct)
    {
        if (principal.Kind != PrincipalKind.Player || principal.SteamId is null || principal.IsSecurityTelemetryOnly)
            throw new TradeRuleException("trade_player_required");
        var character = await game.GetCharacterEconomyContextAsync(characterId, ct);
        if (character is null || character.SteamId != principal.SteamId) throw new TradeRuleException("character_not_owned");
        return new(principal.SteamId, characterId);
    }

    public Task<TradeStoredSession?> FindAsync(Guid? tradeId, TradeParticipant actor, CancellationToken ct) =>
        tradeId is { } id ? store.FindAsync(id, actor, ct) : store.FindActiveAsync(actor, ct);

    public Task<IReadOnlyList<TradeTarget>> FindTargetsAsync(TradeParticipant actor, string? input, CancellationToken ct)
    {
        if (!options.Value.Enabled) throw new TradeRuleException("trading_disabled");
        string name = TradeTargetSearch.Name(input);
        if (!limiter.AcquireSearch(actor.AccountId)) throw new TradeRuleException("trade_rate_limit");
        return store.FindTargetsAsync(actor, name, ct);
    }

    public async Task<TradeSessionResult> ExecuteAsync(TradeParticipant actor, Guid tradeId, Guid requestId,
        TradeCommandKind kind, long revision, long offerRevision, Guid? inviteeId, TradeOffer? offer, CancellationToken ct)
    {
        // No client/administrator/game-server command can select the worker-only action.
        if (kind == TradeCommandKind.Expire || !Enum.IsDefined(kind)) throw new TradeRuleException("invalid_trade_command");
        if (!options.Value.Enabled && kind is not (TradeCommandKind.Cancel or TradeCommandKind.Withdraw))
            throw new TradeRuleException("trading_disabled");
        TradeParticipant? invitee = null;
        string? currency = null;
        if (kind == TradeCommandKind.Invite)
        {
            var target = inviteeId is { } id ? await game.GetCharacterEconomyContextAsync(id, ct) : null;
            if (target is null) throw new TradeRuleException("trade_target_unavailable");
            invitee = new(target.SteamId, target.CharacterId);
            currency = (await store.FindAsync(tradeId, actor, ct))?.State.CurrencyCode ?? options.Value.CurrencyCode;
        }
        if (!limiter.Acquire(actor.AccountId, kind == TradeCommandKind.Invite, invitee?.AccountId))
            throw new TradeRuleException("trade_rate_limit");
        var result = await store.ExecuteAsync(new(requestId, tradeId, actor, kind, revision, offerRevision, invitee, currency, offer), ct);
        if (observer is not null)
        {
            try
            {
                var state = result.Session.State;
                if (result.RequestId == requestId && state.TradeId == tradeId && (actor == state.Initiator || actor == state.Invitee))
                    observer.Observe(new(state.TradeId,result.RequestId,actor.CharacterId,kind,state.Status,
                        result.Session.Completed && result.Session.Exchange?.TradeId == state.TradeId,result.Rejection is not null));
            }
            catch (Exception) { /* Optional diagnostics cannot change the committed command/exchange or its replay. */ }
        }
        return result;
    }
}
