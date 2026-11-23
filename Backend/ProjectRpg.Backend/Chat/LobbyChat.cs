using System.Text.RegularExpressions;

namespace ProjectRpg.Backend.Chat;

internal sealed record ChatActor(Guid Id, string Name, string World, string SteamId);
internal sealed record ChatPost(Guid RequestId, string? Channel, string? Recipient, string? Text);
internal sealed record ChatLine(long Id, string Sender, string? Recipient, string Text);

// Presence and recent messages live only for the lifetime of this backend instance.
internal sealed partial class LobbyChat(TimeProvider clock)
{
    private static readonly TimeSpan MinimumInterval = TimeSpan.FromMilliseconds(500);
    private static readonly TimeSpan SendWindow = TimeSpan.FromSeconds(10);
    private static readonly TimeSpan PublicRepeatWindow = TimeSpan.FromSeconds(20);
    private static readonly TimeSpan WhisperRepeatWindow = TimeSpan.FromSeconds(3);
    private const int AccountLimit = 8;
    private const int PublicLimit = 3;
    private const int WhisperLimit = 4;

    // A null recipient is the public lobby channel. Whispers use the resolved character ID.
    private sealed record AcceptedSend(TimeSpan At, Guid? Recipient, string ComparisonText);

    private sealed class Inbox(ChatActor actor)
    {
        public ChatActor Actor { get; } = actor;
        public DateTimeOffset LastPoll { get; set; }
        public bool InLobby { get; set; }
        public Queue<ChatLine> Lines { get; } = new();
        public Queue<ChatPost> Receipts { get; } = new();
    }

    private readonly object gate = new();
    private readonly Dictionary<Guid, Inbox> online = new();
    private readonly Dictionary<string, List<AcceptedSend>> sendsByAccount = new(StringComparer.Ordinal);
    private readonly long startedAt = clock.GetTimestamp();
    private readonly Guid epoch = Guid.NewGuid();
    private long sequence;
    private DateTimeOffset nextSweep;

    public object Poll(ChatActor actor, bool inLobby, Guid? previousEpoch, long after)
    {
        lock (gate)
        {
            var now = clock.GetUtcNow();
            Sweep(now, clock.GetElapsedTime(startedAt));
            if (!online.TryGetValue(actor.Id, out var inbox) || inbox.Actor != actor || !IsOnline(inbox, now))
                online[actor.Id] = inbox = new Inbox(actor);
            inbox.LastPoll = now;
            inbox.InLobby = inLobby;
            long cursor = previousEpoch == epoch ? after : 0;
            return new { epoch, cursor = sequence, messages = inbox.Lines
                .Where(m => m.Id > cursor && (inLobby || m.Recipient != null)).ToArray() };
        }
    }

