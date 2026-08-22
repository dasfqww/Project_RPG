CREATE TABLE IF NOT EXISTS accounts (
    steam_id           VARCHAR(20) PRIMARY KEY,
    owner_steam_id     VARCHAR(20) NOT NULL,
    vac_banned         BOOLEAN NOT NULL DEFAULT FALSE,
    publisher_banned   BOOLEAN NOT NULL DEFAULT FALSE,
    created_at         TIMESTAMPTZ NOT NULL,
    last_login_at      TIMESTAMPTZ NOT NULL
);

-- A roster is the account-wide progression owner inside one game world.
-- The current game has one world ("main"), while keeping world_id explicit
-- avoids coupling characters and shared currencies directly to Steam IDs.
CREATE TABLE IF NOT EXISTS rosters (
    roster_id          UUID PRIMARY KEY,
    steam_id           VARCHAR(20) NOT NULL REFERENCES accounts(steam_id) ON DELETE CASCADE,
    world_id           VARCHAR(64) NOT NULL,
    created_at         TIMESTAMPTZ NOT NULL,
    updated_at         TIMESTAMPTZ NOT NULL,
    UNIQUE (steam_id, world_id)
);

CREATE INDEX IF NOT EXISTS ix_rosters_steam_id
    ON rosters(steam_id);

-- Upgrade accounts created before rosters existed. The MD5-derived UUID is
-- deterministic, so schema startup remains idempotent without an extension.
INSERT INTO rosters (roster_id, steam_id, world_id, created_at, updated_at)
SELECT
    MD5('project-rpg-roster:' || steam_id || ':main')::UUID,
    steam_id,
    'main',
    created_at,
    last_login_at
FROM accounts
ON CONFLICT (steam_id, world_id) DO NOTHING;

CREATE TABLE IF NOT EXISTS auth_sessions (
    token_hash         CHAR(64) PRIMARY KEY,
    steam_id           VARCHAR(20) NOT NULL REFERENCES accounts(steam_id) ON DELETE CASCADE,
    expires_at         TIMESTAMPTZ NOT NULL
);

CREATE INDEX IF NOT EXISTS ix_auth_sessions_expires_at
    ON auth_sessions(expires_at);

CREATE TABLE IF NOT EXISTS game_server_credentials (
    token_hash          CHAR(64) PRIMARY KEY,
    server_id           VARCHAR(128) NOT NULL,
    dungeon_session_id  UUID NOT NULL,
    expires_at          TIMESTAMPTZ NOT NULL
);

CREATE INDEX IF NOT EXISTS ix_game_server_credentials_session
    ON game_server_credentials(dungeon_session_id);

CREATE INDEX IF NOT EXISTS ix_game_server_credentials_expires_at
    ON game_server_credentials(expires_at);

CREATE TABLE IF NOT EXISTS characters (
    character_id       UUID PRIMARY KEY,
    roster_id          UUID REFERENCES rosters(roster_id) ON DELETE CASCADE,
    steam_id           VARCHAR(20) NOT NULL REFERENCES accounts(steam_id) ON DELETE CASCADE,
    name               VARCHAR(20) NOT NULL,
    created_at         TIMESTAMPTZ NOT NULL,
    updated_at         TIMESTAMPTZ NOT NULL
);

ALTER TABLE characters
    ADD COLUMN IF NOT EXISTS roster_id UUID
    REFERENCES rosters(roster_id) ON DELETE CASCADE;

UPDATE characters AS character
SET roster_id = roster.roster_id
FROM rosters AS roster
WHERE character.roster_id IS NULL
  AND roster.steam_id = character.steam_id
  AND roster.world_id = 'main';

ALTER TABLE characters
    ALTER COLUMN roster_id SET NOT NULL;

CREATE UNIQUE INDEX IF NOT EXISTS ux_characters_name_ci
    ON characters(LOWER(name));

CREATE INDEX IF NOT EXISTS ix_characters_steam_id
    ON characters(steam_id);

CREATE INDEX IF NOT EXISTS ix_characters_roster_id
    ON characters(roster_id);

