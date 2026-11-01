#include "UI/Lobby/RPGEquipmentGrowthModel.h"

#define LOCTEXT_NAMESPACE "RPGEquipmentGrowth"
namespace
{
	struct FRecipe { const TCHAR* Name; const TCHAR* Label; EEquipmentSlotType Slot; };
	const FRecipe Recipes[] = {
		{TEXT("Starter.GreatSword"), TEXT("시작 대검"), EEquipmentSlotType::Weapon_Primary_R},
		{TEXT("Starter.Chest"), TEXT("시작 상의"), EEquipmentSlotType::Chest},
		{TEXT("Starter.Gloves"), TEXT("시작 장갑"), EEquipmentSlotType::Hands},
		{TEXT("Target.EnduranceGreatSword"), TEXT("인내 대검"), EEquipmentSlotType::Weapon_Primary_R},
		{TEXT("Target.CirculationGreatSword"), TEXT("순환 대검"), EEquipmentSlotType::Weapon_Primary_R},
		{TEXT("Target.Chest"), TEXT("목표 상의"), EEquipmentSlotType::Chest},
		{TEXT("Target.Gloves"), TEXT("목표 장갑"), EEquipmentSlotType::Hands}
	};
	bool Active(const FRPGInventoryProjectionEntry& Item, FDateTime Now)
	{
		return Item.GetItemId().IsValid() && Item.GetRevision() > 0 && Item.GetQuantity() > 0
			&& Item.GetBindState() == ERPGItemBindState::CharacterBound && !Item.IsLocked()
			&& (Item.GetExpiresAtUtc() == FDateTime() || Item.GetExpiresAtUtc() > Now);
	}
	const FRecipe* Recipe(const FRPGInventoryProjectionEntry& Item, FDateTime Now)
	{
		if (!Active(Item, Now) || Item.GetQuantity() != 1 || Item.GetDefinitionVersion() != 1
			|| Item.GetDefinitionId().PrimaryAssetType != FPrimaryAssetType(TEXT("RPGItemDefinition"))
			|| !Item.GetEquipmentProgression().IsSet() || !Item.GetEquipmentProgression().IsValid()) return nullptr;
		for (const auto& Row : Recipes)
			if (Item.GetDefinitionId().PrimaryAssetName.ToString().Equals(FString(TEXT("GameItem.Equipment.Progression.")) + Row.Name, ESearchCase::CaseSensitive)
				&& (Item.GetContainerType() == ERPGItemContainerType::Inventory
					|| (Item.GetContainerType() == ERPGItemContainerType::Equipment && Item.GetSlotIndex() == static_cast<int32>(Row.Slot)))) return &Row;
		return nullptr;
	}
	bool Unique(const FRPGEquipmentMutationView& View)
	{
		TSet<FGuid> Seen;
		for (const auto& Item : View.Items) { if (!Item.GetItemId().IsValid() || Seen.Contains(Item.GetItemId())) return false; Seen.Add(Item.GetItemId()); }
		return true;
	}
	const FRPGInventoryProjectionEntry* Find(const FRPGEquipmentMutationView& View, FGuid Id)
	{ return View.Items.FindByPredicate([Id](const auto& Entry) { return Entry.GetItemId() == Id; }); }
	FRPGEquipmentItemExpectation Expect(const FRPGInventoryProjectionEntry& Item)
	{ FRPGEquipmentItemExpectation E; E.ItemId=Item.GetItemId(); E.ExpectedRevision=Item.GetRevision(); E.ExpectedLevel=Item.GetEquipmentProgression().Level; return E; }
	bool Equal(const FRPGEquipmentItemExpectation& A, const FRPGEquipmentItemExpectation& B)
	{ return A.ItemId==B.ItemId && A.ExpectedRevision==B.ExpectedRevision && A.ExpectedLevel==B.ExpectedLevel; }
	FRPGEquipmentGrowthPreview Invalid(const FText& Text) { FRPGEquipmentGrowthPreview Result; Result.Text=Text; return Result; }
	FText Stats(const FRPGInventoryProjectionEntry& Item, const FRecipe& Row, int32 AfterLevel)
	{
		FRPGEquipmentProgressionContribution Before, After;
		auto Growth=Item.GetEquipmentProgression();
		RPGEquipmentProgression::TryCalculate(Item.GetDefinitionId(),1,Row.Slot,Growth,Before);
		Growth.Level=AfterLevel;
		RPGEquipmentProgression::TryCalculate(Item.GetDefinitionId(),1,Row.Slot,Growth,After);
		return FText::Format(LOCTEXT("Stats", "장비 능력치: 공격력 {0} → {1}, 최대 체력 {2} → {3}"),
			FText::AsNumber(Before.Attack),FText::AsNumber(After.Attack),FText::AsNumber(Before.MaximumHealth),FText::AsNumber(After.MaximumHealth));
	}
}

