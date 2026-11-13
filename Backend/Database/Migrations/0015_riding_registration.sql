CREATE TABLE IF NOT EXISTS riding_roster_state (
    roster_id UUID PRIMARY KEY REFERENCES rosters(roster_id) ON DELETE CASCADE,
    revision BIGINT NOT NULL DEFAULT 0 CHECK (revision >= 0)
);
CREATE TABLE IF NOT EXISTS riding_command_receipts (
    request_id UUID PRIMARY KEY,
    operation VARCHAR(64) NOT NULL CHECK (operation = 'Riding.Register'),
    character_id UUID NOT NULL REFERENCES characters(character_id) ON DELETE CASCADE,
    roster_id UUID NOT NULL REFERENCES rosters(roster_id) ON DELETE CASCADE,
    canonical_fingerprint CHAR(64) NOT NULL CHECK (canonical_fingerprint ~ '^[0-9a-f]{64}$'),
    outcome VARCHAR(32) NOT NULL CHECK (outcome IN ('Registered', 'AlreadyOwned')),
    result_payload JSONB NOT NULL CHECK (jsonb_typeof(result_payload) = 'object'),
    committed_at_utc TIMESTAMPTZ NOT NULL,
    child_item_request_id UUID REFERENCES item_transaction_receipts(request_id),
    CHECK ((outcome = 'Registered') = (child_item_request_id IS NOT NULL))
);
CREATE TABLE IF NOT EXISTS riding_mount_unlocks (
    roster_id UUID NOT NULL REFERENCES riding_roster_state(roster_id) ON DELETE CASCADE,
    mount_id VARCHAR(64) NOT NULL CHECK (mount_id ~ '^[a-z0-9][a-z0-9_.-]{0,63}$'),
    definition_version INTEGER NOT NULL CHECK (definition_version > 0),
    unlocked_at_utc TIMESTAMPTZ NOT NULL,
    source_request_id UUID NOT NULL REFERENCES riding_command_receipts(request_id) DEFERRABLE INITIALLY DEFERRED,
    PRIMARY KEY (roster_id, mount_id)
);
