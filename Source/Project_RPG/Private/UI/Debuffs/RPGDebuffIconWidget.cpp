#include "UI/Debuffs/RPGDebuffIconWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

class SRPGDebuffIcon : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRPGDebuffIcon) {} SLATE_END_ARGS()
	void Construct(const FArguments&) {}
	void SetKind(ERPGHUDDebuffKind Value) { Kind = Value; Invalidate(EInvalidateWidgetReason::Paint); }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(36,36); }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& List,
		int32 Layer, const FWidgetStyle& Style, bool) const override
	{
		const FVector2D Size = G.GetLocalSize();
		const FLinearColor Tint = Style.GetColorAndOpacityTint();
		const FLinearColor Color = RPGDebuffPresentation::Color(Kind) * Tint;
		FSlateDrawElement::MakeBox(List, Layer, G.ToPaintGeometry(), FCoreStyle::Get().GetBrush("WhiteBrush"),
			ESlateDrawEffect::None, FLinearColor(.025f,.035f,.055f,.94f) * Tint);
		auto Line = [&](std::initializer_list<FVector2D> Points, FLinearColor Ink, float Width = 2.f)
		{
			TArray<FVector2D> Scaled;
			for (const auto& P : Points) Scaled.Add(FVector2D(P.X * Size.X, P.Y * Size.Y));
			FSlateDrawElement::MakeLines(List, Layer + 1, G.ToPaintGeometry(), Scaled, ESlateDrawEffect::None, Ink, true, Width);
		};
		Line({{.02,.02},{.98,.02},{.98,.98},{.02,.98},{.02,.02}}, Color.CopyWithNewOpacity(.65f), 1.f);
		switch (Kind)
		{
		case ERPGHUDDebuffKind::Slow:
			Line({{.5,.18},{.5,.82}},Color); Line({{.22,.33},{.78,.67}},Color); Line({{.22,.67},{.78,.33}},Color);
			Line({{.38,.27},{.5,.38},{.62,.27}},Color); Line({{.38,.73},{.5,.62},{.62,.73}},Color); break;
		case ERPGHUDDebuffKind::Burn:
			Line({{.52,.13},{.68,.4},{.72,.28},{.84,.6},{.75,.79},{.5,.87},{.27,.77},{.2,.56},{.41,.28},{.4,.55},{.52,.13}},Color);
			Line({{.5,.48},{.62,.68},{.5,.8},{.39,.68},{.5,.48}},Color); break;
		case ERPGHUDDebuffKind::DefenseReduction:
			Line({{.5,.17},{.8,.28},{.75,.63},{.5,.84},{.25,.63},{.2,.28},{.5,.17}},Color);
			Line({{.58,.23},{.43,.44},{.61,.51},{.44,.77}},Color); break;
		case ERPGHUDDebuffKind::DamageTakenIncrease:
			Line({{.73,.18},{.8,.25},{.47,.65},{.35,.57},{.73,.18}},Color);
			Line({{.28,.52},{.57,.76}},Color); Line({{.42,.64},{.26,.82}},Color);
			Line({{.2,.25},{.2,.43}},Color); Line({{.11,.34},{.29,.34}},Color); break;
		case ERPGHUDDebuffKind::Stun:
			Line({{.6,.15},{.3,.54},{.51,.54},{.4,.86},{.75,.43},{.53,.43},{.6,.15}},Color); break;
		case ERPGHUDDebuffKind::Knockback:
			Line({{.18,.36},{.5,.36},{.5,.22},{.82,.5},{.5,.78},{.5,.64},{.18,.64}},Color); break;
		case ERPGHUDDebuffKind::Knockdown:
			Line({{.24,.48},{.5,.2},{.76,.48}},Color); Line({{.5,.2},{.5,.7}},Color);
			Line({{.22,.83},{.78,.83}},Color); break;
		default:
			Line({{.16,.55},{.34,.55},{.42,.27},{.55,.76},{.65,.45},{.84,.45}},Color); break;
		}
		return Layer + 1;
	}
private:
	ERPGHUDDebuffKind Kind = ERPGHUDDebuffKind::Slow;
};

void URPGDebuffIconWidget::SetKind(ERPGHUDDebuffKind Value) { Kind = Value; if (Icon) Icon->SetKind(Kind); }
TSharedRef<SWidget> URPGDebuffIconWidget::RebuildWidget() { SAssignNew(Icon, SRPGDebuffIcon); Icon->SetKind(Kind); return Icon.ToSharedRef(); }
void URPGDebuffIconWidget::ReleaseSlateResources(bool bReleaseChildren) { Super::ReleaseSlateResources(bReleaseChildren); Icon.Reset(); }
