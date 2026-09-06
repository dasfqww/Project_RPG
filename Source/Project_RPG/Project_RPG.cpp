// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_RPG.h"
#include "Modules/ModuleManager.h"

namespace
{
	/**
	 * Modules normally requested during the first engine tick must be loaded
	 * before a GameNetDriver creates its Iris serializer registry. Loading them
	 * later makes Iris rebuild polymorphic serializer caches while a replication
	 * system is active.
	 */
	class FProjectRPGModule final : public FDefaultGameModuleImpl
	{
	public:
		virtual void StartupModule() override
		{
			FDefaultGameModuleImpl::StartupModule();

			FModuleManager::Get().LoadModule(TEXT("Voice"));
#if WITH_DEV_AUTOMATION_TESTS
			FModuleManager::Get().LoadModule(TEXT("AutomationWorker"));
			FModuleManager::Get().LoadModule(TEXT("AutomationController"));
#endif
		}
	};
}

IMPLEMENT_PRIMARY_GAME_MODULE(FProjectRPGModule, Project_RPG, "Project_RPG");
