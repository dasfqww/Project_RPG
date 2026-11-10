CREATE TABLE IF NOT EXISTS auction_escrow_listings (
    listing_id UUID PRIMARY KEY,
    seller_account VARCHAR(20) NOT NULL REFERENCES accounts(steam_id),
    seller_character UUID NOT NULL REFERENCES characters(character_id),
    escrow_item UUID NOT NULL REFERENCES item_records(item_id),
    expires_at TIMESTAMPTZ NOT NULL,
    revision BIGINT NOT NULL CHECK (revision > 0),
    closed BOOLEAN NOT NULL,
    payload JSONB NOT NULL CHECK (jsonb_typeof(payload)='object')
);
CREATE UNIQUE INDEX IF NOT EXISTS ux_auction_escrow_active_item ON auction_escrow_listings(escrow_item) WHERE NOT closed;
CREATE INDEX IF NOT EXISTS ix_auction_escrow_account ON auction_escrow_listings(seller_account) WHERE NOT closed;
CREATE INDEX IF NOT EXISTS ix_auction_escrow_expiry ON auction_escrow_listings(expires_at,listing_id) WHERE NOT closed;
CREATE TABLE IF NOT EXISTS auction_escrow_receipts (
    request_id UUID PRIMARY KEY,
    listing_id UUID NOT NULL REFERENCES auction_escrow_listings(listing_id),
    actor_account VARCHAR(20) NOT NULL,
    actor_character UUID NOT NULL,
    fingerprint VARCHAR(64) NOT NULL CHECK (fingerprint ~ '^[0-9a-f]{64}$'),
    result JSONB NOT NULL CHECK (jsonb_typeof(result)='object'),
    committed_at TIMESTAMPTZ NOT NULL
);
