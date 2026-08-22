using System.Collections.Concurrent;
using ProjectRpg.Backend.Domain;

namespace ProjectRpg.Backend.Data;

public sealed class InMemoryGameRepository : IGameRepository
{
    private sealed record AccountRecord(
        SteamIdentity Identity,
        DateTimeOffset CreatedAt,
        DateTimeOffset LastLoginAt);

    private sealed record SessionRecord(string SteamId, DateTimeOffset ExpiresAt);

    private sealed record GameServerCredentialRecord(
        string ServerId,
        Guid DungeonSessionId,
        DateTimeOffset ExpiresAt);

    private sealed record JoinTicketRecord(
        string SteamId,
        Guid CharacterId,
        Guid DungeonSessionId,
        DateTimeOffset ExpiresAt);

    private sealed record CharacterLeaseRecord(
        Guid DungeonSessionId,
        DateTimeOffset ExpiresAt);

    private readonly ConcurrentDictionary<string, AccountRecord> _accounts = new();
    private readonly ConcurrentDictionary<string, SessionRecord> _sessions = new();
    private readonly ConcurrentDictionary<string, GameServerCredentialRecord>
        _gameServerCredentials = new();
    private readonly ConcurrentDictionary<string, JoinTicketRecord> _joinTickets = new();
    private readonly ConcurrentDictionary<Guid, Roster> _rosters = new();
    private readonly ConcurrentDictionary<Guid, GameCharacter> _characters = new();
    private readonly ConcurrentDictionary<Guid, IReadOnlyList<InventoryItem>> _inventories = new();
    private readonly Dictionary<Guid, DungeonSession> _dungeonSessions = new();
    private readonly Dictionary<Guid, DungeonRewardSettlement>
        _dungeonRewardSettlements = new();
    private readonly Dictionary<Guid, CharacterLeaseRecord> _characterLeases = new();
    private readonly object _characterGate = new();
    private readonly object _dungeonGate = new();

    public Task EnsureCreatedAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    public Task UpsertAccountAsync(
        SteamIdentity identity,
        DateTimeOffset authenticatedAt,
        CancellationToken cancellationToken)
    {
        _accounts.AddOrUpdate(
            identity.SteamId,
            _ => new AccountRecord(identity, authenticatedAt, authenticatedAt),
            (_, existing) => existing with
            {
                Identity = identity,
                LastLoginAt = authenticatedAt
            });
        lock (_characterGate)
        {
            if (!_rosters.Values.Any(roster =>
                roster.SteamId == identity.SteamId
                && roster.WorldId == "main"))
            {
                Roster roster = new(
                    Guid.NewGuid(),
                    identity.SteamId,
                    "main",
                    authenticatedAt);
                _rosters[roster.RosterId] = roster;
            }
        }
        return Task.CompletedTask;
    }

    public Task StoreSessionAsync(
        string tokenHash,
        string steamId,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken)
    {
        _sessions[tokenHash] = new SessionRecord(steamId, expiresAt);
        return Task.CompletedTask;
    }

    public Task<string?> ResolveSessionAsync(
        string tokenHash,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        if (!_sessions.TryGetValue(tokenHash, out SessionRecord? session))
        {
            return Task.FromResult<string?>(null);
        }

        if (session.ExpiresAt <= now)
        {
            _sessions.TryRemove(tokenHash, out _);
            return Task.FromResult<string?>(null);
        }

        return Task.FromResult<string?>(session.SteamId);
    }

    public Task StoreGameServerCredentialAsync(
        string tokenHash,
        string serverId,
        Guid dungeonSessionId,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            foreach ((string hash, GameServerCredentialRecord credential) in
                _gameServerCredentials.ToArray())
            {
                if (credential.DungeonSessionId == dungeonSessionId)
                {
                    _gameServerCredentials.TryRemove(hash, out _);
                }
            }

            _gameServerCredentials[tokenHash] = new GameServerCredentialRecord(
                serverId,
                dungeonSessionId,
                expiresAt);
        }

