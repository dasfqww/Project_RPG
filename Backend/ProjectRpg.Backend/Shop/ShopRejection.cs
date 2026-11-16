using System.Text.Json;

namespace ProjectRpg.Backend.Shop;

/// <summary>A durable decision for an authenticated, well-formed order. It can never later become a purchase.</summary>
public sealed record ShopRejection(string Error, DateTimeOffset RejectedAt)
{
    public bool Rejected => true;
}

public sealed class ShopRejectedException(ShopCommand command, string error, DateTimeOffset rejectedAt)
    : ShopRuleException(error)
{
    public ShopCommand Command { get; } = command;
    public DateTimeOffset RejectedAt { get; } = rejectedAt;
}

public static class ShopRejectionRules
{
    // Lock contention, cancellation, authentication, invalid/corrupt data and storage/service failures are retryable.
    // Only decisions known to precede all asset mutations may seal an order as rejected.
    public static bool IsDurable(string code) => code is
        "shop_disabled" or "shop_unavailable" or "shop_policy_changed" or "shop_offer_unavailable"
        or "shop_sale_not_allowed" or "shop_price_changed" or "shop_quantity_limit"
        or "shop_insufficient_balance" or "shop_inventory_full" or "shop_wallet_limit"
        or "shop_item_unavailable" or "shop_item_revision_conflict" or "shop_item_revision_exhausted"
        or "shop_currency_unavailable" or "shop_revision_exhausted" or "shop_requires_lobby"
        or "shop_item_identity_conflict";

    public static void Validate(ShopRejection rejection)
    {
        if (rejection is null || !IsDurable(rejection.Error) || rejection.RejectedAt == default)
            throw new InvalidDataException("Invalid shop rejection receipt.");
    }
}

internal sealed record ShopStoredOutcome(ShopReceipt? Receipt, ShopRejection? Rejection)
{
    public DateTimeOffset At => Receipt?.CommittedAt ?? Rejection!.RejectedAt;
    public ShopReceipt Replay(ShopCommand command) => Rejection is { } denied
        ? throw new ShopRejectedException(command, denied.Error, denied.RejectedAt) : Receipt!;

    public static ShopStoredOutcome Decode(string payload, ShopCommand command)
    {
        try { return DecodeCore(payload, command); }
        catch (JsonException error) { throw new InvalidDataException("Malformed stored shop outcome.", error); }
    }

    private static ShopStoredOutcome DecodeCore(string payload, ShopCommand command)
    {
        using var json = JsonDocument.Parse(payload);
        if (json.RootElement.ValueKind != JsonValueKind.Object)
            throw new InvalidDataException("Invalid shop outcome payload.");
        if (json.RootElement.TryGetProperty("Rejected", out var rejected))
        {
            string[] fields = ["Rejected", "Error", "RejectedAt"];
            var properties = json.RootElement.EnumerateObject().ToArray();
            if (rejected.ValueKind != JsonValueKind.True || properties.Length != fields.Length
                || fields.Any(name => properties.Count(p => p.Name == name) != 1))
                throw new InvalidDataException("Invalid shop rejection shape.");
            var denial = JsonSerializer.Deserialize<ShopRejection>(payload) ?? throw new InvalidDataException("Missing shop rejection.");
            ShopRejectionRules.Validate(denial);
            return new(null, denial);
        }
        var receipt = JsonSerializer.Deserialize<ShopReceipt>(payload) ?? throw new InvalidDataException("Missing shop receipt.");
        ShopRules.ValidateReceipt(receipt, command);
        return new(receipt, null);
    }
}
