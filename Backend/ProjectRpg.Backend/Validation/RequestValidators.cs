using ProjectRpg.Backend.Contracts;

namespace ProjectRpg.Backend.Validation;

public static class RequestValidators
{
    public static string? ValidateDungeonSelection(
        string? rawDungeonId,
        string? rawDifficulty,
        out string dungeonId,
        out string difficulty)
    {
        dungeonId = rawDungeonId?.Trim() ?? string.Empty;
        difficulty = rawDifficulty?.Trim() ?? string.Empty;
        if (!IsSimpleIdentifier(dungeonId, 64))
        {
            return "Dungeon ID must contain 1 to 64 letters, digits, dots, underscores, or hyphens.";
        }

        if (!IsSimpleIdentifier(difficulty, 32))
        {
            return "Difficulty must contain 1 to 32 letters, digits, dots, underscores, or hyphens.";
        }

        return null;
    }

    public static string? ValidateServerId(string? rawServerId, out string serverId)
    {
        serverId = rawServerId?.Trim() ?? string.Empty;
        return IsSimpleIdentifier(serverId, 128)
            ? null
            : "Server ID must contain 1 to 128 letters, digits, dots, underscores, or hyphens.";
    }

    public static string? ValidateServerAddress(
        string? rawServerAddress,
        out string serverAddress)
    {
        serverAddress = rawServerAddress?.Trim() ?? string.Empty;
        if (serverAddress.Length is < 3 or > 255
            || serverAddress.Any(char.IsWhiteSpace)
            || serverAddress.Any(char.IsControl)
            || serverAddress.Contains('/')
            || serverAddress.Contains('?')
            || serverAddress.Contains('#')
            || !Uri.TryCreate(
                $"udp://{serverAddress}",
                UriKind.Absolute,
                out Uri? endpoint)
            || string.IsNullOrWhiteSpace(endpoint.Host)
            || endpoint.Port is < 1 or > 65535)
        {
            return "Server address must be a host:port endpoint with a port from 1 to 65535.";
        }

        return null;
    }

    public static string? ValidateCharacterName(string? rawName, out string normalizedName)
    {
        normalizedName = rawName?.Trim() ?? string.Empty;
        if (normalizedName.Length is < 2 or > 20)
        {
            return "Character name must contain 2 to 20 characters.";
        }

        if (normalizedName.Any(char.IsControl))
        {
            return "Character name contains an invalid control character.";
        }

        return null;
    }

    public static string? ValidateInventory(IReadOnlyList<InventoryItemContract>? inventory)
    {
        if (inventory is null)
        {
            return "Inventory is required.";
        }

        if (inventory.Count > 200)
        {
            return "Inventory cannot contain more than 200 entries.";
        }

        HashSet<int> slots = [];
        HashSet<string> instanceIds = new(StringComparer.Ordinal);
        foreach (InventoryItemContract item in inventory)
        {
            if (string.IsNullOrWhiteSpace(item.ItemId) || item.ItemId.Length > 128)
            {
                return "Inventory contains an invalid item ID.";
            }

            if (item.Quantity is < 1 or > 999_999)
            {
                return "Inventory contains an invalid quantity.";
            }

            if (item.SlotIndex is < 0 or >= 200 || !slots.Add(item.SlotIndex))
            {
                return "Inventory contains an invalid or duplicate slot index.";
            }

            if (item.Category.Length > 64 || item.InstanceId.Length > 128)
            {
                return "Inventory contains oversized metadata.";
            }

            if (!string.IsNullOrEmpty(item.InstanceId) && !instanceIds.Add(item.InstanceId))
            {
                return "Inventory contains a duplicate item instance ID.";
            }
        }

        return null;
    }

    private static bool IsSimpleIdentifier(string value, int maxLength)
    {
        return value.Length >= 1
            && value.Length <= maxLength
            && value.All(character =>
                char.IsAsciiLetterOrDigit(character)
                || character is '.' or '_' or '-');
    }
}