bool FRPGEquipmentGrowthPreview::HasSameIntent(const FRPGEquipmentGrowthPreview& Other) const
{
	if (!bValid || !Other.bValid || Operation!=Other.Operation || !Equal(Source,Other.Source) || !Equal(Target,Other.Target)
		|| OreCost!=Other.OreCost || Costs.Num()!=Other.Costs.Num()) return false;
	for (int32 I=0;I<Costs.Num();++I) if (Costs[I].ItemId!=Other.Costs[I].ItemId || Costs[I].ExpectedRevision!=Other.Costs[I].ExpectedRevision) return false;
	return true;
}

bool RPGEquipmentGrowthUI::CanStartNew(const FRPGEquipmentMutationView& View)
{
	return View.CharacterId.IsValid() && View.ItemsLoadState==ERPGInventoryProjectionLoadState::Ready && !View.bNeedsInventoryRefresh
		&& (View.State==ERPGEquipmentMutationState::Idle || View.State==ERPGEquipmentMutationState::Committed || View.State==ERPGEquipmentMutationState::Rejected);
}
bool RPGEquipmentGrowthUI::CanRetry(const FRPGEquipmentMutationView& View)
{
	return View.RequestId.IsValid() && (View.State==ERPGEquipmentMutationState::RecoveryRequired || View.State==ERPGEquipmentMutationState::AuthenticationRequired);
}
TArray<FRPGEquipmentGrowthChoice> RPGEquipmentGrowthUI::Choices(const FRPGEquipmentMutationView& View, FDateTime Now)
{
	TArray<FRPGEquipmentGrowthChoice> Result;
	if (View.ItemsLoadState!=ERPGInventoryProjectionLoadState::Ready || View.bNeedsInventoryRefresh || !Unique(View)) return Result;
	for (const auto& Item : View.Items) if (const auto* Row=Recipe(Item,Now))
	{
		FRPGEquipmentGrowthChoice Choice; Choice.ItemId=Item.GetItemId(); Choice.Slot=Row->Slot; Choice.Level=Item.GetEquipmentProgression().Level;
		Choice.Label=FText::Format(LOCTEXT("Choice", "{0} +{1} · {2} {3}"), FText::FromString(Row->Label),FText::AsNumber(Choice.Level),
			Item.GetContainerType()==ERPGItemContainerType::Equipment ? LOCTEXT("Equipped","장착 칸") : LOCTEXT("Inventory","가방 칸"),FText::AsNumber(Item.GetSlotIndex()+1));
		Result.Add(Choice);
	}
	Result.Sort([](const auto& A,const auto& B) { return A.ItemId.ToString()<B.ItemId.ToString(); });
	return Result;
}

FRPGEquipmentGrowthPreview RPGEquipmentGrowthUI::Upgrade(const FRPGEquipmentMutationView& View, FGuid Id, FDateTime Now)
{
	if (!CanStartNew(View) || !Unique(View)) return Invalid(LOCTEXT("ReadFirst","최신 장비를 읽고 이전 작업의 결과를 확인하세요."));
	const auto* Item=Find(View,Id); const auto* Row=Item ? Recipe(*Item,Now) : nullptr;
	if (!Row) return Invalid(LOCTEXT("Select","강화할 장비를 선택하세요. 잠금·만료·보관함 장비는 사용할 수 없습니다."));
	if (Item->GetEquipmentProgression().Level==3) return Invalid(LOCTEXT("Maximum","이번 성장 구간의 최대 단계인 +3입니다. 추가 강화는 필요하지 않습니다."));
	FRPGEquipmentGrowthPreview Result; Result.Operation=ERPGEquipmentMutationOperation::Upgrade; Result.Source=Expect(*Item);
	Result.OreCost=(Result.Source.ExpectedLevel+1)*2;
	TArray<const FRPGInventoryProjectionEntry*> Ore;
	for (const auto& Entry : View.Items)
		if (Active(Entry,Now) && Entry.GetContainerType()==ERPGItemContainerType::Inventory && Entry.GetDefinitionVersion()==1
			&& Entry.GetDefinitionId()==FPrimaryAssetId(TEXT("RPGItemDefinition"),TEXT("GameItem.Material.Progression.Ore"))) Ore.Add(&Entry);
	Ore.Sort([](const auto& A,const auto& B) { return A.GetItemId().ToString()<B.GetItemId().ToString(); });
	int32 Remaining=Result.OreCost;
	for (const auto* Stack : Ore)
	{
		if (Remaining<=0 || Result.Costs.Num()==15) break;
		FRPGEquipmentCostExpectation Cost; Cost.ItemId=Stack->GetItemId(); Cost.ExpectedRevision=Stack->GetRevision(); Result.Costs.Add(Cost);
		Remaining-=FMath::Min(Remaining,Stack->GetQuantity());
	}
	if (Remaining>0) return Invalid(FText::Format(LOCTEXT("OreMissing","사용할 수 있는 광석 {0}개가 필요합니다. 가방의 잠금·만료 상태를 확인하세요."),FText::AsNumber(Result.OreCost)));
	Result.bValid=true;
	Result.Text=FText::Format(LOCTEXT("Upgrade","{0}: +{1} → +{2}\n광석 {3}개 · 성공률 100%\n{4}"),FText::FromString(Row->Label),
		FText::AsNumber(Result.Source.ExpectedLevel),FText::AsNumber(Result.Source.ExpectedLevel+1),FText::AsNumber(Result.OreCost),Stats(*Item,*Row,Result.Source.ExpectedLevel+1));
	return Result;
}

