#pragma once

#include "CoreMinimal.h"
#include "Item/Definition/RPGItemDefinitionCatalog.h"

struct FItemManifest;

/** Pure compatibility mapper from an authored legacy manifest to the new view. */
class PROJECT_RPG_API FRPGLegacyItemDefinitionAdapter final
{
public:
	static bool TryBuildDefinitionView(
		const FItemManifest& Manifest,
		FRPGItemDefinitionView& OutView,
		FString* OutError = nullptr);
};

