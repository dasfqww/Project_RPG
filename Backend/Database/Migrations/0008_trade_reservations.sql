-- Persistent holds and release tombstones. Expiry is evaluated under asset locks, not by a timer alone.
CREATE TABLE IF NOT EXISTS trade_asset_reservations (
    reservation_id UUID PRIMARY KEY,
    trade_id UUID NOT NULL,
    character_id UUID NOT NULL REFERENCES characters(character_id),
    account_id VARCHAR(20) NOT NULL REFERENCES accounts(steam_id),
    fingerprint VARCHAR(64) NOT NULL CHECK (fingerprint ~ '^[0-9a-f]{64}$'),
    command_payload JSONB NOT NULL CHECK (jsonb_typeof(command_payload) = 'object'),
    state VARCHAR(16) NOT NULL CHECK (state IN ('Active','Released')),
    expires_at TIMESTAMPTZ NOT NULL,
    wallet_owner_type VARCHAR(16),
    wallet_owner_id VARCHAR(128),
    currency_code VARCHAR(64) NOT NULL,
    currency_amount BIGINT NOT NULL CHECK (currency_amount >= 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    CHECK ((wallet_owner_type IS NULL AND wallet_owner_id IS NULL AND state='Released')
        OR (wallet_owner_type IS NOT NULL AND wallet_owner_id IS NOT NULL
            AND wallet_owner_type IN ('Account','Roster','Character')))
);
CREATE INDEX IF NOT EXISTS ix_trade_reservation_character ON trade_asset_reservations(character_id,expires_at) WHERE state='Active';
CREATE INDEX IF NOT EXISTS ix_trade_reservation_wallet ON trade_asset_reservations(wallet_owner_type,wallet_owner_id,currency_code,expires_at) WHERE state='Active';
CREATE TABLE IF NOT EXISTS trade_reserved_items (
    reservation_id UUID NOT NULL REFERENCES trade_asset_reservations(reservation_id),
    item_id UUID NOT NULL REFERENCES item_records(item_id),
    PRIMARY KEY (reservation_id,item_id)
);
CREATE INDEX IF NOT EXISTS ix_trade_reserved_item ON trade_reserved_items(item_id,reservation_id);