    public IResult Send(ChatActor actor, ChatPost post)
    {
        var body = post.Text?.Trim();
        var name = post.Recipient?.Trim();
        if (post.RequestId == Guid.Empty || string.IsNullOrEmpty(body) || body.Length > 240
            || body.Any(char.IsControl)
            || (post.Channel switch
            {
                "lobby" => !string.IsNullOrEmpty(name),
                "whisper" => string.IsNullOrEmpty(name) || name.Length > 20,
                _ => true
            }))
            return Results.BadRequest();

        lock (gate)
        {
            var now = clock.GetUtcNow();
            if (!online.TryGetValue(actor.Id, out var sender) || sender.Actor != actor || !IsOnline(sender, now))
                return Results.Conflict(new { error = "chat_reconnect" });
            var previous = sender.Receipts.FirstOrDefault(r => r.RequestId == post.RequestId);
            if (previous != null)
                return previous == post ? Results.NoContent() : Results.Conflict();
            var peers = online.Values.Where(p => p.Actor.World == actor.World && IsOnline(p, now));
            Inbox? target = null;
            if (post.Channel == "whisper")
            {
                target = peers.FirstOrDefault(p => string.Equals(p.Actor.Name, name, StringComparison.OrdinalIgnoreCase));
                if (target == null) return Results.NotFound(new { error = "recipient_offline" });
            }
            else if (!sender.InLobby)
                return Results.StatusCode(StatusCodes.Status403Forbidden);

            var sentAt = clock.GetElapsedTime(startedAt);
            var comparisonText = Whitespace().Replace(body.Normalize(), " ");
            if (!sendsByAccount.TryGetValue(actor.SteamId, out var history))
                sendsByAccount[actor.SteamId] = history = new List<AcceptedSend>();

            var retry = CheckSend(history, sentAt, target?.Actor.Id, comparisonText);
            if (retry != null) return retry;

            if (target != null)
            {
                var line = new ChatLine(++sequence, actor.Name, target.Actor.Name, body);
                Append(sender.Lines, line);
                if (target != sender) Append(target.Lines, line);
            }
            else
            {
                var line = new ChatLine(++sequence, actor.Name, null, body);
                foreach (var peer in peers.Where(p => p.InLobby)) Append(peer.Lines, line);
            }
            history.Add(new AcceptedSend(sentAt, target?.Actor.Id, comparisonText));
            Append(sender.Receipts, post);
            return Results.NoContent();
        }
    }

    private static IResult? CheckSend(List<AcceptedSend> history, TimeSpan now, Guid? recipient, string comparisonText)
    {
        history.RemoveAll(send => now - send.At >= PublicRepeatWindow);
        var channelLimit = recipient.HasValue ? WhisperLimit : PublicLimit;
        var repeatWindow = recipient.HasValue ? WhisperRepeatWindow : PublicRepeatWindow;

        int totalCount = 0;
        int channelCount = 0;
        var rateWait = TimeSpan.Zero;
        var repeatWait = TimeSpan.Zero;
        if (history.Count > 0)
            rateWait = MinimumInterval - (now - history[^1].At);

        for (int index = history.Count - 1; index >= 0; index--)
        {
            var send = history[index];
            var age = now - send.At;
            if (age < SendWindow && ++totalCount == AccountLimit)
                rateWait = Max(rateWait, SendWindow - age);

            if (send.Recipient != recipient) continue;
            if (age < SendWindow && ++channelCount == channelLimit)
                rateWait = Max(rateWait, SendWindow - age);
            if (age < repeatWindow && string.Equals(send.ComparisonText, comparisonText, StringComparison.OrdinalIgnoreCase))
                repeatWait = Max(repeatWait, repeatWindow - age);
        }

        var wait = Max(rateWait, repeatWait);
        if (wait <= TimeSpan.Zero) return null;
        return Results.Json(new
        {
            error = repeatWait >= rateWait ? "chat_duplicate" : "chat_rate_limited",
            retryAfterSeconds = (int)Math.Ceiling(wait.TotalSeconds)
        }, statusCode: StatusCodes.Status429TooManyRequests);
    }

    private static TimeSpan Max(TimeSpan a, TimeSpan b) => a >= b ? a : b;

    [GeneratedRegex(@"\s+")]
    private static partial Regex Whitespace();

    private static bool IsOnline(Inbox inbox, DateTimeOffset now) =>
        now - inbox.LastPoll < TimeSpan.FromSeconds(30);

    private static void Append<T>(Queue<T> queue, T item)
    {
        if (queue.Count >= 64) queue.Dequeue();
        queue.Enqueue(item);
    }

    private void Sweep(DateTimeOffset now, TimeSpan elapsed)
    {
        if (now < nextSweep) return;
        nextSweep = now.AddSeconds(30);
        foreach (var key in online.Where(p => !IsOnline(p.Value, now)).Select(p => p.Key).ToArray())
            online.Remove(key);
        foreach (var key in sendsByAccount.Where(p => elapsed - p.Value[^1].At >= PublicRepeatWindow)
                     .Select(p => p.Key).ToArray())
            sendsByAccount.Remove(key);
    }
}
