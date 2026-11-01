#pragma once
#include "CoreMinimal.h"
class UAnimMontage;
class UAnimSequenceBase;
namespace RPGMontageImmunityTests
{
	UAnimMontage* LoadPlayerMontage();
	void AddWindow(UAnimMontage& Montage, float StartFraction, float EndFraction, int32 Flags);
	void Refresh(UAnimSequenceBase& Animation);
}
