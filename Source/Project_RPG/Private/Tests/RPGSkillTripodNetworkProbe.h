#pragma once

#include "CoreMinimal.h"
#include "GameplayAbilitySpec.h"
#include "UObject/StrongObjectPtr.h"

class APlayerController;
class URPGPlayerSkillComponent;
class URPGSkillCatalogSubsystem;
class URPGSkillDefinition;
class URPGSkillViewModel;
class URPGTripodBoard;
class URPGAbilitySystemComponent;
class SWidget;
class FRPGSkillAssetCooldownProbe;

/** Opt-in, transient fixture; never edits authored definitions or D1 abilities. */
class FRPGSkillTripodNetworkProbe
{
public:
	~FRPGSkillTripodNetworkProbe();
	enum class EResult : uint8 { Waiting, Passed, Failed };
	EResult Tick(APlayerController& Controller);
	const FString& GetFailure() const { return Failure; }

private:
	enum class EPhase : uint8
	{
		Initialize, Level, Select, InstantActivate, Reject, Switch,
		ChargeActivate, ChargeRelease, Privacy, Budget,
		BudgetReject, BudgetReset, LevelDown, Reset, Finished
	};
	bool Initialize(APlayerController& Controller, URPGPlayerSkillComponent& Component,
		URPGSkillCatalogSubsystem& Catalog);
	EResult TickClient(APlayerController& Controller, URPGPlayerSkillComponent& Component);
	EResult TickServer(APlayerController& Controller, URPGPlayerSkillComponent& Component);
	void BeginReplyWait(const URPGPlayerSkillComponent& Component, uint32 Replies = 1);
	bool RepliesArrived(const URPGPlayerSkillComponent& Component) const;
	bool CheckState(const URPGPlayerSkillComponent& Component, int32 Level,
		int32 Option, int32 RemainingSP) const;
	bool RequestRejection(URPGPlayerSkillComponent& Component);
	bool ClickTripod(int32 Tier, int32 Option);
	bool CheckButtons(int32 Level, int32 Option) const;
	EResult TickClientActivation(APlayerController& Controller, bool bCharge);
	bool ObserveServerActivation(APlayerController& Controller);
	void ObserveAbilityEnd(APlayerController& Controller, URPGAbilitySystemComponent& ASC);
	void ReleaseInput();
	bool CheckBudgetState(const URPGPlayerSkillComponent& Component, int32 Level) const;
	bool CheckRemotePrivacy(APlayerController& Controller, int32& VisiblePeers,
		bool& AllPeersReady) const;
	EResult Fail(const TCHAR* Reason);

	TStrongObjectPtr<URPGSkillDefinition> Definition;
	TStrongObjectPtr<URPGSkillDefinition> BudgetDefinition;
	TStrongObjectPtr<URPGSkillViewModel> ViewModel;
	TStrongObjectPtr<URPGTripodBoard> Board;
	TSharedPtr<SWidget> SlateBoard;
	TSharedPtr<FRPGSkillAssetCooldownProbe> AssetCooldownProbe;
	TWeakObjectPtr<URPGAbilitySystemComponent> InputAbilitySystem;
	FDelegateHandle AbilityEndedHandle;
	FGameplayAbilitySpecHandle SkillHandle;
	FString Failure;
	uint32 RevisionBefore = 0;
	uint32 ExpectedReplies = 1;
	int32 RejectionIndex = 0;
	int32 ExpectedClients = 0;
	double PrivacyReadySince = 0.0;
	float OriginalPlayerScore = 0.0f;
	EPhase Phase = EPhase::Initialize;
	bool bSawFirstSelection = false;
	bool bSawSecondSelection = false;
	bool bSawLockedTierCleared = false;
	bool bSawBudgetLimit = false;
	bool bSawBudgetRefund = false;
	bool bInputHeld = false;
	bool bLoggedCooldownWait = false;
	bool bClientSawInstant = false;
	bool bClientSawCharge = false;
	bool bClientSawRelease = false;
	bool bServerSawInstant = false;
	bool bServerSawCharge = false;
	bool bServerSawRelease = false;
	bool bChargeEndedNormally = false;
};
