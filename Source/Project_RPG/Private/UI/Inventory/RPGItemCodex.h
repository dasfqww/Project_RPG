#pragma once

#include "CoreMinimal.h"
#include "UObject/PrimaryAssetId.h"

class APlayerController;
class SWidget;
class UUserWidget;
class URPGWidgetBase;

namespace RPGItemCodex
{
	struct FEntry
	{
		FPrimaryAssetId Id;
		int32 Version = 0;
		FText Name;
		FText Description;
		FText Category;
		FText Usage;
		TArray<FText> Sources;

		bool Matches(const FString& Query) const;
	};

	TArray<FEntry> LoadEntries();
	URPGWidgetBase* CreateWindow(APlayerController& Owner, FSimpleDelegate OnClose, TSharedPtr<SWidget>& OutSearch);
	bool IsWindow(const UUserWidget& Widget);
}
