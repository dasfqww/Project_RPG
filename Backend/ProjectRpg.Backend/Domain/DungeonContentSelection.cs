namespace ProjectRpg.Backend.Domain;

// Exact pair: allowing two IDs must not produce a cross-product of difficulties.
public sealed record DungeonContentSelection(string DungeonId, string Difficulty);
