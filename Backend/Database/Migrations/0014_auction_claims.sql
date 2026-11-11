CREATE TABLE IF NOT EXISTS auction_claim_receipts (
    request_id UUID PRIMARY KEY,
    listing_id UUID NOT NULL REFERENCES auction_seller_payables(listing_id),
    actor_account VARCHAR(20) NOT NULL REFERENCES accounts(steam_id),
    actor_character UUID NOT NULL REFERENCES characters(character_id),
    fingerprint VARCHAR(64) NOT NULL CHECK (fingerprint ~ '^[0-9a-f]{64}$'),
    paid BIGINT NOT NULL CHECK (paid >= 0),
    result JSONB NOT NULL CHECK (jsonb_typeof(result)='object'),
    committed_at TIMESTAMPTZ NOT NULL
);
CREATE INDEX IF NOT EXISTS ix_auction_claim_listing ON auction_claim_receipts(listing_id);
