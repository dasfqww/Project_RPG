#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/PrimaryAssetId.h"

class URPGPlayerClassDefinition;
class URPGPlayerSkillComponent;
struct FRPGPlayerSkillRestoreData;

struct FRPGSavedProgressionSkill
{
	FGameplayTag SkillTag;
	int32 Level = 0;
	TArray<FString> TripodIds;
};

/** Validated backend record; never a client command or a replacement for the skill state owner. */
struct PROJECT_RPG_API FRPGCharacterProgressionSnapshot
{
	FString CharacterId;
	FString ProfileId;
	FPrimaryAssetId ClassId;
	int64 Revision = 0;
	int32 Experience = 0;
	int32 Level = 0;
	int32 TotalSP = 0;
	int32 UsedSP = 0;
	int32 Milestones = 0;
	bool bIdentityUnlocked = false;
	TArray<FRPGSavedProgressionSkill> Skills;
	TArray<FGameplayTag> SkillSlots;

	bool BuildSkillRestoreData(const URPGPlayerClassDefinition& Definition,
		const URPGPlayerSkillComponent& Growth, FRPGPlayerSkillRestoreData& OutData, FText& OutError) const;
};

struct FRPGCharacterProgressionResult
{
	bool bSuccessful = false;
	/** A write may have committed even when no usable result reached the client. */
	bool bOutcomeUnknown = false;
	int32 HttpStatus = 0;
	FString Error;
	FRPGCharacterProgressionSnapshot Snapshot;
};

/** Local frontend draft command. Never accepted as a Pawn restore record. */
struct FRPGCharacterSkillConfiguration
{
	FGuid RequestId;
	int64 ExpectedRevision = 0;
	TArray<FRPGSavedProgressionSkill> Skills;
	TArray<FGameplayTag> SkillSlots;
};

class PROJECT_RPG_API FRPGCharacterProgressionSnapshotCodec
{
public:
	static bool Decode(const FString& Body, const FString& ExpectedCharacterId,
		FRPGCharacterProgressionSnapshot& OutSnapshot, FString& OutError);
	static bool EncodeConfiguration(const FRPGCharacterSkillConfiguration& Command, FString& OutBody);
};
