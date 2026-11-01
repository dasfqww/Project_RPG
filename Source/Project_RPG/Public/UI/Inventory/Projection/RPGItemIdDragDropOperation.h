#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "RPGItemIdDragDropOperation.generated.h"

class URPGInventoryProjectionViewModel;

/** Client-side drag payload; registration still requires server validation. */
UCLASS()
class PROJECT_RPG_API URPGItemIdDragDropOperation final : public UDragDropOperation
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Item|UI")
	FGuid ItemId;

	/** Local drag provenance only; neither value grants server authority. */
	TWeakObjectPtr<URPGInventoryProjectionViewModel> SourceViewModel;
	int64 ExpectedRevision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Item|UI")
	bool bSplitStack = false;
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Item|UI")
	bool bChooseSplitQuantity = false;
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Item|UI")
	int32 SplitQuantity = 0;
};
