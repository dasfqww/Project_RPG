namespace ProjectRpg.Backend.Data;

public sealed class InMemoryTransactionGate
{
    internal object SyncRoot { get; } = new();
}
