#pragma once

#include "Modules/ModuleManager.h"

class FCommonCoroutineModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};