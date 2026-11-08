#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "Manager/HttpWebManager.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGSteamLobbySubsystem.h"
#include "Type/RPGEnumTypes.h"
#include "UI/Lobby/RPGOnlineLobbyPartyView.h"
#include "UI/Lobby/RPGOnlineLobbyNextStep.h"
#include "UI/Lobby/RPGOnlineLobbyActionAvailability.h"
#include "RPGOnlineLobbyWidget.generated.h"

class UButton;
class UComboBoxString;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;
class URPGSteamAuthSubsystem;
class URPGDungeonDefinition;
class URPGCharacterConfigurationWidget;
class URPGEquipmentGrowthWidget;
class URPGNarrativeDialogueWidget;
class URPGTradeWidget;
class URPGServiceStatusSubsystem;

struct PROJECT_RPG_API FRPGOnlineLobbyPresentation
{
	bool bCanLogin = false;
	bool bCanLoadCharacters = false;
	bool bCanCreateCharacter = false;
	bool bCanSelectCharacter = false;
	bool bCanCreateSession = false;
	bool bCanResume = false;
	bool bCanRefresh = false;
	bool bCanLeave = false;
	bool bCanEnter = false;
	bool bBusy = false;
};

/** Minimal native frontend using the existing authenticated HTTP/Steam coordinators. */
UCLASS(Config = Game)
class PROJECT_RPG_API URPGOnlineLobbyWidget final : public UUserWidget
{
	GENERATED_BODY()
public:
	static FRPGOnlineLobbyPresentation BuildPresentation(bool bLocalLobby, bool bAuthenticated,
		bool bUIRequest, bool bAuthRequest, bool bSteamRequest, bool bFlowRequest,
		bool bOwnedSelection, const FString& SelectedCharacter, const FRPGDungeonSession& Session,
		ERPGDungeonClientFlowState FlowState, const FString& AdmissionBlockReason = FString());
	static bool IsDevelopmentUIEnabled();
	static bool TryNormalizeCharacterName(const FString& Input, FString& NormalizedName);
	FString GetCharacterOption(const FString& CharacterId) const;
	FText GetDisplayedStatus() const;
	/** Guidance only; the target name is not permission to execute an action. */
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Lobby|Guidance")
	FText GetNextStepText() const;
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Lobby|Guidance")
	FName GetNextStepControlName() const;
	/** Latest presentation reason only; an empty result is not an execution permit. */
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Lobby|Guidance")
	FText GetActionUnavailableReason(FName ButtonName) const;
	/** Read-only backend membership, not Steam membership or online/readiness status. */
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Lobby|Party")
	TArray<FRPGOnlineLobbyPartyMemberView> GetDungeonPartyMembers() const;
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Lobby|Party")
	FText GetDungeonPartySummaryText() const;
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Lobby|Party")
	FText GetDungeonPartyRosterText() const;
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon|Selection")
	static TArray<ERPGGameDifficulty> GetDungeonDifficultyOptions();
	/** Registered exact IDs. An empty config retains the existing single-dungeon preference. */
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon|Selection")
	TArray<FString> GetDungeonOptions() const;
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon|Selection")
	FText GetDungeonDisplayName(const FString& InDungeonId) const;
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon|Selection")
	bool GetSelectedDungeonId(FString& OutDungeonId) const;
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon|Selection")
	bool CanSelectDungeon() const;
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon|Selection")
	bool SelectDungeon(const FString& InDungeonId);
	/** Use this per-dungeon list for custom UI; the static list is the global codec. */
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon|Selection")
	TArray<ERPGGameDifficulty> GetAvailableDungeonDifficultyOptions() const;
	/** A creation preference only. An unfinished session displays its backend value instead. */
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon|Selection")
	bool GetSelectedDungeonDifficulty(ERPGGameDifficulty& OutDifficulty) const;
	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon|Selection")
	bool CanSelectDungeonDifficulty() const;
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon|Selection")
	bool SelectDungeonDifficulty(ERPGGameDifficulty InDifficulty);
	/** Uses the same current-state guards as the native create button. Never changes an existing session. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon|Selection")
	bool CreateSelectedDungeonSession();
	bool IsFrontendRequestInFlight() const;
	bool CanUseTrading() const;
	UFUNCTION(BlueprintPure, Category = "RPG|Progression|Lobby")
	URPGCharacterConfigurationWidget* GetCharacterConfigurationWidget() const { return ConfigurationPanel; }
	UFUNCTION(BlueprintPure, Category = "RPG|Progression|Lobby")
	URPGEquipmentGrowthWidget* GetEquipmentGrowthWidget() const { return EquipmentPanel; }
	UFUNCTION(BlueprintCallable, Category = "RPG|Narrative")
	bool OpenNarrativeDialogues();
	UFUNCTION(BlueprintPure, Category = "RPG|Narrative")
	URPGNarrativeDialogueWidget* GetNarrativeDialogueWidget() const { return NarrativePanel; }
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
private:
	friend struct FRPGServiceStatusTestAccess;
	void BindServiceStatus();
	void UnbindServiceStatus();
	void PollServiceStatus();
	void HandleServiceStatusChanged();
	void HandleServiceDraining(const FString& ApiUrl);
	void RefreshServicePresentation();
	void BuildServiceStatusContent(UVerticalBox& Header);
	bool IsServiceAdmissionBlocked(FName Name) const;
	UPROPERTY(Transient) TObjectPtr<URPGServiceStatusSubsystem> ServiceStatus;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ServiceStatusText;
	UPROPERTY(Transient) TObjectPtr<UWidget> ServiceStatusContainer;
	friend class URPGCharacterConfigurationWidget;
	friend class URPGEquipmentGrowthWidget;
	friend class FRPGEquipmentGrowthWidgetTest;
	bool CanUseCharacterConfiguration(bool bEdit, bool bRetry) const;
	bool CanUseEquipmentGrowth(bool bWrite, bool bRetry) const;
	FText GetEquipmentGrowthUnavailableReason(bool bWrite, bool bRetry, bool* OutAllowed = nullptr) const;
	friend struct FRPGDungeonAbandonmentTestAccess;
	friend struct FRPGDungeonSelectionTestAccess;
	friend class FRPGOnlineLobbyWidgetTest;
	friend class FRPGMenuFrontendTest;
	void BuildMenuContent(UVerticalBox& Parent, bool bServerPage);
	void RefreshMenuPresentation();
	bool CanStartTutorial() const;
	FString GetPracticeEntryDungeonId() const;
	bool HasCompletedTutorial() const;
	void AdvanceTutorialEntry();
	void StopTutorialEntry();
	UFUNCTION() void SelectServerPage();
	UFUNCTION() void ShowServerPage();
	UFUNCTION() void OpenSkillMenu();
	UFUNCTION() void OpenEquipmentMenu();
	UFUNCTION() void OpenTradeMenu();
	void HandleTradeClosed();
	UPROPERTY(Transient) TObjectPtr<URPGTradeWidget> TradePanel;
	UFUNCTION() void OpenStoryMenu();
	bool CanOpenNarrativeDialogues() const;
	UFUNCTION() void OpenDungeonMenu();
	UFUNCTION() void CloseLobbyDetails();
	UFUNCTION() void StartTutorial();
	UFUNCTION() void ReloadTutorialProgress();
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> ServerContent;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> MenuContent;
	UPROPERTY(Transient) TObjectPtr<UWidget> ConfigurationSection;
	UPROPERTY(Transient) TObjectPtr<UWidget> EquipmentSection;
	UPROPERTY(Transient) TObjectPtr<UWidget> DungeonSection;
	UPROPERTY(Transient) TObjectPtr<UWidget> SteamSection;
	UPROPERTY(Transient) TObjectPtr<UWidget> DevelopmentSessionSection;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TutorialStatusText;
	// Presentation and user intent only; selected identity/session stay in Flow.
	bool bServerSelected = false, bSkillMenuOpen = false, bDungeonMenuOpen = false, bEquipmentMenuOpen = false;
	bool bTutorialEntryActive = false, bTutorialResolvingSession = false, bAdvancingTutorial = false;
	FString TutorialAccount, TutorialCharacter, TutorialDungeonId;
	FText TutorialEntryFailure;
	double TutorialDeadline = 0.0;
	friend class FRPGOnlineLobbyNavigationNativeTest;
	friend class FRPGOnlineLobbyFocusHandoffTest;
	friend class FRPGOnlineLobbyBackInputTest;
	FRPGOnlineLobbyPresentation CurrentPresentation() const;
	void RefreshPresentation();
	void RefreshEntryPresentation();
	void RefreshInputNavigation();
	void BuildCharacterContent(UVerticalBox& Parent);
	void RefreshCharacterPresentation();
	FText GetCharacterProgressionText(const FRPGBackendCharacter& Character) const;
	const FRPGCharacterCreationOption* GetSelectedCreationClass() const;
	void BeginCreationOptionsLoad();
	UFUNCTION() void ReloadCreationOptions();
	UFUNCTION() void HandleCreationClassSelected(FString Option, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleCreationOptions(const TArray<FRPGCharacterCreationOption>& Options, bool bSuccess);
	FRPGOnlineLobbyNextStep BuildNextStepGuidance() const;
	void RefreshNextStepPresentation();
	void RefreshActionUnavailablePresentation();
	void RefreshDungeonPartyPresentation();
	bool IsDungeonPartySnapshotCached() const;
	FString GetDisplayedDungeonDifficulty() const;
	FString GetDisplayedDungeonId() const;
	bool HasUnfinishedDungeonSelection() const;
	void LoadDungeonDefinitions();
	bool ValidateDungeonDefinitions(FString& OutError) const;
	const URPGDungeonDefinition* FindDungeonDefinition(const FString& InDungeonId) const;
	bool UpdateDungeonSelectionPresentation();
	void PollSession();
	void BeginCharacterLoad();
	bool HasOwnedSelection() const;
	bool CanUse(FName ButtonName);
	UFUNCTION() void LoginSteam();
	UFUNCTION() void LoginDevelopment();
	UFUNCTION() void LogoutBackend();
	UFUNCTION() void HandleLogout(bool bSuccess);
	UFUNCTION() void ReloadCharacters();
	UFUNCTION() void CreateCharacter();
	UFUNCTION() void SelectCharacter(FString Option, ESelectInfo::Type SelectionType);
	UFUNCTION() void ContinueWithCharacter();
	UFUNCTION() void ChangeCharacter();
	UFUNCTION() void HandleDungeonDifficultySelected(FString Option, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleDungeonSelected(FString Option, ESelectInfo::Type SelectionType);
	UFUNCTION() void CreateSession();
	UFUNCTION() void ResumeSession();
	UFUNCTION() void RefreshSession();
	UFUNCTION() void LeaveSession();
	UFUNCTION() void RequestDeparture();
	UFUNCTION() void CancelDeparture();
	UFUNCTION() void RetryDeparture();
	UFUNCTION() void OpenAbandonmentConfirmation();
	UFUNCTION() void ConfirmAbandonment();
	UFUNCTION() void CancelAbandonment();
	bool CanConfirmAbandonment() const;
	void CloseAbandonmentConfirmation();
	UFUNCTION() void EnterDungeon();
	UFUNCTION() void CreateSteamParty();
	UFUNCTION() void FindSteamParties();
	UFUNCTION() void JoinSteamParty();
	UFUNCTION() void AcceptSteamInvite();
	UFUNCTION() void InviteSteamFriends();
	UFUNCTION() void LeaveSteamParty();
	UFUNCTION() void PublishSteamServer();
	UFUNCTION() void JoinDevelopmentSession();
	UFUNCTION() void HandleLogin(bool bSuccess);
	UFUNCTION() void HandleCharacters(const TArray<FRPGBackendCharacter>& Characters, bool bSuccess);
	UFUNCTION() void HandleCharacterCreated(const FRPGBackendCharacter& Character, bool bSuccess);
	UFUNCTION() void HandleFlow(ERPGDungeonClientFlowState State, const FString& ErrorCode);
	UFUNCTION() void HandleSession(const FRPGDungeonSession& Session);
	UFUNCTION() void HandleSteamState(ERPGSteamLobbyState State, const FString& ErrorCode);
	UFUNCTION() void HandleSteamLobby(const FRPGSteamLobbyInfo& Lobby);
	UFUNCTION() void HandleSteamSearch(const TArray<FRPGSteamLobbyInfo>& Results, bool bSuccess);
	UPROPERTY(Config) FString DungeonId = TEXT("Dungeon.RaidSwordMan");
	UPROPERTY(Config) FString Difficulty = TEXT("Hard");
	/** Presentation labels keyed by the exact saved class ID; never a class grant/catalog. */
	UPROPERTY(Config) TMap<FString, FText> CharacterClassNames;
	/** Explicit asset references; configured invalid catalogs never fall back to the legacy ID. */
	UPROPERTY(Config) TArray<TSoftObjectPtr<URPGDungeonDefinition>> DungeonDefinitions;
	UPROPERTY(Transient) TArray<TObjectPtr<URPGDungeonDefinition>> LoadedDungeonDefinitions;
	FString DungeonDefinitionsLoadError;
	UPROPERTY(Transient) TObjectPtr<UHttpWebManager> Web;
	UPROPERTY(Transient) TObjectPtr<URPGSteamAuthSubsystem> Auth;
	UPROPERTY(Transient) TObjectPtr<URPGDungeonSessionSubsystem> Flow;
	UPROPERTY(Transient) TObjectPtr<URPGSteamLobbySubsystem> Steam;
	UPROPERTY(Transient) TArray<FRPGBackendCharacter> OwnedCharacters;
	UPROPERTY(Transient) TArray<FRPGSteamLobbyInfo> FoundParties;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LoginHintText;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> LoginContent;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> CharacterContent;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> LobbyContent;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CharacterListText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SelectedCharacterText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SelectedCharacterDetailsText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CreationClassText;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> CreationClassBox;
	UPROPERTY(Transient) TArray<FRPGCharacterCreationOption> CreationClasses;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PreparationCharacterText;
	UPROPERTY(Transient) TObjectPtr<UWidget> AccountSection;
	UPROPERTY(Transient) TObjectPtr<URPGCharacterConfigurationWidget> ConfigurationPanel;
	UPROPERTY(Transient) TObjectPtr<URPGEquipmentGrowthWidget> EquipmentPanel;
	UPROPERTY(Transient) TObjectPtr<URPGNarrativeDialogueWidget> NarrativePanel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NextStepText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SessionText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PartyRosterHeader;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PartyRosterText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DungeonSelectionText;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> DifficultyBox;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> DungeonsBox;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> AbandonmentConfirmation;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> CharactersBox;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> PartiesBox;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> CharacterName;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> DevelopmentSteamId;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> DevelopmentSessionId;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UButton>> Buttons;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UTextBlock>> ActionUnavailableTexts;
	TMap<FName, ERPGOnlineLobbyBlockReason> ActionBlockReasons;
	FTimerHandle PollTimer;
	FString UIError;
	FString PolledSessionId;
	FString ConfirmAbandonmentSessionId;
	FString ConfirmAbandonmentCharacterId;
	FString ConfirmAbandonmentSteamId;
	bool bAbandonmentConfirmationOpen = false;
	double PollUntil = 0.0;
	bool bLoadingCharacters = false;
	bool bInitialCharacterLoadPending = false;
	bool bCreatingCharacter = false;
	bool bLoggingIn = false;
	bool bLoggingOut = false;
	bool bUpdatingOptions = false;
	bool bUpdatingDifficultyOptions = false;
	bool bUpdatingDungeonOptions = false;
	// Navigation only. Character identity and membership remain owned by Flow.
	bool bPreparationOpen = false;
	bool bHasLoadedCharacterList = false;
	bool bLoadingCreationOptions = false;
	bool bCreationOptionsReady = false;
	bool bUpdatingCreationOptions = false;
};
