#pragma once

#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RPGDungeonRecoveryTestSubsystem.generated.h"

/** Explicit development-only offline fixture. Never creates real backend membership. */
UCLASS()
class URPGDungeonRecoveryTestSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
private:
	bool TickProbe(float DeltaSeconds);
	FTSTicker::FDelegateHandle Ticker;
	double Deadline = 0.0;
	bool bSeeded = false;
};
