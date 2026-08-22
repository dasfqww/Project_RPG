#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Type/RPGEnumTypes.h"
#include "UObject/PrimaryAssetId.h"

class URPGItemDefinition;
class UTexture2D;

/** Immutable subset of definition data required by authoritative transactions. */
struct PROJECT_RPG_API FRPGItemDefinitionSnapshot
{
	FPrimaryAssetId DefinitionId;
	int32 DefinitionVersion = 0;
	int32 MaxStackSize = 1;

	bool IsValid() const
	{
		return DefinitionId.IsValid() &&
			DefinitionVersion > 0 &&
			MaxStackSize > 0;
	}
};

/** UI-facing immutable definition data, independent of the authoring source. */
struct PROJECT_RPG_API FRPGItemDefinitionView
{
	FRPGItemDefinitionSnapshot Snapshot;
	FGameplayTag ItemTag;
	EItemCategory ItemCategory = EItemCategory::None;
	FText DisplayName;
	FText Description;
	TSoftObjectPtr<UTexture2D> Icon;
	FIntPoint GridSize = FIntPoint(1, 1);
	bool bLegacySource = false;

	bool IsValid() const
	{
		return Snapshot.IsValid() &&
			ItemTag.IsValid() &&
			ItemCategory != EItemCategory::None &&
			!DisplayName.IsEmpty() &&
			GridSize.X > 0 &&
			GridSize.Y > 0;
	}
};

/** Read-only dependency used by transactions instead of loading assets directly. */
class PROJECT_RPG_API IRPGItemDefinitionCatalog
{
public:
	virtual ~IRPGItemDefinitionCatalog() = default;

	virtual bool TryFindDefinition(
		const FPrimaryAssetId& DefinitionId,
		FRPGItemDefinitionSnapshot& OutSnapshot) const = 0;
};

/** Narrow presentation dependency used by UI and pickup presentation code. */
class PROJECT_RPG_API IRPGItemDefinitionViewCatalog
{
public:
	virtual ~IRPGItemDefinitionViewCatalog() = default;

	virtual bool TryFindDefinitionView(
		const FPrimaryAssetId& DefinitionId,
		FRPGItemDefinitionView& OutView) const = 0;
};

/**
 * Thread-safe server-side catalog populated during startup or asset loading.
 * Transaction execution never performs a synchronous asset load.
 */
class PROJECT_RPG_API FRPGItemDefinitionRegistry final
	: public IRPGItemDefinitionCatalog,
	  public IRPGItemDefinitionViewCatalog
{
public:
	bool RegisterDefinition(const URPGItemDefinition& Definition);
	bool RegisterDefinitionView(const FRPGItemDefinitionView& View);
	bool RegisterSnapshot(const FRPGItemDefinitionSnapshot& Snapshot);
	void Reset();

	virtual bool TryFindDefinition(
		const FPrimaryAssetId& DefinitionId,
		FRPGItemDefinitionSnapshot& OutSnapshot) const override;
	virtual bool TryFindDefinitionView(
		const FPrimaryAssetId& DefinitionId,
		FRPGItemDefinitionView& OutView) const override;

private:
	mutable FCriticalSection CriticalSection;
	TMap<FPrimaryAssetId, FRPGItemDefinitionSnapshot> Snapshots;
	TMap<FPrimaryAssetId, FRPGItemDefinitionView> Views;
};
