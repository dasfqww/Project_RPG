namespace ProjectRpg.Backend.Domain;

public sealed class DuplicateCharacterNameException(string characterName)
    : Exception($"Character name '{characterName}' already exists.");

public sealed class CharacterSessionConflictException(Guid characterId)
    : Exception($"Character '{characterId}' already has an active dungeon session.");

public sealed class DungeonSessionNotFoundException(Guid dungeonSessionId)
    : Exception($"Dungeon session '{dungeonSessionId}' was not found.");

public sealed class DungeonSessionNotJoinableException(Guid dungeonSessionId)
    : Exception($"Dungeon session '{dungeonSessionId}' is not accepting members.");

public sealed class DungeonSessionFullException(Guid dungeonSessionId)
    : Exception($"Dungeon session '{dungeonSessionId}' is full.");

public sealed class DungeonSessionServerMismatchException(Guid dungeonSessionId)
    : Exception($"Dungeon session '{dungeonSessionId}' belongs to another game server.");

public sealed class DungeonSessionStateConflictException(
    Guid dungeonSessionId,
    DungeonSessionState state)
    : Exception($"Dungeon session '{dungeonSessionId}' cannot transition from '{state}'.");

public sealed class DungeonSessionMembershipException(Guid dungeonSessionId)
    : Exception($"The character is not an active member of dungeon session '{dungeonSessionId}'.");

public sealed class DungeonRewardSettlementConflictException(
    Guid dungeonSessionId)
    : Exception($"Dungeon session '{dungeonSessionId}' already has a different reward settlement command.");
