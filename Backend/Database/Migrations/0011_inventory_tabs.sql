-- Run during deployment with game servers drained. Replaying development initialization
-- only touches owners still using the old shared layout; receipts remain immutable.
LOCK TABLE item_records IN ACCESS EXCLUSIVE MODE;
CREATE TEMP TABLE inventory_tab_upgrade ON COMMIT DROP AS
WITH classified AS (
    SELECT item_id, owner_type, owner_id, container_id, slot_index, revision,
        CASE WHEN definition_name LIKE 'GameItem.Equipment.%' THEN 0
             WHEN definition_name LIKE 'GameItem.Consume.%' THEN 1 ELSE 2 END AS tab
    FROM item_records WHERE lifecycle_state = 'Active' AND container_type = 'Inventory'
        AND owner_type = 'Character' AND container_id = owner_id
), owners AS (
    SELECT DISTINCT owner_type, owner_id, container_id FROM classified
    WHERE slot_index < tab * 80 OR slot_index >= (tab + 1) * 80
)
SELECT c.item_id, c.revision, c.tab * 80 +
    (row_number() OVER (PARTITION BY c.owner_type, c.owner_id, c.container_id, c.tab
                       ORDER BY c.slot_index, c.item_id) - 1)::integer AS new_slot,
    count(*) OVER (PARTITION BY c.owner_type, c.owner_id, c.container_id, c.tab) AS tab_count
FROM classified c JOIN owners o USING (owner_type, owner_id, container_id);
DO $$ BEGIN
    IF EXISTS (SELECT 1 FROM inventory_tab_upgrade WHERE tab_count > 80 OR revision = 9223372036854775807) THEN
        RAISE EXCEPTION 'Inventory tab upgrade requires capacity/revision repair; no item was changed';
    END IF;
    IF EXISTS (SELECT 1 FROM trade_reserved_items r JOIN inventory_tab_upgrade u USING (item_id)
        JOIN trade_asset_reservations a USING (reservation_id) WHERE a.state = 'Active' AND a.expires_at > CURRENT_TIMESTAMP) THEN
        RAISE EXCEPTION 'Settle or cancel active trades before the inventory tab upgrade';
    END IF;
END $$;
-- The existing immediate unique index cannot observe the intermediate permutation.
-- Drop/recreate under the exclusive table lock, within the migration transaction.
DROP INDEX ux_item_records_active_location;
UPDATE item_records i SET slot_index = u.new_slot, revision = i.revision + 1,
    updated_at = CURRENT_TIMESTAMP
FROM inventory_tab_upgrade u WHERE i.item_id = u.item_id AND i.slot_index <> u.new_slot;
CREATE UNIQUE INDEX ux_item_records_active_location ON item_records
    (owner_type, owner_id, container_type, container_id, slot_index) WHERE lifecycle_state = 'Active';
