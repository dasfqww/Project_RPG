#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RPGRegionTravelComponent.generated.h"

class ARPGRegionGate;
class UUserWidget;
class UAbilitySystemComponent;

UCLASS()
class PROJECT_RPG_API URPGRegionTravelComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGRegionTravelComponent();
	void BeginTransfer(ARPGRegionGate* Source, ACharacter* Character);
	/** Level package confirmed for this controller's current pawn. None denotes the persistent region. */
	UFUNCTION(BlueprintPure, BlueprintAuthorityOnly, Category="Region")
	FName GetCurrentRegion() const;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	UFUNCTION(Client, Reliable)
	void ClientPrepare(int32 Id, FName Source, FName Destination);
	UFUNCTION(Server, Reliable)
	void ServerPrepared(int32 Id);
	UFUNCTION(Client, Reliable)
	void ClientFinish(int32 Id, bool bCommitted, FVector ArrivalLocation);

	void PollServer();
	void FinishServer(bool bCommitted);
	void PollClient();
	void ReleaseClient();
	void ReleaseServer();

	int32 Serial = 0;
	bool bServerPending = false;
	bool bReady = false;
	double ServerDeadline = 0;
	TWeakObjectPtr<ARPGRegionGate> SourceGate;
	TWeakObjectPtr<ARPGRegionGate> TargetGate;
	TWeakObjectPtr<ACharacter> LockedCharacter;
	TWeakObjectPtr<ACharacter> RegionCharacter;
	FName CurrentRegion;
	TWeakObjectPtr<UAbilitySystemComponent> BlockedAbilities;
	EMovementMode PreviousMode = MOVE_None;
	uint8 PreviousCustomMode = 0;
	FTimerHandle ServerTimer;

	int32 ClientId = 0;
	bool bReadySent = false;
	bool bAwaitingArrival = false;
	FVector ExpectedLocation = FVector::ZeroVector;
	double ArrivalDeadline = 0;
	FName ClientSource;
	FName ClientTarget;
	FTimerHandle ClientTimer;
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> Screen;
	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> InputBlock;
};
