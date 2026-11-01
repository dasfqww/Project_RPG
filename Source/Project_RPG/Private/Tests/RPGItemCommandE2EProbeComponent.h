#pragma once

#include "Component/RPGItemCommandComponent.h"
#include "Components/ActorComponent.h"
#include "Tests/RPGNPCPickupNetworkProbe.h"
#include "RPGItemCommandE2EProbeComponent.generated.h"

/**
 * Development-only network probe for the live Item V2 RPC path.
 *
 * The component is attached only in non-shipping builds and remains disabled
 * unless both -RPGItemE2EConsume=<item-guid> and
 * -RPGItemE2EEquip=<item-guid> are present on a remote client. When
 * -RPGItemE2ERestricted is also present on both processes, the probe asks the
 * server to enter Restricted state and verifies that a subsequent consume RPC
 * is rejected without mutating the projection or backend record.
 * -RPGItemE2ERestrictedReleaseFile=<path> holds that request until the test
 * harness creates the file, allowing deterministic backend outage injection.
 */
UCLASS(Transient, NotBlueprintable)
class URPGItemCommandE2EProbeComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGItemCommandE2EProbeComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(
		const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	enum class EProbePhase : uint8
	{
		WaitingForInitialProjection,
		WaitingForConsumeResult,
		WaitingForConsumeProjection,
		WaitingForEquipResult,
		WaitingForEquipProjection,
		WaitingForUnequipResult,
		WaitingForUnequipProjection,
		WaitingForRestrictionRelease,
		WaitingForRestrictionConfirmation,
		WaitingForRestrictedConsumeResult,
		WaitingForRestrictedProjection
	};

	UFUNCTION(Server, Reliable)
	void ServerForceRestrictedState();

	UFUNCTION(Client, Reliable)
	void ClientConfirmRestrictedState(bool bRestricted, float RiskScore);

	UFUNCTION()
	void HandleCommandCompleted(FRPGItemCommandClientResult Result);

	void FailProbe(const TCHAR* Reason);
	void FinishProbe();

	FGuid ConsumableItemId;
	FGuid EquipmentItemId;
	FGuid RequestId;
	int32 InitialConsumableQuantity = 0;
	int64 InitialConsumableRevision = 0;
	int64 InitialEquipmentRevision = 0;
	double DeadlineSeconds = 0.0;
	FString RestrictedReleaseFile;
	bool bVerifyRestrictedGate = false;
	EProbePhase Phase = EProbePhase::WaitingForInitialProjection;
	bool bNPCPickupProbe = false;
	FRPGNPCPickupNetworkProbe NPCPickupProbe;
};