CREATE TABLE IF NOT EXISTS dungeon_sessions (
    dungeon_session_id UUID PRIMARY KEY,
    dungeon_id         VARCHAR(64) NOT NULL,
    difficulty         VARCHAR(32) NOT NULL,
    state              VARCHAR(16) NOT NULL
                       CHECK (state IN (
                           'Waiting', 'Loading', 'InProgress',
                           'SettlementPending',
                           'Cleared', 'Failed', 'Closed')),
    server_id          VARCHAR(128),
    server_address     VARCHAR(255),
    created_at         TIMESTAMPTZ NOT NULL,
    updated_at         TIMESTAMPTZ NOT NULL,
    expires_at         TIMESTAMPTZ NOT NULL
);

ALTER TABLE dungeon_sessions
    ADD COLUMN IF NOT EXISTS server_address VARCHAR(255);

-- Replace the original state check when upgrading an existing database.
ALTER TABLE dungeon_sessions
    DROP CONSTRAINT IF EXISTS dungeon_sessions_state_check;
ALTER TABLE dungeon_sessions
    ADD CONSTRAINT dungeon_sessions_state_check
    CHECK (state IN (
        'Waiting', 'Loading', 'InProgress', 'SettlementPending',
        'Cleared', 'Failed', 'Closed'));

ALTER TABLE game_server_credentials
    DROP CONSTRAINT IF EXISTS fk_game_server_credentials_session;
ALTER TABLE game_server_credentials
    ADD CONSTRAINT fk_game_server_credentials_session
    FOREIGN KEY (dungeon_session_id)
    REFERENCES dungeon_sessions(dungeon_session_id) ON DELETE CASCADE;

CREATE INDEX IF NOT EXISTS ix_dungeon_sessions_state_expires_at
    ON dungeon_sessions(state, expires_at);

CREATE TABLE IF NOT EXISTS dungeon_session_members (
    dungeon_session_id UUID NOT NULL
                       REFERENCES dungeon_sessions(dungeon_session_id) ON DELETE CASCADE,
    character_id       UUID NOT NULL
                       REFERENCES characters(character_id) ON DELETE CASCADE,
    steam_id           VARCHAR(20) NOT NULL
                       REFERENCES accounts(steam_id) ON DELETE CASCADE,
    joined_at          TIMESTAMPTZ NOT NULL,
    lease_expires_at   TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (dungeon_session_id, character_id)
);

CREATE INDEX IF NOT EXISTS ix_dungeon_session_members_character_id
    ON dungeon_session_members(character_id);

CREATE TABLE IF NOT EXISTS dungeon_reward_settlements (
    dungeon_session_id     UUID PRIMARY KEY
                           REFERENCES dungeon_sessions(dungeon_session_id)
                           ON DELETE CASCADE,
    server_id              VARCHAR(128) NOT NULL,
    reward_version         VARCHAR(64) NOT NULL,
    command_fingerprint    CHAR(64) NOT NULL,
    state                  VARCHAR(16) NOT NULL
                           CHECK (state IN (
                               'Pending', 'Processing',
                               'Completed', 'Failed')),
    character_ids          JSONB NOT NULL
                           CHECK (jsonb_typeof(character_ids) = 'array'),
    currency_changes       JSONB NOT NULL
                           CHECK (jsonb_typeof(currency_changes) = 'array'),
    item_rewards           JSONB NOT NULL DEFAULT '[]'::JSONB
                           CONSTRAINT ck_dungeon_reward_settlements_item_rewards_array
                           CHECK (jsonb_typeof(item_rewards) = 'array'),
    attempt_count          INTEGER NOT NULL DEFAULT 0
                           CHECK (attempt_count >= 0),
    next_attempt_at        TIMESTAMPTZ NOT NULL,
    worker_id              VARCHAR(128),
    processing_expires_at  TIMESTAMPTZ,
    last_error             VARCHAR(512),
    created_at             TIMESTAMPTZ NOT NULL,
    updated_at             TIMESTAMPTZ NOT NULL,
    completed_at           TIMESTAMPTZ
);

ALTER TABLE dungeon_reward_settlements
    ADD COLUMN IF NOT EXISTS item_rewards JSONB NOT NULL DEFAULT '[]'::JSONB;

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM pg_constraint
        WHERE conname = 'ck_dungeon_reward_settlements_item_rewards_array'
          AND conrelid = 'dungeon_reward_settlements'::regclass)
    THEN
        ALTER TABLE dungeon_reward_settlements
            ADD CONSTRAINT ck_dungeon_reward_settlements_item_rewards_array
            CHECK (jsonb_typeof(item_rewards) = 'array');
    END IF;
END
$$;

CREATE INDEX IF NOT EXISTS ix_dungeon_reward_settlements_due
    ON dungeon_reward_settlements(
        state, next_attempt_at, processing_expires_at);

