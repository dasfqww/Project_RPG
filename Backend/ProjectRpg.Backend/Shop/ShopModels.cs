using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Shop;

public sealed record ShopActor(string AccountId, Guid CharacterId);
public sealed record ShopCommand(Guid RequestId, ShopActor Actor, string ShopId, string PolicyTag,
    string Action, string OfferId, int Quantity, long ExpectedUnitPrice, Guid? ItemId = null,
    long? ExpectedItemRevision = null);
public sealed record ShopOffer(string OfferId, string DisplayName, string DefinitionType,
    string DefinitionName, int DefinitionVersion, long UnitPrice, int MaximumQuantity,
    int MaximumStackSize, ItemBindState BindState);
public sealed record ShopSaleRule(string RuleId, string DisplayName, string DefinitionType,
    string DefinitionName, int DefinitionVersion, long UnitPrice, int MaximumQuantity,
    int MaximumStackSize, IReadOnlyList<ItemBindState> AllowedBindStates);
public sealed record ShopPlan(long TotalPrice, long BalanceAfter, IReadOnlyList<ItemRecordMutation> Mutations);
public sealed record ShopReceipt(Guid RequestId, ShopActor Actor, string ShopId, string PolicyTag,
    string Action, string OfferId, Guid? ItemId, int Quantity, long UnitPrice, long TotalPrice,
    string CurrencyCode, long BalanceBefore, long BalanceAfter, IReadOnlyList<ItemRecord> Records,
    DateTimeOffset CommittedAt);

public class ShopRuleException(string code) : Exception(code)
{
    public string Code { get; } = code;
}

public static class ShopRules
{
    public const int MaximumOrderQuantity = 10_000;
    public const int MaximumCatalogEntries = 128;

    public static void Validate(ShopCommand c)
    {
        if (c is null || c.RequestId == Guid.Empty || !ValidActor(c.Actor)
            || !ValidIdentity(c.ShopId, 64) || !ValidIdentity(c.OfferId, 64)
            || !ValidPolicyTag(c.PolicyTag) || c.Action is not ("buy" or "sell")
            || c.Quantity is < 1 or > MaximumOrderQuantity || c.ExpectedUnitPrice <= 0
            || (c.Action == "buy" && (c.ItemId is not null || c.ExpectedItemRevision is not null))
            || (c.Action == "sell" && (c.ItemId is null || c.ItemId == Guid.Empty
                || c.ExpectedItemRevision is null or <= 0 or long.MaxValue)))
            throw new ShopRuleException("invalid_shop_command");
        _ = Total(c.ExpectedUnitPrice, c.Quantity);
    }

    public static string Fingerprint(ShopCommand c)
    {
        Validate(c);
        return Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(c))));
    }

    public static long Total(long unitPrice, int quantity)
    {
        if (unitPrice <= 0 || quantity <= 0 || quantity > MaximumOrderQuantity
            || unitPrice > long.MaxValue / quantity) throw new ShopRuleException("shop_price_overflow");
        return unitPrice * quantity;
    }

    public static bool ValidActor(ShopActor? actor) => actor is not null
        && ValidIdentity(actor.AccountId, 64) && actor.CharacterId != Guid.Empty;
    public static bool ValidIdentity(string? value, int maximum = 128) => !string.IsNullOrEmpty(value)
        && value.Length <= maximum && value.All(c => char.IsAsciiLetterOrDigit(c) || c is '.' or '_' or '-');
    public static bool ValidPolicyTag(string? value) => value is not null && value.Length == 64
        && value.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'f');
    internal static bool ValidDisplay(string? value) => !string.IsNullOrWhiteSpace(value)
        && value.Length <= 128 && value == value.Trim() && !value.Any(char.IsControl);
    internal static bool ValidRecord(ItemRecord? r) => r is not null && r.Owner is not null
        && r.Location is not null && r.State is not null && r.Metadata?.Durability is not null
        && r.DefinitionType is not null && r.DefinitionName is not null && r.Owner.OwnerId is not null
        && r.Location.ContainerId is not null && r.Metadata.CreationSource is not null
        && r.State.InstanceTags is not null && r.State.StatValues is not null
        && r.State.InstanceTags.All(t => t is not null) && r.State.StatValues.All(s => s is not null)
        && ItemRecordRules.TryValidateRecord(r, out _);

    // Validate stored receipts independently of the current catalog: policy changes never rewrite history.
    public static void ValidateReceipt(ShopReceipt r, ShopCommand c)
    {
        Validate(c);
        long total = Total(c.ExpectedUnitPrice, c.Quantity);
        if (r is null || r.RequestId != c.RequestId || r.Actor != c.Actor || r.ShopId != c.ShopId
            || r.PolicyTag != c.PolicyTag || r.Action != c.Action || r.OfferId != c.OfferId
            || r.ItemId != c.ItemId || r.Quantity != c.Quantity || r.UnitPrice != c.ExpectedUnitPrice
            || r.TotalPrice != total || !ValidIdentity(r.CurrencyCode, 64)
            || r.BalanceBefore < 0 || r.BalanceAfter < 0 || r.CommittedAt == default
            || (c.Action == "buy" ? r.BalanceBefore < total || r.BalanceAfter != r.BalanceBefore - total
                : r.BalanceBefore > long.MaxValue - total || r.BalanceAfter != r.BalanceBefore + total)
            || r.Records is null || r.Records.Count is < 1 or > InventoryBagLayout.TotalSlots + 1
            || r.Records.Any(i => !ValidRecord(i) || i.Revision <= 0
                || i.Owner != new ItemOwnerRef(ItemOwnerType.Character, c.Actor.CharacterId.ToString("D")))
            || r.Records.Select(i => i.ItemId).Distinct().Count() != r.Records.Count
            || (c.Action == "buy" && r.Records.Any(i => i.LifecycleState != ItemLifecycleState.Active
                || i.Location.ContainerType != ItemContainerType.Inventory || !InventoryBagLayout.CanStore(i)
                || i.Location.ContainerId != c.Actor.CharacterId.ToString("D")))
            || (c.Action == "sell" && (r.Records.Count != 1 || r.Records[0].ItemId != c.ItemId
                || r.Records[0].Revision != c.ExpectedItemRevision + 1
                || r.Records[0].LifecycleState is not (ItemLifecycleState.Active or ItemLifecycleState.Consumed)
                || (r.Records[0].LifecycleState == ItemLifecycleState.Active
                    && (r.Records[0].Location.ContainerType != ItemContainerType.Inventory
                        || r.Records[0].Location.ContainerId != c.Actor.CharacterId.ToString("D")
                        || !InventoryBagLayout.CanStore(r.Records[0]))))))
            throw new InvalidDataException("Invalid shop receipt.");
    }
}
