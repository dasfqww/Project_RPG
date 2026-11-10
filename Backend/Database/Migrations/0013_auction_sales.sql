CREATE TABLE IF NOT EXISTS auction_sales (
    listing_id UUID PRIMARY KEY REFERENCES auction_escrow_listings(listing_id),
    request_id UUID NOT NULL UNIQUE,
    buyer_account VARCHAR(20) NOT NULL REFERENCES accounts(steam_id),
    buyer_character UUID NOT NULL REFERENCES characters(character_id),
    currency_code VARCHAR(64) NOT NULL REFERENCES currency_definitions(currency_code),
    price BIGINT NOT NULL CHECK (price > 0),
    fee BIGINT NOT NULL CHECK (fee >= 0),
    seller_amount BIGINT NOT NULL CHECK (seller_amount >= 0),
    fingerprint VARCHAR(64) NOT NULL CHECK (fingerprint ~ '^[0-9a-f]{64}$'),
    result JSONB NOT NULL CHECK (jsonb_typeof(result)='object'),
    committed_at TIMESTAMPTZ NOT NULL,
    CHECK (price = fee + seller_amount)
);
CREATE TABLE IF NOT EXISTS auction_seller_payables (
    listing_id UUID PRIMARY KEY REFERENCES auction_sales(listing_id),
    seller_account VARCHAR(20) NOT NULL REFERENCES accounts(steam_id),
    seller_character UUID NOT NULL REFERENCES characters(character_id),
    currency_code VARCHAR(64) NOT NULL REFERENCES currency_definitions(currency_code),
    wallet_owner_type VARCHAR(16) NOT NULL CHECK (wallet_owner_type IN ('Account','Roster','Character')),
    wallet_owner_id VARCHAR(128) NOT NULL,
    initial_amount BIGINT NOT NULL CHECK (initial_amount >= 0),
    remaining_amount BIGINT NOT NULL CHECK (remaining_amount >= 0 AND remaining_amount <= initial_amount),
    revision BIGINT NOT NULL CHECK (revision > 0),
    created_at TIMESTAMPTZ NOT NULL
);
CREATE INDEX IF NOT EXISTS ix_auction_payables_seller ON auction_seller_payables(seller_account,currency_code,listing_id) WHERE remaining_amount > 0;