CREATE TABLE IF NOT EXISTS character_session_leases (
    character_id       UUID PRIMARY KEY
                       REFERENCES characters(character_id) ON DELETE CASCADE,
    dungeon_session_id UUID NOT NULL
                       REFERENCES dungeon_sessions(dungeon_session_id) ON DELETE CASCADE,
    expires_at         TIMESTAMPTZ NOT NULL,
    updated_at         TIMESTAMPTZ NOT NULL
);

CREATE INDEX IF NOT EXISTS ix_character_session_leases_expires_at
    ON character_session_leases(expires_at);

CREATE TABLE IF NOT EXISTS game_join_tickets (
    token_hash         CHAR(64) PRIMARY KEY,
    steam_id           VARCHAR(20) NOT NULL REFERENCES accounts(steam_id) ON DELETE CASCADE,
    character_id       UUID NOT NULL REFERENCES characters(character_id) ON DELETE CASCADE,
    dungeon_session_id UUID NOT NULL
                       REFERENCES dungeon_sessions(dungeon_session_id) ON DELETE CASCADE,
    expires_at         TIMESTAMPTZ NOT NULL
);

-- Upgrade the earlier vertical-slice table. Old tickets are intentionally
-- invalidated because they were not bound to a dungeon session.
ALTER TABLE game_join_tickets
    ADD COLUMN IF NOT EXISTS dungeon_session_id UUID
    REFERENCES dungeon_sessions(dungeon_session_id) ON DELETE CASCADE;
DELETE FROM game_join_tickets
    WHERE dungeon_session_id IS NULL;
ALTER TABLE game_join_tickets
    ALTER COLUMN dungeon_session_id SET NOT NULL;

CREATE INDEX IF NOT EXISTS ix_game_join_tickets_expires_at
    ON game_join_tickets(expires_at);

CREATE TABLE IF NOT EXISTS inventory_items (
    character_id       UUID NOT NULL REFERENCES characters(character_id) ON DELETE CASCADE,
    slot_index         INTEGER NOT NULL,
    item_id            VARCHAR(128) NOT NULL,
    quantity           INTEGER NOT NULL CHECK (quantity > 0),
    category           VARCHAR(64) NOT NULL DEFAULT '',
    instance_id        VARCHAR(128) NOT NULL DEFAULT '',
    updated_at         TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (character_id, slot_index)
);

CREATE UNIQUE INDEX IF NOT EXISTS ux_inventory_instance_id
    ON inventory_items(instance_id)
    WHERE instance_id <> '';

-- Authoritative MMORPG item records. The legacy inventory_items table remains
-- available while existing clients migrate to revisioned item transactions.
CREATE TABLE IF NOT EXISTS item_records (
    item_id             UUID PRIMARY KEY,
    definition_type     VARCHAR(64) NOT NULL,
    definition_name     VARCHAR(128) NOT NULL,
    definition_version  INTEGER NOT NULL CHECK (definition_version > 0),
    owner_type          VARCHAR(16) NOT NULL
                        CHECK (owner_type IN (
                            'Character', 'Account', 'System', 'World')),
    owner_id            VARCHAR(128) NOT NULL,
    container_type      VARCHAR(32) NOT NULL
                        CHECK (container_type IN (
                            'Inventory', 'Equipment', 'CharacterStorage',
                            'AccountStorage', 'Mail', 'Trade', 'Auction',
                            'World', 'Terminal')),
    container_id        VARCHAR(128) NOT NULL,
    slot_index          INTEGER NOT NULL,
    generation_seed     INTEGER NOT NULL,
    quantity            INTEGER NOT NULL CHECK (quantity >= 0),
    instance_tags       JSONB NOT NULL DEFAULT '[]'::JSONB
                        CHECK (jsonb_typeof(instance_tags) = 'array'),
    stat_values         JSONB NOT NULL DEFAULT '[]'::JSONB
                        CHECK (jsonb_typeof(stat_values) = 'array'),
    revision            BIGINT NOT NULL CHECK (revision > 0),
    lifecycle_state     VARCHAR(16) NOT NULL
                        CHECK (lifecycle_state IN (
                            'Active', 'Consumed', 'Destroyed', 'Expired')),
    bind_state          VARCHAR(20) NOT NULL
                        CHECK (bind_state IN (
                            'Unbound', 'BindOnEquip',
                            'CharacterBound', 'AccountBound')),
    durability_current  INTEGER NOT NULL DEFAULT 0,
    durability_maximum  INTEGER NOT NULL DEFAULT 0,
    expires_at          TIMESTAMPTZ,
    creation_source     VARCHAR(128) NOT NULL DEFAULT '',
    is_locked           BOOLEAN NOT NULL DEFAULT FALSE,
    created_at          TIMESTAMPTZ NOT NULL,
    updated_at          TIMESTAMPTZ NOT NULL,
    CHECK (
        durability_maximum >= 0
        AND durability_current >= 0
        AND durability_current <= durability_maximum),
    CHECK (
        (lifecycle_state = 'Active'
         AND quantity > 0
         AND container_type <> 'Terminal'
         AND container_id <> ''
         AND slot_index >= 0)
        OR
        (lifecycle_state <> 'Active'
         AND quantity = 0
         AND container_type = 'Terminal'
         AND container_id = ''
         AND slot_index = -1))
);

