#pragma once

#include "Progression/RPGEquipmentMutationTypes.h"

enum class ERPGEquipmentJournalRead : uint8 { Missing, Ready, Busy, Invalid, Unavailable };

struct PROJECT_RPG_API FRPGEquipmentPendingMutation
{
	FRPGEquipmentMutationScope Scope;
	FRPGEquipmentMutationCommand Command;
	FString Body;
	FString Fingerprint;
};

/** Client intent storage, NOT authorization. Never stores a bearer/service token.
 * Windows exclusive file sharing serializes cooperating processes. Every clear
 * compares the full original command under that same lock, not a read/delete race.
 */
class PROJECT_RPG_API FRPGEquipmentMutationJournal final
{
public:
	static ERPGEquipmentJournalRead Load(const FString& Root, const FRPGEquipmentMutationScope& Scope,
		FRPGEquipmentPendingMutation& OutPending);
	/** Never replaces an existing intent, including corrupt/unknown-version files. */
	static bool Create(const FString& Root, const FRPGEquipmentMutationScope& Scope,
		const FRPGEquipmentMutationCommand& Command);
	/** Only after a matching receipt or a proven definitive rejection. */
	static bool Clear(const FString& Root, const FRPGEquipmentMutationScope& Scope,
		const FRPGEquipmentMutationCommand& Expected);
};
