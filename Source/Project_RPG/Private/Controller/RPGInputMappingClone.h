#pragma once

#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "UObject/UObjectGlobals.h"

namespace RPGInputMappingClone
{
	// This authored context loses migrated default records in its UE 5.8 runtime
	// duplicate. Repair only an empty copy, before saved player overrides run.
	// Keep the duplicate's context metadata and never overwrite live bindings.
	inline bool RestoreMissingRecords(const UInputMappingContext& Source, UInputMappingContext& Copy)
	{
		if (&Source == &Copy || Source.GetMappings().IsEmpty() || !Copy.GetMappings().IsEmpty()) return false;
		for (const FEnhancedActionKeyMapping& Original : Source.GetMappings())
		{
			FEnhancedActionKeyMapping& Record = Copy.MapKey(Original.Action, Original.Key);
			Record = Original;
			for (auto& Modifier : Record.Modifiers)
				if (Modifier) Modifier = DuplicateObject<UInputModifier>(Modifier, &Copy,
					MakeUniqueObjectName(&Copy, Modifier->GetClass(), Modifier->GetFName()));
			for (auto& Trigger : Record.Triggers)
				if (Trigger) Trigger = DuplicateObject<UInputTrigger>(Trigger, &Copy,
					MakeUniqueObjectName(&Copy, Trigger->GetClass(), Trigger->GetFName()));
		}
		return true;
	}
}
