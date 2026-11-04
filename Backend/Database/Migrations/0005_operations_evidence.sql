-- Append-only read indexes. Gameplay rows and existing migration SQL stay intact.
CREATE INDEX IF NOT EXISTS ix_ops_currency_receipt_page
    ON currency_transaction_receipts(character_id, committed_at DESC, request_id DESC);
CREATE INDEX IF NOT EXISTS ix_ops_item_receipt_actor_page
    ON item_transaction_receipts(actor_type, actor_id, committed_at DESC, request_id DESC);
CREATE INDEX IF NOT EXISTS ix_ops_item_receipt_result_owner
    ON item_transaction_receipts USING GIN(result_records jsonb_path_ops);
CREATE INDEX IF NOT EXISTS ix_ops_settlement_characters
    ON dungeon_reward_settlements USING GIN(character_ids jsonb_path_ops);
CREATE INDEX IF NOT EXISTS ix_ops_settlement_time
    ON dungeon_reward_settlements(updated_at DESC, dungeon_session_id DESC);
