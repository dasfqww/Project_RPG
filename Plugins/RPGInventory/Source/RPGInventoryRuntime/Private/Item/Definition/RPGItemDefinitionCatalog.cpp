#include "Item/Definition/RPGItemDefinitionCatalog.h"

#include "Item/Definition/RPGItemDefinition.h"
#include "Misc/ScopeLock.h"

bool FRPGItemDefinitionRegistry::RegisterDefinition(
	const URPGItemDefinition& Definition)
{
	FRPGItemDefinitionView View;
	View.Snapshot.DefinitionId = Definition.GetPrimaryAssetId();
	View.Snapshot.DefinitionVersion = Definition.GetDefinitionVersion();
	View.Snapshot.MaxStackSize = Definition.GetMaxStackSize();
	View.ItemTag = Definition.ItemTag;
	View.ItemCategory = Definition.ItemCategory;
	View.DisplayName = Definition.DisplayName;
	View.Description = Definition.Description;
	View.Icon = Definition.Icon;
	View.GridSize = Definition.GridSize;
	return View.IsValid()
		? RegisterDefinitionView(View)
		: RegisterSnapshot(View.Snapshot);
}

bool FRPGItemDefinitionRegistry::RegisterDefinitionView(
	const FRPGItemDefinitionView& View)
{
	if (!View.IsValid())
	{
		return false;
	}

	FScopeLock Lock(&CriticalSection);
	const FRPGItemDefinitionView* Existing =
		Views.Find(View.Snapshot.DefinitionId);
	if (Existing && !Existing->bLegacySource && View.bLegacySource)
	{
		return true;
	}

	Snapshots.Add(View.Snapshot.DefinitionId, View.Snapshot);
	Views.Add(View.Snapshot.DefinitionId, View);
	return true;
}

bool FRPGItemDefinitionRegistry::RegisterSnapshot(
	const FRPGItemDefinitionSnapshot& Snapshot)
{
	if (!Snapshot.IsValid())
	{
		return false;
	}

	FScopeLock Lock(&CriticalSection);
	Snapshots.Add(Snapshot.DefinitionId, Snapshot);
	if (FRPGItemDefinitionView* View = Views.Find(Snapshot.DefinitionId))
	{
		View->Snapshot = Snapshot;
	}
	return true;
}

void FRPGItemDefinitionRegistry::Reset()
{
	FScopeLock Lock(&CriticalSection);
	Snapshots.Reset();
	Views.Reset();
}

bool FRPGItemDefinitionRegistry::TryFindDefinition(
	const FPrimaryAssetId& DefinitionId,
	FRPGItemDefinitionSnapshot& OutSnapshot) const
{
	FScopeLock Lock(&CriticalSection);
	const FRPGItemDefinitionSnapshot* Snapshot = Snapshots.Find(DefinitionId);
	if (!Snapshot)
	{
		return false;
	}

	OutSnapshot = *Snapshot;
	return true;
}

bool FRPGItemDefinitionRegistry::TryFindDefinitionView(
	const FPrimaryAssetId& DefinitionId,
	FRPGItemDefinitionView& OutView) const
{
	FScopeLock Lock(&CriticalSection);
	const FRPGItemDefinitionView* View = Views.Find(DefinitionId);
	if (!View)
	{
		return false;
	}

	OutView = *View;
	return true;
}
