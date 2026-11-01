#pragma once

#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RPGCharacterProgressionRestoreE2E.generated.h"

class FRPGCharacterProgressionCombatProbe;
/** Opt-in observer of real backend admission/restoration and the existing quickslot input path. */
UCLASS(Transient, NotBlueprintable)
class URPGCharacterProgressionRestoreE2E final : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
private:
	bool TickProbe(float);
	bool Fail(const TCHAR* Reason);
	FTSTicker::FDelegateHandle TickHandle;
	FString CharacterId, SteamId, SessionId, GateRoot;
	FString ChargeChoice = TEXT("FullCharge");
	TSharedPtr<FRPGCharacterProgressionCombatProbe> CombatProbe;
	bool bCombat = false;
	double Deadline = 0.0, InputStartedAt = 0.0, CastSeenAt = 0.0;
	int32 Stage = 0;
};