FRPGEquipmentGrowthPreview RPGEquipmentGrowthUI::Transfer(const FRPGEquipmentMutationView& View, FGuid Source, FGuid Target, FDateTime Now)
{
	if (!CanStartNew(View) || !Unique(View)) return Invalid(LOCTEXT("ReadFirst","최신 장비를 읽고 이전 작업의 결과를 확인하세요."));
	const auto* A=Find(View,Source); const auto* B=Find(View,Target);
	const auto* RA=A ? Recipe(*A,Now) : nullptr; const auto* RB=B ? Recipe(*B,Now) : nullptr;
	if (!RA || !RB || Source==Target) return Invalid(LOCTEXT("TwoItems","강화를 옮길 원본과 받을 장비를 각각 선택하세요."));
	if (RA->Slot!=RB->Slot) return Invalid(LOCTEXT("SameSlot","같은 부위의 장비끼리만 이전할 수 있습니다."));
	if (A->GetEquipmentProgression().Level==0 || B->GetEquipmentProgression().Level!=0)
		return Invalid(LOCTEXT("TransferLevels","강화된 원본과 +0인 대상 장비가 필요합니다. 강화 단계는 합산되지 않습니다."));
	FRPGEquipmentGrowthPreview Result; Result.bValid=true; Result.Operation=ERPGEquipmentMutationOperation::Transfer;
	Result.Source=Expect(*A); Result.Target=Expect(*B);
	Result.Text=FText::Format(LOCTEXT("Transfer","원본 {0}: +{1} → +0\n대상 {2}: +0 → +{1}\n비용 없음 · 두 장비와 각각의 고유 효과는 유지됩니다. 장착 상태는 바뀌지 않습니다.\n원본 {3}\n대상 {4}"),
		FText::FromString(RA->Label),FText::AsNumber(Result.Source.ExpectedLevel),FText::FromString(RB->Label),Stats(*A,*RA,0),Stats(*B,*RB,Result.Source.ExpectedLevel));
	return Result;
}

FText RPGEquipmentGrowthUI::Status(const FRPGEquipmentMutationView& View)
{
	switch (View.State)
	{
	case ERPGEquipmentMutationState::AwaitingServer: return LOCTEXT("Waiting","서버에서 작업 결과를 확인하고 있습니다.");
	case ERPGEquipmentMutationState::RecoveryRequired: return LOCTEXT("Recovery","이전 작업의 결과가 아직 확인되지 않았습니다. 원래 요청의 결과를 다시 확인하세요.");
	case ERPGEquipmentMutationState::AuthenticationRequired: return LOCTEXT("Auth","같은 계정으로 다시 로그인한 뒤 이전 작업의 결과를 확인하세요.");
	case ERPGEquipmentMutationState::StorageBlocked: return LOCTEXT("Storage","이전 작업을 안전하게 보관하거나 복구하지 못해 새 작업을 잠갔습니다.");
	case ERPGEquipmentMutationState::Unavailable: return LOCTEXT("Unavailable","로비에서 로그인과 캐릭터 선택을 확인하세요.");
	default: break;
	}
	if (View.ItemsLoadState==ERPGInventoryProjectionLoadState::Loading) return LOCTEXT("Loading","최신 장비를 읽고 있습니다.");
	if (View.bNeedsInventoryRefresh || View.ItemsLoadState!=ERPGInventoryProjectionLoadState::Ready) return LOCTEXT("Reload","장비 새로고침을 눌러 최신 상태를 확인하세요.");
	if (View.State==ERPGEquipmentMutationState::Rejected) return LOCTEXT("Rejected","작업이 적용되지 않았습니다. 최신 장비와 이용 조건을 확인하고 다시 선택하세요.");
	if (View.State==ERPGEquipmentMutationState::Committed) return LOCTEXT("Committed","작업 결과와 최신 장비 상태를 확인했습니다.");
	return LOCTEXT("Ready","장비를 선택하면 비용과 변경 결과를 확인할 수 있습니다.");
}
#undef LOCTEXT_NAMESPACE
