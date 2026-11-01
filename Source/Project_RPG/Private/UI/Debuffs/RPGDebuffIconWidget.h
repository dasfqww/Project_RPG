#pragma once

#include "Components/Widget.h"
#include "UI/Debuffs/RPGDebuffPresentation.h"
#include "RPGDebuffIconWidget.generated.h"

/** Small vector pictogram; no font glyph, external texture, or editor brush dependency. */
UCLASS()
class URPGDebuffIconWidget : public UWidget
{
	GENERATED_BODY()
public:
	void SetKind(ERPGHUDDebuffKind Value);
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
private:
	ERPGHUDDebuffKind Kind = ERPGHUDDebuffKind::Slow;
	TSharedPtr<class SRPGDebuffIcon> Icon;
};
