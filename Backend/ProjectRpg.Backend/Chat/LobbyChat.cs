namespace ProjectRpg.Backend.Chat;

internal sealed record ChatActor(Guid Id, string Name, string World);
internal sealed record ChatPost(Guid RequestId, string? Channel, string? Recipient, string? Text);
internal sealed record ChatLine(long Id, string Sender, string? Recipient, string Text);

// Presence and recent messages live only for the lifetime of this backend instance.
internal sealed class LobbyChat(TimeProvider clock)
{
    private sealed class Inbox(ChatActor actor)
    {
        public ChatActor Actor { get; } = actor;
        public DateTimeOffset LastPoll { get; set; }
        public bool InLobby { get; set; }
        public Queue<ChatLine> Lines { get; } = new();
        public Queue<ChatPost> Receipts { get; } = new();
        public DateTimeOffset LastPost { get; set; }
    }

    private readonly object gate = new();
    private readonly Dictionary<Guid, Inbox> online = new();
    private readonly Guid epoch = Guid.NewGuid();
    private long sequence;
    private DateTimeOffset nextSweep;

    public object Poll(ChatActor actor, bool inLobby, Guid? previousEpoch, long after)
    {
        lock (gate)
        {
            var now = clock.GetUtcNow();
            Sweep(now);
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
            if (now - sender.LastPost < TimeSpan.FromMilliseconds(500))
                return Results.StatusCode(StatusCodes.Status429TooManyRequests);

            var peers = online.Values.Where(p => p.Actor.World == actor.World && IsOnline(p, now));
            if (post.Channel == "whisper")
            {
                var target = peers.FirstOrDefault(p => string.Equals(p.Actor.Name, name, StringComparison.OrdinalIgnoreCase));
                if (target == null) return Results.NotFound(new { error = "recipient_offline" });
                var line = new ChatLine(++sequence, actor.Name, target.Actor.Name, body);
                Append(sender.Lines, line);
                if (target != sender) Append(target.Lines, line);
            }
            else
            {
                if (!sender.InLobby) return Results.StatusCode(StatusCodes.Status403Forbidden);
                var line = new ChatLine(++sequence, actor.Name, null, body);
                foreach (var peer in peers.Where(p => p.InLobby)) Append(peer.Lines, line);
            }
            sender.LastPost = now;
            Append(sender.Receipts, post);
            return Results.NoContent();
        }
    }

    private static bool IsOnline(Inbox inbox, DateTimeOffset now) =>
        now - inbox.LastPoll < TimeSpan.FromSeconds(30);

    private static void Append<T>(Queue<T> queue, T item)
    {
        if (queue.Count >= 64) queue.Dequeue();
        queue.Enqueue(item);
    }

    private void Sweep(DateTimeOffset now)
    {
        if (now < nextSweep) return;
        nextSweep = now.AddSeconds(30);
        foreach (var key in online.Where(p => !IsOnline(p.Value, now)).Select(p => p.Key).ToArray())
            online.Remove(key);
    }
}
