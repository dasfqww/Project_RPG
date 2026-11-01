#pragma once
#include "CoreMinimal.h"
class UGameInstance;

namespace RPGAirborneJoinGate
{
	FString File(const TCHAR* Name);
	bool DelayCompletion(UGameInstance& Instance);
}
