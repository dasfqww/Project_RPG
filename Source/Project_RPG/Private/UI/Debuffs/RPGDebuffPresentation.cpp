#include "UI/Debuffs/RPGDebuffPresentation.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/Actor.h"
#include "Attribute/RPGAttributeSet.h"
#include "Combat/RPGCombatControlTypes.h"
#include "Combat/RPGStaggerTypes.h"
#include "Combat/RPGKnockbackTypes.h"
#include "Combat/RPGAirborneTypes.h"
#include "Combat/RPGSlowTypes.h"
#include "Combat/RPGBurnTypes.h"
#include "Combat/RPGDefenseReductionTypes.h"
#include "Combat/RPGDamageTakenIncreaseTypes.h"

TArray<FRPGHUDDebuff> RPGDebuffPresentation::Read(const UAbilitySystemComponent* Source, double LocalWorldTime)
{
	TArray<FRPGHUDDebuff> Out;
	if (!IsValid(Source) || !Source->IsRegistered() || !FMath::IsFinite(LocalWorldTime)
		|| !IsValid(Source->GetAvatarActor()) || Source->GetAvatarActor()->IsActorBeingDestroyed()) return Out;
	if (Source->HasAttributeSetForAttribute(URPGAttributeSet::GetCurrentHealthAttribute()))
	{
		const float Health = Source->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
		if (!FMath::IsFinite(Health) || Health <= 0.f) return Out;
	}
	const FGameplayTag Tags[] = {RPGStagger::StatusTag(), RPGCombatControl::StunTag(), RPGKnockback::StatusTag(),
		RPGAirborne::SequenceTag(), RPGSlow::StatusTag(), RPGBurn::StatusTag(),
		RPGDefenseReduction::StatusTag(), RPGDamageTakenIncrease::StatusTag()};
	for (const auto Handle : Source->GetActiveEffects(FGameplayEffectQuery()))
	{
		const auto* Effect = Source->GetActiveGameplayEffect(Handle);
		if (!Effect || Effect->bIsInhibited || !Effect->Spec.Def) continue;
		const bool bTimed = Effect->GetDuration() != FGameplayEffectConstants::INFINITE_DURATION;
		const double Left = bTimed ? Effect->GetTimeRemaining(float(LocalWorldTime)) : 0.;
		if (bTimed && (!FMath::IsFinite(Left) || Left <= 0.)) continue;
		FGameplayTagContainer Granted; Effect->Spec.GetAllGrantedTags(Granted);
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Tags); ++Index)
		{
			bool bMatch = Granted.HasTag(Tags[Index]);
			if (Index == int32(ERPGHUDDebuffKind::Knockdown))
				bMatch |= Granted.HasTag(RPGAirborne::AirborneTag()) || Granted.HasTag(RPGAirborne::DownedTag()) || Granted.HasTag(RPGAirborne::GettingUpTag());
			if (!bMatch) continue;
			const auto Kind = ERPGHUDDebuffKind(Index);
			auto* Item = Out.FindByPredicate([Kind](const FRPGHUDDebuff& Row) { return Row.Kind == Kind; });
			if (!Item) { Item = &Out.AddDefaulted_GetRef(); Item->Kind = Kind; Item->bTimed = bTimed; }
			// A knockdown sequence has an infinite ownership GE plus a finite down/get-up phase.
			// Prefer a real finite phase clock when present; never invent a landing countdown.
			if (bTimed) { Item->bTimed = true; Item->RemainingSeconds = FMath::Max(Item->RemainingSeconds, Left); }
		}
	}
	Out.Sort([](const FRPGHUDDebuff& A, const FRPGHUDDebuff& B) { return uint8(A.Kind) < uint8(B.Kind); });
	return Out;
}

TArray<FRPGHUDDebuff> RPGDebuffPresentation::StableOrder(const TArray<FRPGHUDDebuff>& Current, const TArray<FRPGHUDDebuff>& Previous)
{
	TArray<FRPGHUDDebuff> Out;
	for (const auto& Old : Previous)
		if (const auto* Item = Current.FindByPredicate([&](const FRPGHUDDebuff& Row) { return Row.Kind == Old.Kind; })) Out.Add(*Item);
	for (const auto& Item : Current)
		if (!Out.ContainsByPredicate([&](const FRPGHUDDebuff& Row) { return Row.Kind == Item.Kind; })) Out.Add(Item);
	return Out;
}

FText RPGDebuffPresentation::Name(ERPGHUDDebuffKind Kind)
{
	switch (Kind)
	{
	case ERPGHUDDebuffKind::Stagger: return NSLOCTEXT("RPGDebuffHUD", "Stagger", "경직");
	case ERPGHUDDebuffKind::Stun: return NSLOCTEXT("RPGDebuffHUD", "Stun", "기절");
	case ERPGHUDDebuffKind::Knockback: return NSLOCTEXT("RPGDebuffHUD", "Knockback", "넉백");
	case ERPGHUDDebuffKind::Knockdown: return NSLOCTEXT("RPGDebuffHUD", "Knockdown", "피격이상");
	case ERPGHUDDebuffKind::Slow: return NSLOCTEXT("RPGDebuffHUD", "Slow", "둔화");
	case ERPGHUDDebuffKind::Burn: return NSLOCTEXT("RPGDebuffHUD", "Burn", "화상");
	case ERPGHUDDebuffKind::DefenseReduction: return NSLOCTEXT("RPGDebuffHUD", "DefenseReduction", "방어력 감소");
	case ERPGHUDDebuffKind::DamageTakenIncrease: return NSLOCTEXT("RPGDebuffHUD", "DamageTakenIncrease", "받는 피해 증가");
	default: return FText::GetEmpty();
	}
}

FText RPGDebuffPresentation::TimeText(const FRPGHUDDebuff& Item)
{
	if (!Item.bTimed) return NSLOCTEXT("RPGDebuffHUD", "Untimed", "—");
	const double Left = FMath::IsFinite(Item.RemainingSeconds) ? FMath::Max(0., Item.RemainingSeconds) : 0.;
	FNumberFormattingOptions Format; Format.SetUseGrouping(false);
	if (Left < 10.)
	{
		Format.SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1);
		return FText::AsNumber(FMath::CeilToDouble(Left * 10.) / 10., &Format);
	}
	Format.SetMaximumFractionalDigits(0);
	return FText::AsNumber(FMath::CeilToDouble(Left), &Format);
}

FLinearColor RPGDebuffPresentation::Color(ERPGHUDDebuffKind Kind)
{
	switch (Kind)
	{
	case ERPGHUDDebuffKind::Slow: return FLinearColor(.3f,.8f,1.f);
	case ERPGHUDDebuffKind::Burn: return FLinearColor(1.f,.42f,.12f);
	case ERPGHUDDebuffKind::DefenseReduction: return FLinearColor(.68f,.52f,1.f);
	case ERPGHUDDebuffKind::DamageTakenIncrease: return FLinearColor(1.f,.25f,.37f);
	case ERPGHUDDebuffKind::Stun: return FLinearColor(1.f,.85f,.25f);
	default: return FLinearColor(.83f,.72f,1.f);
	}
}
