#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RPGHUDModuleTools.generated.h"
class UWidgetBlueprint;

/** Narrow editor migration helpers for protected WidgetBlueprint animation data. */
UCLASS()
class PROJECT_RPGEDITOR_API URPGHUDModuleTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="ProjectRPG|Editor|HUD")
    static TArray<FString> DescribeAnimations(UWidgetBlueprint* Blueprint);

    /** Only clear the parent's copies after confirming the duplicate owns the same identity animations. */
    UFUNCTION(BlueprintCallable, Category="ProjectRPG|Editor|HUD")
    static bool FinalizeIdentityAnimationExtraction(UWidgetBlueprint* Parent, UWidgetBlueprint* Module);
};
