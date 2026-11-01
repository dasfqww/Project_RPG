#include "UI/Option/RPGOptionsMenuRuntime.h"

#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogRPGOptionsMenuBridge, Log, All);

FName IRPGOptionsMenuRuntime::GetFeatureName()
{
    static const FName Name(TEXT("RPGOptionsMenuRuntime"));
    return Name;
}

IRPGOptionsMenuRuntime* IRPGOptionsMenuRuntime::Get(bool bLoadIfNeeded)
{
    check(IsInGameThread());
    IModularFeatures& Features = IModularFeatures::Get();
    const FName Name = GetFeatureName();
    if (bLoadIfNeeded && !Features.IsModularFeatureAvailable(Name))
    {
        FModuleManager::LoadModulePtr<IModuleInterface>(TEXT("RPGOptionsUIRuntime"));
    }
    if (Features.IsModularFeatureAvailable(Name))
    {
        return &Features.GetModularFeature<IRPGOptionsMenuRuntime>(Name);
    }
    if (bLoadIfNeeded)
    {
        UE_LOG(LogRPGOptionsMenuBridge, Error, TEXT("Options input implementation is unavailable."));
    }
    return nullptr;
}
