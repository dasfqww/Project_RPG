#pragma once

#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// Shared by the manager's transport implementations; never a player authority grant.
namespace RPGHttpWebManager
{
	inline bool IsHttpBackendServerRuntime()
	{
#if UE_BUILD_SHIPPING
		return IsRunningDedicatedServer();
#else
		return IsRunningDedicatedServer()
			|| FParse::Param(
				FCommandLine::Get(),
				TEXT("RPGItemE2EServer"));
#endif
	}
}
