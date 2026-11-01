using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;

if (args.Contains("--first-slice-starter-memory", StringComparer.Ordinal)
    || args.Contains("--first-slice-starter-postgres", StringComparer.Ordinal))
{
    if (args.Length != 1) throw new ArgumentException("Starter grant modes cannot be combined.");
    await FirstSliceStarterCases.RunAsync(args[0] == "--first-slice-starter-postgres");
    return;
}

if (args.Contains("--equipment-rewards-memory", StringComparer.Ordinal)
    || args.Contains("--equipment-rewards-postgres", StringComparer.Ordinal))
{
    if (args.Length != 1) throw new ArgumentException("Equipment reward modes cannot be combined.");
    await EquipmentRewardCases.RunAsync(args[0] == "--equipment-rewards-postgres");
    return;
}

if (args.Contains("--equipment-crafting-memory", StringComparer.Ordinal)
    || args.Contains("--equipment-crafting-postgres", StringComparer.Ordinal))
{
    if (args.Length != 1) throw new ArgumentException("Equipment crafting modes cannot be combined.");
    await EquipmentCraftingCases.RunAsync(args[0] == "--equipment-crafting-postgres");
    return;
}

if (args.Contains("--equipment-lobby-memory", StringComparer.Ordinal)
    || args.Contains("--equipment-lobby-postgres", StringComparer.Ordinal))
{
    if (args.Length != 1) throw new ArgumentException("Equipment lobby modes cannot be combined.");
    await EquipmentLobbyCases.RunAsync(args[0] == "--equipment-lobby-postgres");
    return;
}

if (args.Contains("--equipment-commands-only", StringComparer.Ordinal))
{
    if (args.Length != 1) throw new ArgumentException("The equipment command mode cannot be combined.");
    await EquipmentCommandCases.RunAsync();
    return;
}

if (args.Contains("--equipment-persistence-only", StringComparer.Ordinal)
    || args.Contains("--equipment-postgres-only", StringComparer.Ordinal))
{
    if (args.Length != 1) throw new ArgumentException("Equipment persistence modes cannot be combined.");
    await EquipmentPersistenceCases.RunAsync(args[0] == "--equipment-postgres-only");
    return;
}

if (args.Contains("--equipment-progression-only", StringComparer.Ordinal))
{
    if (args.Length != 1) throw new ArgumentException("The equipment progression mode cannot be combined with other arguments.");
    EquipmentProgressionCases.Run();
    return;
}

InMemoryTransactionGate gate = new();
InMemoryItemRepository items = new(gate);
InMemoryGameRepository game = new(gate, items);
DateTimeOffset now = DateTimeOffset.UtcNow;
const string steamId = "76561198000000001";
Require(CharacterCreationCatalog.Options.Count == 1
    && CharacterCreationCatalog.Options[0].ClassId == FirstSliceProgression.ClassId
    && CharacterCreationCatalog.IsSupported(FirstSliceProgression.ClassId)
    && CharacterCreationCatalog.IsSupported(null), "The offered and legacy default classes must use the implemented recipe.");
foreach (string unsupported in new[] { "", " ", FirstSliceProgression.LegacyClassId,
    FirstSliceProgression.ClassId.ToLowerInvariant(), "RPGPlayerClassDefinition:Unknown" })
    Require(!CharacterCreationCatalog.IsSupported(unsupported), "Explicit unsupported classes must not fall back to the default.");
await game.UpsertAccountAsync(new SteamIdentity(steamId, steamId, false, false), now, default);

GameCharacter character = await game.CreateCharacterAsync(steamId, "StarterHero", now, default);
string ownerId = character.CharacterId.ToString("D");
ItemOwnerRef owner = new(ItemOwnerType.Character, ownerId);
IReadOnlyList<ItemRecord> records = await items.FindByOwnerAsync(owner, false, 100, default);
FirstSliceStarterCases.Verify(records, character.CharacterId);
ItemRecord starter = records.Single(item => item.Location.SlotIndex == StarterEquipment.PrimaryRightWeaponSlot);
Require(starter.DefinitionType == StarterEquipment.DefinitionType
    && starter.DefinitionName == "GameItem.Equipment.Progression.Starter.GreatSword"
    && starter.DefinitionVersion == 1
    && starter.Owner == owner
    && starter.Location == new ItemLocation(ItemContainerType.Equipment, ownerId,
        StarterEquipment.PrimaryRightWeaponSlot)
    && starter.State.Quantity == 1
    && starter.Revision == 1
    && starter.Metadata.BindState == ItemBindState.CharacterBound
    && starter.Metadata.CreationSource == "CharacterCreation",
    "Starter weapon record is not usable as authoritative equipment.");

bool duplicateRejected = false;
try
{
    await game.CreateCharacterAsync(steamId, "starterhero", now, default);
}
catch (DuplicateCharacterNameException)
{
    duplicateRejected = true;
}
Require(duplicateRejected, "Duplicate character name must be rejected.");
Require((await game.GetCharactersAsync(steamId, default)).Count == 1
    && (await items.FindByOwnerAsync(owner, false, 100, default)).Count == 3,
    "Rejected creation must not duplicate the character or starter item.");

Console.WriteLine("StarterEquipment: memory character + three equipped +0 items + duplicate guard passed.");
await ProgressionCases.RunAsync();

static void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}
