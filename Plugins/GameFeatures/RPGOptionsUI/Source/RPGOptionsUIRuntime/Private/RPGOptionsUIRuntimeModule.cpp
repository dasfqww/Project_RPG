#include "Modules/ModuleManager.h"

#include "Features/IModularFeatures.h"
#include "RPGGraphicsOptionsRuntime.h"
#include "RPGSoundOptionsRuntime.h"
#include "RPGOptionsMenuRuntime.h"
#include "RPGInputOptionsRuntime.h"

class FRPGOptionsUIRuntimeModule final : public FDefaultModuleImpl
{
public:
    virtual void StartupModule() override
    {
        FDefaultModuleImpl::StartupModule();
        IModularFeatures::Get().RegisterModularFeature(
            IRPGGraphicsOptionsRuntime::GetFeatureName(), &GraphicsOptions);
        IModularFeatures::Get().RegisterModularFeature(
            IRPGSoundOptionsRuntime::GetFeatureName(), &SoundOptions);
        IModularFeatures::Get().RegisterModularFeature(
            IRPGOptionsMenuRuntime::GetFeatureName(), &OptionsMenu);
        IModularFeatures::Get().RegisterModularFeature(
            IRPGInputOptionsRuntime::GetFeatureName(), &InputOptions);
    }

    virtual void ShutdownModule() override
    {
        IModularFeatures::Get().UnregisterModularFeature(
            IRPGInputOptionsRuntime::GetFeatureName(), &InputOptions);
        IModularFeatures::Get().UnregisterModularFeature(
            IRPGOptionsMenuRuntime::GetFeatureName(), &OptionsMenu);
        IModularFeatures::Get().UnregisterModularFeature(
            IRPGGraphicsOptionsRuntime::GetFeatureName(), &GraphicsOptions);
        IModularFeatures::Get().UnregisterModularFeature(
            IRPGSoundOptionsRuntime::GetFeatureName(), &SoundOptions);
        FDefaultModuleImpl::ShutdownModule();
    }

private:
    FRPGGraphicsOptionsRuntime GraphicsOptions;
    FRPGSoundOptionsRuntime SoundOptions;
    FRPGOptionsMenuRuntime OptionsMenu;
    FRPGInputOptionsRuntime InputOptions;
};

IMPLEMENT_MODULE(FRPGOptionsUIRuntimeModule, RPGOptionsUIRuntime)
