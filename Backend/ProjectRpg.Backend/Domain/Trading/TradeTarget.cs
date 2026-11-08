namespace ProjectRpg.Backend.Domain.Trading;

// A discovery hint, never an admission grant or a claim that the player is online.
public sealed record TradeTarget(Guid CharacterId, string Name);

public static class TradeTargetSearch
{
    public static string Name(string? input)
    {
        if (input is null || input.Length > 64) throw new TradeRuleException("invalid_trade_target_query");
        string name = input.Trim();
        if (name.Length is < 2 or > 20 || name.Any(char.IsControl))
            throw new TradeRuleException("invalid_trade_target_query");
        return name;
    }
}
