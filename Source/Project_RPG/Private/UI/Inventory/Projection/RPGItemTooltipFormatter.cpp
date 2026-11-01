#include "UI/Inventory/Projection/RPGItemTooltipFormatter.h"

#include "Item/Projection/RPGInventoryProjectionTypes.h"

FText FRPGItemTooltipFormatter::BuildInstanceDetails(
	const FRPGInventoryProjectionEntry& Entry, const int32 MaximumStackSize)
{
	if (!Entry.GetItemId().IsValid()) return FText::GetEmpty();

	TArray<FText> Lines;
	Lines.Add(MaximumStackSize > 1
		? FText::Format(NSLOCTEXT("RPGItemUI", "TooltipQuantityWithLimit", "Quantity: {0} / {1}"),
			FText::AsNumber(Entry.GetQuantity()), FText::AsNumber(MaximumStackSize))
		: FText::Format(NSLOCTEXT("RPGItemUI", "TooltipQuantity", "Quantity: {0}"),
			FText::AsNumber(Entry.GetQuantity())));

	FText Binding;
	switch (Entry.GetBindState())
	{
	case ERPGItemBindState::Unbound:
		Binding = NSLOCTEXT("RPGItemUI", "BindingUnbound", "Unbound");
		break;
	case ERPGItemBindState::BindOnEquip:
		Binding = NSLOCTEXT("RPGItemUI", "BindingOnEquip", "Binds on equip");
		break;
	case ERPGItemBindState::CharacterBound:
		Binding = NSLOCTEXT("RPGItemUI", "BindingCharacter", "Character-bound");
		break;
	case ERPGItemBindState::AccountBound:
		Binding = NSLOCTEXT("RPGItemUI", "BindingAccount", "Account-bound");
		break;
	default:
		Binding = NSLOCTEXT("RPGItemUI", "BindingUnknown", "Unknown");
		break;
	}
	Lines.Add(FText::Format(NSLOCTEXT("RPGItemUI", "TooltipBinding", "Binding: {0}"), Binding));
	const FRPGEquipmentProgressionState& Growth = Entry.GetEquipmentProgression();
	if (Growth.IsSet() && Growth.IsValid())
	{
		// Display the persisted opt-in only. This is not an eligibility decision,
		// an implicit legacy +0, or a calculation/application of combat stats.
		Lines.Add(FText::Format(NSLOCTEXT("RPGItemUI", "TooltipEquipmentProgression", "Enhancement: +{0}"),
			FText::AsNumber(Growth.Level)));
	}
	if (Entry.IsLocked())
	{
		Lines.Add(NSLOCTEXT("RPGItemUI", "TooltipLocked", "Locked: item actions unavailable"));
	}
	const FRPGItemDurability& Durability = Entry.GetDurability();
	if (Durability.IsTracked())
	{
		Lines.Add(FText::Format(NSLOCTEXT("RPGItemUI", "TooltipDurability", "Durability: {0} / {1}"),
			FText::AsNumber(Durability.Current), FText::AsNumber(Durability.Maximum)));
	}
	if (Entry.GetExpiresAtUtc().GetTicks() > 0)
	{
		// Display the server timestamp, not a local-clock eligibility decision or countdown.
		Lines.Add(FText::Format(NSLOCTEXT("RPGItemUI", "TooltipExpiryUtc", "Expires (UTC): {0}"),
			FText::FromString(Entry.GetExpiresAtUtc().ToIso8601())));
	}

	TArray<FRPGItemStatValue> Stats = Entry.GetRolledStats();
	Stats.Sort([](const FRPGItemStatValue& Left, const FRPGItemStatValue& Right)
		{ return Left.StatTag.ToString() < Right.StatTag.ToString(); });
	TArray<FText> StatLines;
	FNumberFormattingOptions NumberFormat;
	NumberFormat.SetUseGrouping(false).SetMinimumFractionalDigits(0).SetMaximumFractionalDigits(6);
	for (const FRPGItemStatValue& Stat : Stats)
	{
		if (!Stat.StatTag.IsValid() || !FMath::IsFinite(Stat.Value)) continue;
		// Keep the full option identity and authored value. Tags alone do not say
		// whether a value is a percentage, flat bonus, multiplier or derived stat.
		StatLines.Add(FText::Format(NSLOCTEXT("RPGItemUI", "TooltipRolledStat", "{0}: {1}"),
			FText::FromName(Stat.StatTag.GetTagName()), FText::AsNumber(Stat.Value, &NumberFormat)));
	}
	if (!StatLines.IsEmpty())
	{
		Lines.Add(NSLOCTEXT("RPGItemUI", "TooltipRolledStats", "Item options:"));
		Lines.Append(StatLines);
	}
	return FText::Join(FText::FromString(TEXT("\n")), Lines);
}

FText FRPGItemTooltipFormatter::JoinDetails(const FText& DisplayName,
	const FText& Description, const FText& InstanceDetails)
{
	TArray<FText> Sections;
	if (!DisplayName.IsEmpty()) Sections.Add(DisplayName);
	if (!Description.IsEmpty()) Sections.Add(Description);
	if (!InstanceDetails.IsEmpty()) Sections.Add(InstanceDetails);
	if (Sections.IsEmpty()) return FText::GetEmpty();
	return FText::Join(FText::FromString(TEXT("\n\n")), Sections);
}
