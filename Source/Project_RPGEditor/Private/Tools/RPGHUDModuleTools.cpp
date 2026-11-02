#include "Tools/RPGHUDModuleTools.h"
#include "WidgetBlueprint.h"
#include "Animation/WidgetAnimation.h"
#include "Kismet2/BlueprintEditorUtils.h"

TArray<FString> URPGHUDModuleTools::DescribeAnimations(UWidgetBlueprint* Blueprint)
{
    TArray<FString> Result;
    if (!Blueprint) return Result;
    Result.Add(FString::Printf(TEXT("__PROPERTY_BINDINGS__|%d"), Blueprint->Bindings.Num()));
    for (const auto& Entry : Blueprint->Animations)
    {
        UWidgetAnimation* Animation = Entry.Get();
        if (!Animation) continue;
        for (const auto& Binding : Animation->GetBindings())
            Result.Add(FString::Printf(TEXT("%s|%s|%s|%d"), *Animation->GetName(), *Binding.WidgetName.ToString(),
                *Binding.SlotWidgetName.ToString(), Binding.bIsRootWidget ? 1 : 0));
    }
    return Result;
}

bool URPGHUDModuleTools::FinalizeIdentityAnimationExtraction(UWidgetBlueprint* Parent, UWidgetBlueprint* Module)
{
    if (!Parent || !Module || Parent == Module || Parent->Animations.Num() != Module->Animations.Num()
        || Parent->Bindings.Num() || Module->Bindings.Num()) return false;
    for (int32 Index = 0; Index < Parent->Animations.Num(); ++Index)
    {
        const auto* Original = Parent->Animations[Index].Get();
        const auto* Copy = Module->Animations[Index].Get();
        if (!Original || !Copy || Original == Copy || Original->GetName() != Copy->GetName()
            || Original->GetBindings() != Copy->GetBindings()
            || Original->GetStartTime() != Copy->GetStartTime() || Original->GetEndTime() != Copy->GetEndTime()) return false;
        for (const auto& Binding : Original->GetBindings())
            if (Binding.bIsRootWidget || (Binding.WidgetName != TEXT("WBP_IdentityGaugeBar")
                && Binding.WidgetName != TEXT("WBP_AbilityIconSlot_RageSkill"))) return false;
    }
    Parent->Modify();
    // Match UMG's own Delete Animation path: both GUID bookkeeping and the old
    // inner object must be retired before the Blueprint is reconstructed/saved.
    for (const auto& Entry : Parent->Animations)
    {
        const FName Name = Entry->GetFName();
        Entry->Rename(nullptr, GetTransientPackage());
        Parent->OnVariableRemoved(Name);
    }
    Parent->Animations.Reset();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Parent);
    return true;
}
