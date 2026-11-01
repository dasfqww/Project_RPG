-- Version 2: optional per-instance growth. Legacy rows remain SQL NULL.
-- Idempotent as well as versioned so Development/Testing initialization can replay it.
ALTER TABLE item_records
    ADD COLUMN IF NOT EXISTS equipment_progression JSONB;

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conrelid = 'item_records'::regclass
          AND conname = 'ck_item_equipment_progression'
    ) THEN
        ALTER TABLE item_records
            ADD CONSTRAINT ck_item_equipment_progression CHECK (
                equipment_progression IS NULL OR (
                    jsonb_typeof(equipment_progression) = 'object'
                    AND equipment_progression ->> 'growthBandId' = 'FirstSlice'
                    AND jsonb_typeof(equipment_progression -> 'ruleVersion') = 'number'
                    AND equipment_progression ->> 'ruleVersion' = '1'
                    AND jsonb_typeof(equipment_progression -> 'level') = 'number'
                    AND equipment_progression ->> 'level' IN ('0', '1', '2', '3')
                    AND quantity IN (0, 1)
                ) IS TRUE
            );
    END IF;
END;
$$;
