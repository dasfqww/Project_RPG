#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineGlobals.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/RPGPlayerState.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "UI/Progress/RPGActionProgressWidget.h"
#include "UObject/UObjectIterator.h"

namespace RPGActionProgressCancelPIE
{
// All mutations are a single normal GAS cancellation. Python editor execution
// guards force Actor RPCs local, so the request must originate in a native tick.
struct FProbe : TSharedFromThis<FProbe>
{
	FString Gates, Character, Session, Steam;
	double Deadline = FPlatformTime::Seconds() + 240., ReleasedAt = 0;
	bool bCancelled = false, bServerEnded = false, bSavedLate = false;
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	TWeakObjectPtr<URPGAbilitySystemComponent> ASC;
	TWeakObjectPtr<URPGGameplayAbility_SkillContainer> ObservedSkill;
	FGameplayAbilitySpecHandle ObservedHandle;
	uint64 ObservedActivation = 0;
	FDelegateHandle EndedHandle;
	~FProbe() { if (auto* Component = ASC.Get()) Component->OnAbilityEnded.Remove(EndedHandle); }

	void Save(const bool bServer)
	{
		FString Json;
		FJsonSerializer::Serialize(Result.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
		const FString Path = FPaths::Combine(Gates, bServer ? TEXT("ServerCancellation.json") : TEXT("Cancellation.json"));
		const FString Temporary = Path + TEXT(".writing");
		FFileHelper::SaveStringToFile(Json, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		IFileManager::Get().Move(*Path, *Temporary, true, true);
	}
	bool Fail(const TCHAR* Message, const bool bServer)
	{
		Result->SetBoolField(TEXT("passed"), false);
		Result->SetStringField(TEXT("failure"), Message);
		Save(bServer);
		UE_LOG(LogTemp, Error, TEXT("RPG_ACTION_CANCEL_NATIVE FAIL Server=%d Reason=%s"), bServer, Message);
		return false;
	}
	URPGGameplayAbility_SkillContainer* FindSkill(ARPGPlayer* Pawn) const
	{
		URPGGameplayAbility_SkillContainer* Found = nullptr;
		for (TObjectIterator<URPGGameplayAbility_SkillContainer> It; It; ++It)
			if (It->IsActive() && It->GetAvatarActorFromActorInfo() == Pawn
				&& It->GetClass()->GetName() == TEXT("GA_Player_ChargeSlash_C"))
			{
				if (Found) return nullptr;
				Found = *It;
			}
		return Found;
	}
	static TSharedPtr<FJsonObject> Record(const URPGActionProgressWidget& View)
	{
		const auto Snapshot = View.GetDisplayedProgress();
		auto Value = MakeShared<FJsonObject>();
		Value->SetBoolField(TEXT("active"), Snapshot.bActive);
		Value->SetNumberField(TEXT("fraction"), Snapshot.Fraction);
		Value->SetStringField(TEXT("label"), Snapshot.Data.Label.ToString());
		Value->SetStringField(TEXT("reason"), StaticEnum<ERPGActionProgressEndReason>()->GetNameStringByValue(static_cast<int64>(Snapshot.EndReason)));
		Value->SetBoolField(TEXT("collapsed"), View.GetVisibility() == ESlateVisibility::Collapsed);
		return Value;
	}
	static bool IsCancelled(const URPGActionProgressWidget& View)
	{
		const auto Snapshot = View.GetDisplayedProgress();
		return !Snapshot.bActive && Snapshot.EndReason == ERPGActionProgressEndReason::Cancelled
			&& View.GetVisibility() == ESlateVisibility::Collapsed;
	}
	bool Tick(float)
	{
		if (IFileManager::Get().FileExists(*FPaths::Combine(Gates, TEXT("stop.ready")))) return false;
		const bool bServer = IsRunningDedicatedServer();
		if (FPlatformTime::Seconds() > Deadline) return Fail(TEXT("Native observer timeout"), bServer);
		UWorld* World = nullptr;
		if (GEngine) for (const auto& Context : GEngine->GetWorldContexts())
			if (Context.World() && Context.World()->IsGameWorld()
				&& Context.World()->GetNetMode() == (bServer ? NM_DedicatedServer : NM_Client)) { World = Context.World(); break; }
		if (!World) return true;
		auto* PC = World->GetFirstPlayerController();
		auto* Pawn = PC ? Cast<ARPGPlayer>(PC->GetPawn()) : nullptr;
		auto* PlayerState = PC ? PC->GetPlayerState<ARPGPlayerState>() : nullptr;
		if (!Pawn || !PlayerState || !PlayerState->HasAuthenticatedCharacter()) return true;
		if (PlayerState->GetBackendCharacterId() != Character || PlayerState->GetBackendDungeonSessionId() != Session
			|| PlayerState->GetAuthenticatedSteamId() != Steam) return Fail(TEXT("Native observer identity mismatch"), bServer);
		if (bServer)
		{
			if (bServerEnded) return false;
			if (EndedHandle.IsValid()) return true;
			auto* Skill = FindSkill(Pawn);
			if (!Skill) return true;
			auto* Component = Cast<URPGAbilitySystemComponent>(Pawn->GetAbilitySystemComponent());
			if (!Component) return Fail(TEXT("Missing authoritative ASC"), true);
			ASC = Component; ObservedSkill = Skill;
			ObservedHandle = Skill->GetCurrentAbilitySpecHandle(); ObservedActivation = Skill->GetSkillActivationSerial();
			const TWeakPtr<FProbe> Weak = AsShared();
			EndedHandle = Component->OnAbilityEnded.AddLambda([Weak](const FAbilityEndedData& Ended)
			{
				const auto Self = Weak.Pin();
				if (!Self || Ended.AbilityThatEnded.Get() != Self->ObservedSkill.Get() || Ended.AbilitySpecHandle != Self->ObservedHandle) return;
				if (const auto* Current = Self->ObservedSkill.Get(); !Current || Current->GetSkillActivationSerial() != Self->ObservedActivation) return;
				Self->bServerEnded = true;
				Self->Result->SetBoolField(TEXT("passed"), Ended.bWasCancelled);
				Self->Result->SetBoolField(TEXT("cancelled"), Ended.bWasCancelled);
				Self->Result->SetNumberField(TEXT("role"), ROLE_Authority);
				Self->Save(true);
				UE_LOG(LogTemp, Display, TEXT("RPG_ACTION_CANCEL_NATIVE SERVER_END Cancelled=%d"), Ended.bWasCancelled);
			});
			return true;
		}
		if (World->WorldType != EWorldType::PIE || Pawn->GetLocalRole() != ROLE_AutonomousProxy) return Fail(TEXT("Expected actual remote-client PIE"), false);
		if (!bCancelled && !IFileManager::Get().FileExists(*FPaths::Combine(Gates, TEXT("cancel.ready")))) return true;
		URPGActionProgressWidget* View = nullptr;
		for (TObjectIterator<URPGActionProgressWidget> It; It; ++It)
			if (It->GetWorld() == World && It->GetOwningPlayer() == PC && It->IsInViewport())
			{ if (View) return Fail(TEXT("Ambiguous owned progress view"), false); View = *It; }
		if (!View) return Fail(TEXT("Missing owned progress view"), false);
		if (!bCancelled)
		{
#if WITH_EDITOR
			if (GAllowActorScriptExecutionInEditor) return true;
#endif
			const auto Before = View->GetDisplayedProgress();
			if (!Before.bActive || Before.Data.Kind != ERPGActionProgressKind::Charge || Before.Data.Phase != ERPGActionProgressPhase::Running
				|| Before.Fraction <= 0 || Before.Fraction >= 1) return Fail(TEXT("Cancel gate outside running charge"), false);
			auto* Skill = FindSkill(Pawn);
			if (!Skill) return Fail(TEXT("Missing unique active charge"), false);
			Result->SetObjectField(TEXT("before"), Record(*View));
			Result->SetStringField(TEXT("ability"), Skill->GetPathName());
			Skill->CancelAbility(Skill->GetCurrentAbilitySpecHandle(), Skill->GetCurrentActorInfo(), Skill->GetCurrentActivationInfo(), true);
			if (!IsCancelled(*View)) return Fail(TEXT("Native GAS cancel did not clear product HUD"), false);
			bCancelled = true;
			Result->SetBoolField(TEXT("cancelled"), true);
			Result->SetBoolField(TEXT("uiStateInjected"), false);
			Result->SetBoolField(TEXT("editorScriptGuard"), false);
			Result->SetObjectField(TEXT("after"), Record(*View)); Save(false);
			UE_LOG(LogTemp, Display, TEXT("RPG_ACTION_CANCEL_NATIVE CLIENT_CANCEL Sent=1"));
		}
		if (!IsCancelled(*View)) return Fail(TEXT("Cancelled HUD was restored or overwritten"), false);
		if (IFileManager::Get().FileExists(*FPaths::Combine(Gates, TEXT("release-observed.ready"))))
		{
			if (ReleasedAt == 0) ReleasedAt = FPlatformTime::Seconds();
			if (!bSavedLate && FPlatformTime::Seconds() - ReleasedAt >= .25)
			{
				bSavedLate = true; Result->SetBoolField(TEXT("passed"), true);
				Result->SetBoolField(TEXT("lateReleaseStillHidden"), true);
				Result->SetObjectField(TEXT("lateRelease"), Record(*View)); Save(false);
			}
		}
		return true;
	}
};

static const FDelegateHandle Registration = FCoreDelegates::GetOnPostEngineInit().AddLambda([]
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("RPGActionProgressCancelPIE"))
		|| !FParse::Param(FCommandLine::Get(), TEXT("RPGTrainingPlaytest")) || !FParse::Param(FCommandLine::Get(), TEXT("nosteam"))) return;
	auto Probe = MakeShared<FProbe>();
	FParse::Value(FCommandLine::Get(), TEXT("RPGTrainingGates="), Probe->Gates);
	FParse::Value(FCommandLine::Get(), TEXT("RPGTrainingCharacter="), Probe->Character);
	FParse::Value(FCommandLine::Get(), TEXT("RPGTrainingSession="), Probe->Session);
	FParse::Value(FCommandLine::Get(), TEXT("RPGTrainingSteam="), Probe->Steam);
	FString Normalized = Probe->Gates; FPaths::NormalizeDirectoryName(Normalized);
	FGuid Id;
	if (FPaths::IsRelative(Normalized) || !Normalized.Contains(TEXT("/Saved/Tests/TrainingPlaytest/"))
		|| !FGuid::ParseExact(FPaths::GetCleanFilename(Normalized), EGuidFormats::Digits, Id)) return;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float Delta) { return Probe->Tick(Delta); }), .025f);
});
}
#endif