CREATE INDEX IF NOT EXISTS ix_item_records_owner
    ON item_records(owner_type, owner_id, lifecycle_state);

CREATE UNIQUE INDEX IF NOT EXISTS ux_item_records_active_location
    ON item_records(
        owner_type, owner_id, container_type, container_id, slot_index)
    WHERE lifecycle_state = 'Active';

CREATE TABLE IF NOT EXISTS item_transaction_receipts (
    request_id           UUID PRIMARY KEY,
    operation            VARCHAR(64) NOT NULL,
    command_fingerprint  VARCHAR(512) NOT NULL,
    actor_type           VARCHAR(16) NOT NULL
                         CHECK (actor_type IN (
                             'Character', 'Account', 'System', 'World')),
    actor_id             VARCHAR(128) NOT NULL,
    affected_quantity    INTEGER NOT NULL CHECK (affected_quantity >= 0),
    result_records       JSONB NOT NULL
                         CHECK (jsonb_typeof(result_records) = 'array'),
    committed_at         TIMESTAMPTZ NOT NULL
);

CREATE INDEX IF NOT EXISTS ix_item_transaction_receipts_committed_at
    ON item_transaction_receipts(committed_at);

-- Currency definitions decide ownership scope. Callers provide a character
-- context, and the backend resolves Account/Roster/Character ownership.
CREATE TABLE IF NOT EXISTS currency_definitions (
    currency_code       VARCHAR(64) PRIMARY KEY,
    display_name        VARCHAR(128) NOT NULL,
    scope               VARCHAR(16) NOT NULL
                        CHECK (scope IN ('Account', 'Roster', 'Character')),
    max_balance         BIGINT NOT NULL CHECK (max_balance > 0),
    enabled             BOOLEAN NOT NULL DEFAULT TRUE,
    created_at          TIMESTAMPTZ NOT NULL,
    updated_at          TIMESTAMPTZ NOT NULL
);

CREATE TABLE IF NOT EXISTS currency_balances (
    owner_type          VARCHAR(16) NOT NULL
                        CHECK (owner_type IN ('Account', 'Roster', 'Character')),
    owner_id            VARCHAR(128) NOT NULL,
    currency_code       VARCHAR(64) NOT NULL
                        REFERENCES currency_definitions(currency_code),
    balance             BIGINT NOT NULL CHECK (balance >= 0),
    revision            BIGINT NOT NULL CHECK (revision >= 0),
    updated_at          TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (owner_type, owner_id, currency_code)
);

CREATE INDEX IF NOT EXISTS ix_currency_balances_code
    ON currency_balances(currency_code);

-- Successful commands are retained as immutable receipts. Replaying the same
-- request_id returns this result; reusing it for another command is rejected.
CREATE TABLE IF NOT EXISTS currency_transaction_receipts (
    request_id           UUID PRIMARY KEY,
    character_id         UUID NOT NULL REFERENCES characters(character_id) ON DELETE RESTRICT,
    operation            VARCHAR(64) NOT NULL,
    command_fingerprint  VARCHAR(512) NOT NULL,
    reason               VARCHAR(128) NOT NULL,
    result_changes       JSONB NOT NULL
                         CHECK (jsonb_typeof(result_changes) = 'array'),
    committed_at         TIMESTAMPTZ NOT NULL
);

CREATE INDEX IF NOT EXISTS ix_currency_transaction_receipts_character
    ON currency_transaction_receipts(character_id, committed_at);
