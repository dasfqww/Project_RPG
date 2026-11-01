#include "Project_RPGEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EditorValidatorSubsystem.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "Kismet2/BlueprintEditorUtils.h"

void UProject_RPGEditorUtilities::CompileAllBlueprintsInPath(FString Path)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> AssetDataList;
	AssetRegistryModule.Get().GetAssetsByPath(*Path, AssetDataList, true);

	for (const FAssetData& AssetData : AssetDataList)
	{
		if (UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset()))
		{
			FKismetEditorUtilities::CompileBlueprint(BP);
		}
	}
}

void UProject_RPGEditorUtilities::RunAllValidations()
{
	if (!GEditor) return;

	TArray<FAssetData> AllAssets;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistryModule.Get().GetAssetsByPath(TEXT("/Game"), AllAssets, true);

	if (AllAssets.Num() > 0)
	{
		UEditorValidatorSubsystem* ValidationSubsystem = GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>();
		if (ValidationSubsystem)
		{
			FValidateAssetsSettings Settings;
			FValidateAssetsResults Results;
			ValidationSubsystem->ValidateAssetsWithSettings(AllAssets, Settings, Results);
		}
	}
}

bool UProject_RPGEditorUtilities::ConfigureNPCMeleeHitReactionBranch(UBlueprint* Blueprint)
{
	if (!Blueprint || Blueprint->GetPathName() !=
		TEXT("/Game/Blueprints/Character/Shared/GameplayAbility/GA_NPC_MeleeAttack_Base.GA_NPC_MeleeAttack_Base")) return false;
	UEdGraph* Graph = nullptr;
	for (UEdGraph* Candidate : Blueprint->FunctionGraphs)
		if (Candidate && Candidate->GetFName() == TEXT("HandleApplyDamage")) Graph = Candidate;
	if (!Graph) return false;
	UK2Node_CallFunction* Apply = nullptr;
	UK2Node_CallFunction* Send = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		auto* Call = Cast<UK2Node_CallFunction>(Node);
		if (!Call) continue;
		if (Call->FunctionReference.GetMemberName() == TEXT("BP_ApplyEffectSpecHandleToTarget"))
		{
			if (Apply) return false;
			Apply = Call;
		}
		if (Call->FunctionReference.GetMemberName() == TEXT("SendGameplayEventToActor"))
		{
			if (Send) return false;
			Send = Call;
		}
	}
	if (!Apply || !Send) return false;
	Blueprint->Modify();
	Graph->Modify();
	Apply->ReconstructNode();
	UEdGraphPin* Condition = Apply->FindPin(TEXT("bOutUseLegacyHitReaction"), EGPD_Output);
	UEdGraphPin* SendExec = Send->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
	if (!Condition || !SendExec || SendExec->LinkedTo.Num() != 1) return false;
	// Idempotent when rerunning the editor script on the migrated package.
	if (auto* Existing = Cast<UK2Node_IfThenElse>(SendExec->LinkedTo[0]->GetOwningNode()))
		return Existing->GetConditionPin()->LinkedTo.Contains(Condition)
			&& Existing->GetElsePin()->LinkedTo.IsEmpty();
	UEdGraphPin* Previous = SendExec->LinkedTo[0];
	const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
	if (!Schema || Previous->Direction != EGPD_Output || Previous->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) return false;
	auto* Branch = NewObject<UK2Node_IfThenElse>(Graph);
	Graph->AddNode(Branch, false, false);
	Branch->CreateNewGuid();
	Branch->PostPlacedNewNode();
	Branch->AllocateDefaultPins();
	Branch->NodePosX = Send->NodePosX - 220;
	Branch->NodePosY = Send->NodePosY;
	Previous->BreakLinkTo(SendExec);
	if (!Schema->TryCreateConnection(Previous, Branch->GetExecPin())
		|| !Schema->TryCreateConnection(Condition, Branch->GetConditionPin())
		|| !Schema->TryCreateConnection(Branch->GetThenPin(), SendExec)) return false;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return Blueprint->Status != BS_Error;
}
