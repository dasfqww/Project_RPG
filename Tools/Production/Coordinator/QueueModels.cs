using ProjectRpg.Production.Contracts;

namespace ProjectRpg.Production.Coordinator;

public sealed record TaskSnapshot(TaskCard Card, TaskState State, AdoptionState Adoption, long Generation, int Attempts, string? BlockReason);
public sealed record QueueEvent(long Sequence, string? TaskId, string Kind, string Detail, long UtcMilliseconds);
public sealed record QueueStatus(bool Paused, TaskSnapshot[] Tasks);
public sealed class QueueException(string message) : Exception(message);
