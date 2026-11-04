using System.Diagnostics;

namespace ProjectRpg.Backend.Operations.Infrastructure.Diagnostics;

internal sealed class LogReadLimitException(bool timeout) : Exception
{
    internal bool Timeout { get; } = timeout;
}
internal sealed class LogReadBudget
{
    private readonly Stopwatch elapsed = Stopwatch.StartNew();
    internal long Bytes { get; private set; }
    internal void Add(int count)
    {
        if (elapsed.Elapsed >= TimeSpan.FromSeconds(3)) throw new LogReadLimitException(true);
        if (Bytes + count > 64L * 1024 * 1024) throw new LogReadLimitException(false);
        Bytes += count;
    }
}
internal sealed class ReverseJsonLines(FileStream stream, long offset, LogReadBudget budget)
{
    private readonly byte[] buffer = new byte[65536];
    private long bufferStart = -1;
    private int bufferLength;
    internal long Offset { get; private set; } = offset;
    private async ValueTask<byte> PeekAsync(CancellationToken ct)
    {
        long position = Offset - 1;
        if (position < bufferStart || position >= bufferStart + bufferLength)
        {
            int length = (int)Math.Min(buffer.Length, Offset);
            budget.Add(length);
            bufferStart = Offset - length;
            stream.Position = bufferStart;
            bufferLength = await stream.ReadAsync(buffer.AsMemory(0, length), ct);
            if (bufferLength != length) throw new IOException("Log was truncated while reading.");
        }
        return buffer[position - bufferStart];
    }

    internal async Task<(byte[]? Line, bool Corrupt)> PreviousAsync(CancellationToken ct)
    {
        if (Offset == 0) return (null, false);
        bool complete = await PeekAsync(ct) == 10;
        if (complete) Offset--;
        List<byte> reverse = new(512);
        bool tooLong = false;
        while (Offset > 0)
        {
            byte current = await PeekAsync(ct);
            if (current == 10) break;
            Offset--;
            if (reverse.Count < 32768) reverse.Add(current); else tooLong = true;
        }
        if (!complete || tooLong) return (null, true);
        reverse.Reverse();
        if (reverse.Count > 0 && reverse[^1] == 13) reverse.RemoveAt(reverse.Count - 1);
        return (reverse.ToArray(), false);
    }
}
