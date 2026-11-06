#pragma once

#include "CoreMinimal.h"
#include "Http.h"
#include "Online/RPGServiceStatus.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RPGServiceStatusSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FRPGServiceStatusChanged);

/** Owns only credential-free status transport and cache lifetime. Caller supplies the active backend URL. */
UCLASS()
class PROJECT_RPG_API URPGServiceStatusSubsystem final : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	bool Refresh(const FString& ApiUrl, bool bForce = false);
	void ObserveDraining(const FString& ApiUrl);
	bool MatchesBackend(const FString& ApiUrl) const;
	const FRPGServiceStatusState& GetState() const { return State; }
	bool IsRequestInFlight() const { return Active.IsValid(); }
	FRPGServiceStatusChanged OnChanged;
private:
	friend struct FRPGServiceStatusTestAccess;
	static FString Endpoint(const FString& ApiUrl);
	void Cancel();
	void Complete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess, uint64 RequestGeneration);
	FRPGServiceStatusState State;
	FString StatusUrl;
	FHttpRequestPtr Active;
	uint64 Generation = 0;
	double NextReadAt = 0;
};
