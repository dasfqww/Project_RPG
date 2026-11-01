#include "RPGOptionsShortcutHint.h"

#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "RPGOptionsMenuComponent.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RPGOptionsUI"

URPGOptionsShortcutHint::URPGOptionsShortcutHint(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(false);
}

FText URPGOptionsShortcutHint::GetShortcutText() const
{
	const APlayerController* Controller = GetOwningPlayer();
	if (!IsValid(Controller) || Controller->IsActorBeingDestroyed() || !Controller->IsLocalController()
		|| !Controller->GetLocalPlayer() || GetOwningLocalPlayer() != Controller->GetLocalPlayer()
		|| Controller->GetWorld() != GetWorld()) { return FText::GetEmpty(); }
	const AHUD* HUD = Controller->GetHUD();
	if (!IsValid(HUD) || HUD->IsActorBeingDestroyed() || HUD->GetWorld() != GetWorld()
		|| HUD->GetOwningPlayerController() != Controller) { return FText::GetEmpty(); }
	const URPGOptionsMenuComponent* Options = HUD->FindComponentByClass<URPGOptionsMenuComponent>();
	if (!IsValid(Options)) { return FText::GetEmpty(); }
	const TArray<FKey> Keys = Options->GetShortcutKeys();
	if (Keys.IsEmpty()) { return FText::GetEmpty(); }
	TArray<FText> Labels;
	Labels.Reserve(Keys.Num());
	for (const FKey& Key : Keys) { Labels.Add(Key.GetDisplayName()); }
	return FText::Format(LOCTEXT("OptionsShortcutHint", "{0}  옵션"),
		FText::Join(LOCTEXT("OptionsShortcutSeparator", " / "), Labels));
}

TSharedRef<SWidget> URPGOptionsShortcutHint::RebuildWidget()
{
	const TWeakObjectPtr<URPGOptionsShortcutHint> WeakThis(this);
	return SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(24.f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			.BorderBackgroundColor(FLinearColor(0.02f, 0.025f, 0.035f, 0.82f))
			.Padding(FMargin(14.f, 8.f))
			.Visibility_Lambda([WeakThis]()
			{
				const URPGOptionsShortcutHint* Hint = WeakThis.Get();
				return Hint && !Hint->GetShortcutText().IsEmpty()
					? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			[
				SNew(STextBlock)
				.Text_Lambda([WeakThis]()
				{
					const URPGOptionsShortcutHint* Hint = WeakThis.Get();
					return Hint ? Hint->GetShortcutText() : FText::GetEmpty();
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
				.ColorAndOpacity(FLinearColor(0.92f, 0.94f, 0.98f))
			]
		];
}

#undef LOCTEXT_NAMESPACE
