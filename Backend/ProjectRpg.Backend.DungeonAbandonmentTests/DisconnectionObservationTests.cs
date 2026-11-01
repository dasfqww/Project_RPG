using ProjectRpg.Backend.Data;
using ProjectRpg.Backend.Domain;
using static Tests;

static class DisconnectionObservationTests
{
    private sealed class Clock : TimeProvider
    {
        public DateTimeOffset Now;
        public override DateTimeOffset GetUtcNow() => Now;
    }

    public static async Task RunSuite(string provider, Func<TimeProvider, IGameRepository> create)
    {
        async Task Test(string name, Func<Fixture, Clock, ConsumedJoinTicket, Task> action)
        {
            Clock clock = new() { Now = DateTimeOffset.UtcNow };
            Fixture f = await Fixture.Create(create(clock)); clock.Now = f.Now;
            ConsumedJoinTicket admission = await f.Admit();
            await action(f, clock, admission);
            Console.WriteLine($"PASS {provider} disconnection observation: {name}");
        }
        Task<DateTimeOffset> Report(Fixture f, ConsumedJoinTicket admission,
            DateTimeOffset? observed) => f.Repo.RecordDungeonMemberDisconnectionAsync(
                f.Session, f.Server, f.A.SteamId, f.A.CharacterId, admission.ConnectionGeneration,
                f.Now, default, observed);

        await Test("delayed first report keeps the original deadline and party rewards", async (f, clock, a) =>
        {
            clock.Now = f.Now.AddMinutes(10);
            Require(await Report(f, a, f.Now) == f.Now, "Receipt time replaced observation.");
            DungeonRewardSettlement settlement = await f.Settle();
            Require(settlement.CharacterIds.SequenceEqual([f.B.CharacterId]), "Delayed report extended rewards.");
            Require((await f.Read()).Members.Count == 2, "Reward filtering changed party membership.");
        });
        await Test("delayed first report immediately expires and prevents reentry", async (f, clock, a) =>
        {
            clock.Now = f.Now.AddMinutes(11); await Report(f, a, f.Now);
            string ticket = Guid.NewGuid().ToString("N"); await f.Ticket(ticket, f.A);
            Require(await f.Consume(ticket) is null, "Delayed report permitted an expired reentry.");
            var expelled = await f.Repo.ExpelDisconnectedDungeonMembersAsync(f.Session, f.Server, f.Now, default);
            Require(expelled.Count == 1 && expelled[0].CharacterId == f.A.CharacterId,
                "Delayed report did not expire only the disconnected member.");
            Require((await f.Read()).Members.Single().CharacterId == f.B.CharacterId, "Remaining party changed.");
        });
        await Test("retry cannot replace the first observation", async (f, clock, a) =>
        {
            clock.Now = f.Now.AddMinutes(9); await Report(f, a, f.Now);
            clock.Now = f.Now.AddMinutes(10);
            Require(await Report(f, a, clock.Now) == f.Now, "Retry restarted grace.");
            Require(!(await f.Settle()).CharacterIds.Contains(f.A.CharacterId), "Retry extended reward eligibility.");
        });
        await Test("legacy request retains backend receipt time", async (f, clock, a) =>
        {
            clock.Now = f.Now.AddMinutes(9);
            Require(await Report(f, a, null) == clock.Now, "Legacy receipt-time compatibility changed.");
        });
        await Test("small future skew is clamped to receipt, not added", async (f, clock, a) =>
        {
            clock.Now = f.Now.AddMinutes(9);
            Require(await Report(f, a, clock.Now.AddSeconds(5)) == clock.Now, "Future skew extended grace.");
        });
        await Test("small pre-join skew is clamped to membership start", async (f, clock, a) =>
        {
            clock.Now = f.Now.AddMinutes(9);
            Require(await Report(f, a, f.Now.AddSeconds(-5)) == f.Now, "Pre-join skew was not bounded.");
        });
        await Test("invalid clocks leave membership unmodified", async (f, clock, a) =>
        {
            foreach (DateTimeOffset invalid in new[] { f.Now.AddSeconds(6), f.Now.AddSeconds(-6),
                f.Now.ToOffset(TimeSpan.FromHours(9)), DateTimeOffset.MinValue })
            {
                bool rejected = false;
                try { await Report(f, a, invalid); }
                catch (ArgumentException) { rejected = true; }
                Require(rejected, "Invalid server clock was accepted.");
                Require((await f.Read()).Members.Single(m => m.CharacterId == f.A.CharacterId).DisconnectedAt is null,
                    "Invalid observation changed membership.");
            }
            Require(await Report(f, a, f.Now) == f.Now, "Valid retry after rejection failed.");
        });
        await Test("backend clock preceding membership cannot create a future marker", async (f, clock, a) =>
        {
            clock.Now = f.Now.AddSeconds(-1);
            bool rejected = false;
            try { await Report(f, a, f.Now); }
            catch (ArgumentException) { rejected = true; }
            Require(rejected, "A backwards backend clock accepted an observation outside membership bounds.");
        });
    }
}