        return Task.CompletedTask;
    }

    public Task<GameServerCredential?> ResolveGameServerCredentialAsync(
        string tokenHash,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            if (!_gameServerCredentials.TryGetValue(
                    tokenHash,
                    out GameServerCredentialRecord? credential)
                || credential.ExpiresAt <= now
                || !_dungeonSessions.TryGetValue(
                    credential.DungeonSessionId,
                    out DungeonSession? session)
                || !string.Equals(
                    session.ServerId,
                    credential.ServerId,
                    StringComparison.Ordinal)
                || session.ExpiresAt <= now
                || session.State is not (
                    DungeonSessionState.Loading
                    or DungeonSessionState.InProgress))
            {
                _gameServerCredentials.TryRemove(tokenHash, out _);
                return Task.FromResult<GameServerCredential?>(null);
            }

            return Task.FromResult<GameServerCredential?>(new(
                credential.ServerId,
                credential.DungeonSessionId));
        }
    }

    public Task<IReadOnlyList<GameCharacter>> GetCharactersAsync(
        string steamId,
        CancellationToken cancellationToken)
    {
        IReadOnlyList<GameCharacter> characters = _characters.Values
            .Where(character => character.SteamId == steamId)
            .OrderBy(character => character.CreatedAt)
            .ToArray();
        return Task.FromResult(characters);
    }

    public Task<IReadOnlyList<Roster>> GetRostersAsync(
        string steamId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        IReadOnlyList<Roster> rosters = _rosters.Values
            .Where(roster => roster.SteamId == steamId)
            .OrderBy(roster => roster.WorldId, StringComparer.Ordinal)
            .ToArray();
        return Task.FromResult(rosters);
    }

    public Task<GameCharacter> CreateCharacterAsync(
        string steamId,
        string name,
        DateTimeOffset createdAt,
        CancellationToken cancellationToken)
    {
        lock (_characterGate)
        {
            bool nameExists = _characters.Values.Any(character =>
                string.Equals(character.Name, name, StringComparison.OrdinalIgnoreCase));
            if (nameExists)
            {
                throw new DuplicateCharacterNameException(name);
            }

            Roster roster = _rosters.Values.SingleOrDefault(candidate =>
                candidate.SteamId == steamId
                && candidate.WorldId == "main")
                ?? throw new InvalidOperationException(
                    "The account does not have a default roster.");
            GameCharacter character = new(
                Guid.NewGuid(),
                roster.RosterId,
                steamId,
                name,
                createdAt);
            _characters[character.CharacterId] = character;
            return Task.FromResult(character);
        }
    }

    public Task<string?> GetCharacterOwnerAsync(
        Guid characterId,
        CancellationToken cancellationToken)
    {
        string? owner = _characters.TryGetValue(characterId, out GameCharacter? character)
            ? character.SteamId
            : null;
        return Task.FromResult(owner);
    }

    public Task<CharacterEconomyContext?> GetCharacterEconomyContextAsync(
        Guid characterId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        CharacterEconomyContext? context = _characters.TryGetValue(
            characterId,
            out GameCharacter? character)
            ? new CharacterEconomyContext(
                character.CharacterId,
                character.RosterId,
                character.SteamId)
            : null;
        return Task.FromResult(context);
    }

    public Task<DungeonSession?> GetDungeonSessionAsync(
        Guid dungeonSessionId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            _dungeonSessions.TryGetValue(
                dungeonSessionId,
                out DungeonSession? session);
            return Task.FromResult(session);
        }
    }

    public Task<DungeonSession?> GetActiveDungeonSessionForCharacterAsync(
        Guid characterId,
        string steamId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            if (!_characterLeases.TryGetValue(
                    characterId,
                    out CharacterLeaseRecord? lease)
                || lease.ExpiresAt <= now)
            {
                return Task.FromResult<DungeonSession?>(null);
            }

            if (!_dungeonSessions.TryGetValue(
                    lease.DungeonSessionId,
                    out DungeonSession? session)
                || session.State is not (
                    DungeonSessionState.Waiting
                    or DungeonSessionState.Loading
                    or DungeonSessionState.InProgress)
                || session.ExpiresAt <= now)
            {
                return Task.FromResult<DungeonSession?>(null);
            }

            if (!session.Members.Any(member =>
                    member.CharacterId == characterId
                    && string.Equals(
                        member.SteamId,
                        steamId,
                        StringComparison.Ordinal)
                    && member.LeaseExpiresAt > now))
            {
                return Task.FromResult<DungeonSession?>(null);
            }

            return Task.FromResult<DungeonSession?>(session);
        }
    }

    public Task<DungeonSession> CreateDungeonSessionAsync(
        string steamId,
        Guid characterId,
        string dungeonId,
        string difficulty,
        DateTimeOffset now,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            AcquireCharacterLease(characterId, Guid.Empty, now, expiresAt);

            Guid dungeonSessionId = Guid.NewGuid();
            _characterLeases[characterId] = new CharacterLeaseRecord(
                dungeonSessionId,
                expiresAt);
            DungeonSessionMember member = new(
                characterId,
                steamId,
                now,
                expiresAt);
            DungeonSession session = new(
                dungeonSessionId,
                dungeonId,
                difficulty,
                DungeonSessionState.Waiting,
                null,
                null,
                now,
                now,
                expiresAt,
                [member]);
            _dungeonSessions[dungeonSessionId] = session;
            return Task.FromResult(session);
        }
    }

    public Task<DungeonSession> JoinDungeonSessionAsync(
        Guid dungeonSessionId,
        string steamId,
        Guid characterId,
        int maxPartySize,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            if (session.State != DungeonSessionState.Waiting)
            {
                throw new DungeonSessionNotJoinableException(dungeonSessionId);
            }

            if (session.Members.Any(member =>
                member.CharacterId == characterId
                && member.SteamId == steamId))
            {
                return Task.FromResult(session);
            }

            if (session.Members.Count >= maxPartySize)
            {
                throw new DungeonSessionFullException(dungeonSessionId);
            }

            AcquireCharacterLease(
                characterId,
                dungeonSessionId,
                now,
                leaseExpiresAt);
            _characterLeases[characterId] = new CharacterLeaseRecord(
                dungeonSessionId,
                leaseExpiresAt);
            DungeonSessionMember[] members = session.Members
                .Select(member => member with
                {
                    LeaseExpiresAt = leaseExpiresAt
                })
                .Append(new DungeonSessionMember(
                    characterId,
                    steamId,
                    now,
                    leaseExpiresAt))
                .ToArray();
            foreach (DungeonSessionMember member in members)
            {
                _characterLeases[member.CharacterId] =
                    new CharacterLeaseRecord(
                        dungeonSessionId,
                        leaseExpiresAt);
            }
            session = session with
            {
                Members = members,
                UpdatedAt = now,
                ExpiresAt = leaseExpiresAt
            };
            _dungeonSessions[dungeonSessionId] = session;
            return Task.FromResult(session);
        }
    }

    public Task<DungeonSession> LeaveDungeonSessionAsync(
        Guid dungeonSessionId,
        string steamId,
        Guid characterId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            if (session.State != DungeonSessionState.Waiting)
            {
                throw new DungeonSessionNotJoinableException(dungeonSessionId);
            }

            int removed = session.Members.Count(member =>
                member.CharacterId == characterId
                && member.SteamId == steamId);
            if (removed == 0)
            {
                throw new DungeonSessionMembershipException(dungeonSessionId);
            }

            DungeonSessionMember[] members = session.Members
                .Where(member => member.CharacterId != characterId)
                .ToArray();
            if (_characterLeases.TryGetValue(
                    characterId,
                    out CharacterLeaseRecord? lease)
                && lease.DungeonSessionId == dungeonSessionId)
            {
                _characterLeases.Remove(characterId);
            }

            session = session with
            {
                Members = members,
                State = members.Length == 0
                    ? DungeonSessionState.Closed
                    : session.State,
                UpdatedAt = now,
                ExpiresAt = members.Length == 0 ? now : session.ExpiresAt
            };
            _dungeonSessions[dungeonSessionId] = session;
            return Task.FromResult(session);
        }
    }

    public Task<DungeonSession> AssignDungeonServerAsync(
        Guid dungeonSessionId,
        string serverId,
        string serverAddress,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            if (session.State == DungeonSessionState.Loading)
            {
                EnsureServerMatches(session, serverId);
                if (!string.Equals(
                    session.ServerAddress,
                    serverAddress,
                    StringComparison.Ordinal))
                {
                    throw new DungeonSessionServerMismatchException(
                        dungeonSessionId);
                }
                session = ExtendDungeonLease(session, now, leaseExpiresAt);
                return Task.FromResult(session);
            }

            if (session.State != DungeonSessionState.Waiting)
            {
                throw new DungeonSessionStateConflictException(
                    dungeonSessionId,
                    session.State);
            }

            session = ExtendDungeonLease(
                session with
                {
                    State = DungeonSessionState.Loading,
                    ServerId = serverId,
                    ServerAddress = serverAddress
                },
                now,
                leaseExpiresAt);
            return Task.FromResult(session);
        }
    }

    public Task<DungeonSession?> ClaimNextDungeonSessionAsync(
        string serverId,
        string serverAddress,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            DungeonSession? session = _dungeonSessions.Values
                .Where(candidate =>
                    candidate.State == DungeonSessionState.Waiting
                    && candidate.Members.Count > 0)
                .OrderBy(candidate => candidate.CreatedAt)
                .ThenBy(candidate => candidate.DungeonSessionId)
                .FirstOrDefault();
            if (session is null)
            {
                return Task.FromResult<DungeonSession?>(null);
            }

            session = ExtendDungeonLease(
                session with
                {
                    State = DungeonSessionState.Loading,
                    ServerId = serverId,
                    ServerAddress = serverAddress
                },
                now,
                leaseExpiresAt);
            return Task.FromResult<DungeonSession?>(session);
        }
    }

    public Task<DungeonSession> StartDungeonSessionAsync(
        Guid dungeonSessionId,
        string serverId,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            EnsureServerMatches(session, serverId);
            if (session.State is not (
                DungeonSessionState.Loading
                or DungeonSessionState.InProgress))
            {
                throw new DungeonSessionStateConflictException(
                    dungeonSessionId,
                    session.State);
            }

            session = ExtendDungeonLease(
                session with { State = DungeonSessionState.InProgress },
                now,
                leaseExpiresAt);
            return Task.FromResult(session);
        }
    }

    public Task<DungeonSession> HeartbeatDungeonSessionAsync(
        Guid dungeonSessionId,
        string serverId,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            EnsureServerMatches(session, serverId);
            if (session.State is not (
                DungeonSessionState.Loading
                or DungeonSessionState.InProgress))
            {
                throw new DungeonSessionStateConflictException(
                    dungeonSessionId,
                    session.State);
            }

            session = ExtendDungeonLease(session, now, leaseExpiresAt);
            return Task.FromResult(session);
        }
    }

    public Task<DungeonSession> FinishDungeonSessionAsync(
        Guid dungeonSessionId,
        string serverId,
        DungeonSessionState outcome,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            EnsureServerMatches(session, serverId);
            if (outcome is not (
                    DungeonSessionState.Cleared
                    or DungeonSessionState.Failed))
            {
                throw new DungeonSessionStateConflictException(
                    dungeonSessionId,
                    session.State);
            }

            if (session.State == outcome)
            {
                return Task.FromResult(session);
            }

            if (session.State is not (
                    DungeonSessionState.Loading
                    or DungeonSessionState.InProgress))
            {
                throw new DungeonSessionStateConflictException(
                    dungeonSessionId,
                    session.State);
            }

            foreach (DungeonSessionMember member in session.Members)
            {
                _characterLeases.Remove(member.CharacterId);
            }

            session = session with
            {
                State = outcome,
                UpdatedAt = now,
                ExpiresAt = now
            };
            _dungeonSessions[dungeonSessionId] = session;
            return Task.FromResult(session);
        }
    }

    public Task<DungeonRewardSettlement> EnqueueDungeonRewardSettlementAsync(
        Guid dungeonSessionId,
        string serverId,
        string rewardVersion,
        string commandFingerprint,
        IReadOnlyList<CurrencyChange> changes,
        IReadOnlyList<DungeonItemReward> itemRewards,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            EnsureServerMatches(session, serverId);

            if (_dungeonRewardSettlements.TryGetValue(
                dungeonSessionId,
                out DungeonRewardSettlement? existing))
            {
                if (!string.Equals(
                        existing.RewardVersion,
                        rewardVersion,
                        StringComparison.Ordinal)
                    || !string.Equals(
                        existing.CommandFingerprint,
                        commandFingerprint,
                        StringComparison.Ordinal))
                {
                    throw new DungeonRewardSettlementConflictException(
                        dungeonSessionId);
                }

                return Task.FromResult(existing);
            }

            if (session.State != DungeonSessionState.InProgress)
            {
                throw new DungeonSessionStateConflictException(
                    dungeonSessionId,
                    session.State);
            }

            DungeonRewardSettlement settlement = new(
                dungeonSessionId,
                serverId,
                rewardVersion,
                commandFingerprint,
                DungeonRewardSettlementState.Pending,
                session.Members.Select(member => member.CharacterId).ToArray(),
                changes.ToArray(),
                itemRewards.ToArray(),
                0,
                now,
                null,
                null,
                null,
                now,
                now,
                null);
            _dungeonRewardSettlements[dungeonSessionId] = settlement;
            _dungeonSessions[dungeonSessionId] = session with
            {
                State = DungeonSessionState.SettlementPending,
                UpdatedAt = now,
                ExpiresAt = now
            };
            return Task.FromResult(settlement);
        }
    }

    public Task<DungeonRewardSettlement?> ClaimNextDungeonRewardSettlementAsync(
        string workerId,
        DateTimeOffset now,
        DateTimeOffset processingExpiresAt,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            DungeonRewardSettlement? settlement = _dungeonRewardSettlements.Values
                .Where(candidate =>
                    (candidate.State == DungeonRewardSettlementState.Pending
                        && candidate.NextAttemptAt <= now)
                    || (candidate.State == DungeonRewardSettlementState.Processing
                        && candidate.ProcessingExpiresAt <= now))
                .OrderBy(candidate => candidate.NextAttemptAt)
                .ThenBy(candidate => candidate.CreatedAt)
                .FirstOrDefault();
            if (settlement is null)
            {
                return Task.FromResult<DungeonRewardSettlement?>(null);
            }

            settlement = settlement with
            {
                State = DungeonRewardSettlementState.Processing,
                AttemptCount = settlement.AttemptCount + 1,
                WorkerId = workerId,
                ProcessingExpiresAt = processingExpiresAt,
                UpdatedAt = now
            };
            _dungeonRewardSettlements[settlement.DungeonSessionId] = settlement;
            return Task.FromResult<DungeonRewardSettlement?>(settlement);
        }
    }

    public Task CompleteDungeonRewardSettlementAsync(
        Guid dungeonSessionId,
        string workerId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            DungeonRewardSettlement settlement =
                GetOwnedProcessingSettlement(dungeonSessionId, workerId);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            foreach (Guid characterId in settlement.CharacterIds)
            {
                _characterLeases.Remove(characterId);
            }

            _dungeonRewardSettlements[dungeonSessionId] = settlement with
            {
                State = DungeonRewardSettlementState.Completed,
                WorkerId = null,
                ProcessingExpiresAt = null,
                LastError = null,
                UpdatedAt = now,
                CompletedAt = now
            };
            _dungeonSessions[dungeonSessionId] = session with
            {
                State = DungeonSessionState.Cleared,
                Members = session.Members.Select(member =>
                    member with { LeaseExpiresAt = now }).ToArray(),
                UpdatedAt = now,
                ExpiresAt = now
            };
            return Task.CompletedTask;
        }
    }

    public Task FailDungeonRewardSettlementAsync(
        Guid dungeonSessionId,
        string workerId,
        string error,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            DungeonRewardSettlement settlement =
                GetOwnedProcessingSettlement(dungeonSessionId, workerId);
            DungeonSession session = GetRequiredDungeonSession(dungeonSessionId);
            foreach (Guid characterId in settlement.CharacterIds)
            {
                _characterLeases.Remove(characterId);
            }

            _dungeonRewardSettlements[dungeonSessionId] = settlement with
            {
                State = DungeonRewardSettlementState.Failed,
                WorkerId = null,
                ProcessingExpiresAt = null,
                LastError = error,
                UpdatedAt = now,
                CompletedAt = now
            };
            _dungeonSessions[dungeonSessionId] = session with
            {
                State = DungeonSessionState.Failed,
                Members = session.Members.Select(member =>
                    member with { LeaseExpiresAt = now }).ToArray(),
                UpdatedAt = now,
                ExpiresAt = now
            };
            return Task.CompletedTask;
        }
    }

    public Task RequeueDungeonRewardSettlementAsync(
        Guid dungeonSessionId,
        string workerId,
        string error,
        DateTimeOffset now,
        DateTimeOffset nextAttemptAt,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_dungeonGate)
        {
            DungeonRewardSettlement settlement =
                GetOwnedProcessingSettlement(dungeonSessionId, workerId);
            _dungeonRewardSettlements[dungeonSessionId] = settlement with
            {
                State = DungeonRewardSettlementState.Pending,
                NextAttemptAt = nextAttemptAt,
                WorkerId = null,
                ProcessingExpiresAt = null,
                LastError = error,
                UpdatedAt = now
            };
            return Task.CompletedTask;
        }
    }

    public Task<bool> IsActiveDungeonSessionMemberAsync(
        Guid dungeonSessionId,
        string steamId,
        Guid characterId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            bool isActive = _dungeonSessions.TryGetValue(
                    dungeonSessionId,
                    out DungeonSession? session)
                && session.State is (
                    DungeonSessionState.Loading
                    or DungeonSessionState.InProgress)
                && session.ServerId is not null
                && session.Members.Any(member =>
                    member.CharacterId == characterId
                    && member.SteamId == steamId
                    && member.LeaseExpiresAt > now)
                && _characterLeases.TryGetValue(
                    characterId,
                    out CharacterLeaseRecord? lease)
                && lease.DungeonSessionId == dungeonSessionId
                && lease.ExpiresAt > now;
            return Task.FromResult(isActive);
        }
    }

    public Task<bool> IsAuthorizedGameServerSessionMemberAsync(
        Guid dungeonSessionId,
        string serverId,
        string steamId,
        Guid characterId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            bool isAuthorized = _dungeonSessions.TryGetValue(
                    dungeonSessionId,
                    out DungeonSession? session)
                && session.State is (
                    DungeonSessionState.Loading
                    or DungeonSessionState.InProgress)
                && string.Equals(
                    session.ServerId,
                    serverId,
                    StringComparison.Ordinal)
                && session.ExpiresAt > now
                && session.Members.Any(member =>
                    member.CharacterId == characterId
                    && member.SteamId == steamId
                    && member.LeaseExpiresAt > now)
                && _characterLeases.TryGetValue(
                    characterId,
                    out CharacterLeaseRecord? lease)
                && lease.DungeonSessionId == dungeonSessionId
                && lease.ExpiresAt > now;
            return Task.FromResult(isAuthorized);
        }
    }

    public Task StoreJoinTicketAsync(
        string tokenHash,
        string steamId,
        Guid characterId,
        Guid dungeonSessionId,
        DateTimeOffset expiresAt,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            foreach ((string expiredHash, JoinTicketRecord expiredTicket) in _joinTickets)
            {
                if (expiredTicket.ExpiresAt <= DateTimeOffset.UtcNow)
                {
                    _joinTickets.TryRemove(expiredHash, out _);
                }
            }

            _joinTickets[tokenHash] = new JoinTicketRecord(
                steamId,
                characterId,
                dungeonSessionId,
                expiresAt);
        }
        return Task.CompletedTask;
    }

    public Task<ConsumedJoinTicket?> ConsumeJoinTicketAsync(
        string tokenHash,
        string serverId,
        Guid dungeonSessionId,
        DateTimeOffset now,
        CancellationToken cancellationToken)
    {
        lock (_dungeonGate)
        {
            CleanupExpiredDungeonSessions(now);
            if (!_joinTickets.TryGetValue(tokenHash, out JoinTicketRecord? ticket)
                || ticket.ExpiresAt <= now
                || ticket.DungeonSessionId != dungeonSessionId
                || !_dungeonSessions.TryGetValue(
                    ticket.DungeonSessionId,
                    out DungeonSession? session)
                || session.ServerId != serverId
                || session.State is not (
                    DungeonSessionState.Loading
                    or DungeonSessionState.InProgress)
                || !session.Members.Any(member =>
                    member.CharacterId == ticket.CharacterId
                    && member.SteamId == ticket.SteamId
                    && member.LeaseExpiresAt > now))
            {
                return Task.FromResult<ConsumedJoinTicket?>(null);
            }

            _joinTickets.TryRemove(tokenHash, out _);
            return Task.FromResult<ConsumedJoinTicket?>(new ConsumedJoinTicket(
                ticket.DungeonSessionId,
                ticket.CharacterId,
                ticket.SteamId));
        }
    }

    private void CleanupExpiredDungeonSessions(DateTimeOffset now)
    {
        foreach ((Guid sessionId, DungeonSession session) in _dungeonSessions.ToArray())
        {
            if (session.ExpiresAt <= now
                && session.State is (
                    DungeonSessionState.Waiting
                    or DungeonSessionState.Loading
                    or DungeonSessionState.InProgress))
            {
                _dungeonSessions[sessionId] = session with
                {
                    State = DungeonSessionState.Closed,
                    UpdatedAt = now,
                    ExpiresAt = now
                };
            }
        }

        foreach ((Guid characterId, CharacterLeaseRecord lease) in
            _characterLeases.ToArray())
        {
            bool sessionEnded = !_dungeonSessions.TryGetValue(
                    lease.DungeonSessionId,
                    out DungeonSession? session)
                || session.State is (
                    DungeonSessionState.Cleared
                    or DungeonSessionState.Failed
                    or DungeonSessionState.Closed);
            if (lease.ExpiresAt <= now || sessionEnded)
            {
                _characterLeases.Remove(characterId);
            }
        }
    }

    private void AcquireCharacterLease(
        Guid characterId,
        Guid requestedSessionId,
        DateTimeOffset now,
        DateTimeOffset expiresAt)
    {
        if (_characterLeases.TryGetValue(
                characterId,
                out CharacterLeaseRecord? lease)
            && lease.ExpiresAt > now
            && lease.DungeonSessionId != requestedSessionId)
        {
            throw new CharacterSessionConflictException(characterId);
        }

        if (lease is not null && lease.ExpiresAt <= now)
        {
            _characterLeases.Remove(characterId);
        }
    }

    private DungeonSession GetRequiredDungeonSession(Guid dungeonSessionId)
    {
        return _dungeonSessions.TryGetValue(
            dungeonSessionId,
            out DungeonSession? session)
                ? session
                : throw new DungeonSessionNotFoundException(dungeonSessionId);
    }

    private DungeonRewardSettlement GetOwnedProcessingSettlement(
        Guid dungeonSessionId,
        string workerId)
    {
        if (!_dungeonRewardSettlements.TryGetValue(
                dungeonSessionId,
                out DungeonRewardSettlement? settlement)
            || settlement.State != DungeonRewardSettlementState.Processing
            || !string.Equals(
                settlement.WorkerId,
                workerId,
                StringComparison.Ordinal))
        {
            throw new InvalidOperationException(
                $"Dungeon reward settlement '{dungeonSessionId}' is not owned by worker '{workerId}'.");
        }

        return settlement;
    }

    private static void EnsureServerMatches(
        DungeonSession session,
        string serverId)
    {
        if (!string.Equals(session.ServerId, serverId, StringComparison.Ordinal))
        {
            throw new DungeonSessionServerMismatchException(
                session.DungeonSessionId);
        }
    }

    private DungeonSession ExtendDungeonLease(
        DungeonSession session,
        DateTimeOffset now,
        DateTimeOffset leaseExpiresAt)
    {
        DungeonSessionMember[] members = session.Members
            .Select(member => member with { LeaseExpiresAt = leaseExpiresAt })
            .ToArray();
        foreach (DungeonSessionMember member in members)
        {
            _characterLeases[member.CharacterId] = new CharacterLeaseRecord(
                session.DungeonSessionId,
                leaseExpiresAt);
        }

        session = session with
        {
            Members = members,
            UpdatedAt = now,
            ExpiresAt = leaseExpiresAt
        };
        _dungeonSessions[session.DungeonSessionId] = session;
        return session;
    }


    public Task<IReadOnlyList<InventoryItem>> LoadInventoryAsync(
        Guid characterId,
        CancellationToken cancellationToken)
    {
        IReadOnlyList<InventoryItem> inventory = _inventories.TryGetValue(
            characterId, out IReadOnlyList<InventoryItem>? items)
            ? items
            : Array.Empty<InventoryItem>();
        return Task.FromResult(inventory);
    }

    public Task SaveInventoryAsync(
        Guid characterId,
        IReadOnlyList<InventoryItem> inventory,
        DateTimeOffset updatedAt,
        CancellationToken cancellationToken)
    {
        _inventories[characterId] = inventory
            .OrderBy(item => item.SlotIndex)
            .ToArray();
        return Task.CompletedTask;
    }
}
