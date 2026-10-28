#include "SSenderEndingOverlay.h"
#include "SenderEndingReveal.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> SSenderEndingOverlay::MakeNeonText(const FString& Full,const FSlateFontInfo& Font,const FLinearColor& Color,bool IsTitle)
{
    // Cursive glyph ink and neon outlines can extend beyond their advance width.
    // Give them explicit safety space; never use font measurement as a wrap boundary.
    FSlateFontInfo GlowFont=Font;
    GlowFont.OutlineSettings.OutlineSize=IsTitle?5:3;
    GlowFont.OutlineSettings.OutlineColor=FLinearColor(Color.R,Color.G,Color.B,.12f);
    GlowFont.OutlineSettings.bSeparateFillAlpha=true;
    FSlateFontInfo MainFont=Font;
    MainFont.OutlineSettings.OutlineSize=1;
    MainFont.OutlineSettings.OutlineColor=FLinearColor(Color.R,Color.G,Color.B,.5f);
    const FVector2D InkSize=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Full,MainFont);
    const float SideRoom=FMath::Max(float(Font.Size)*.8f,32.f);
    const float ReserveWidth=float(InkSize.X)+2.f*SideRoom;
    const float ReserveHeight=float(InkSize.Y)*1.3f+48.f;
    TSharedPtr<STextBlock> Glow,Main;
    TSharedRef<SWidget> Result=SNew(SBox).WidthOverride(1400.f).HeightOverride(IsTitle?300.f:100.f).Padding(FMargin(32,8))
    [
        SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
        [
            SNew(SBox).WidthOverride(ReserveWidth).HeightOverride(ReserveHeight).Padding(FMargin(SideRoom,0))
            [
                SNew(SOverlay)
                +SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Center)
                [SNew(STextBlock).Text(FText::FromString(Full)).Font(MainFont)
                    .AutoWrapText(false).WrapTextAt(0.f).Justification(ETextJustify::Left)
                    .LineHeightPercentage(1.3f).Clipping(EWidgetClipping::Inherit).ColorAndOpacity(FLinearColor::Transparent)]
                +SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Center)
                [SAssignNew(Glow,STextBlock).Font(GlowFont)
                    .AutoWrapText(false).WrapTextAt(0.f).Justification(ETextJustify::Left)
                    .LineHeightPercentage(1.3f).Clipping(EWidgetClipping::Inherit).ColorAndOpacity(FLinearColor(Color.R,Color.G,Color.B,.16f))]
                +SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Center)
                [SAssignNew(Main,STextBlock).Font(MainFont)
                    .AutoWrapText(false).WrapTextAt(0.f).Justification(ETextJustify::Left)
                    .LineHeightPercentage(1.3f).Clipping(EWidgetClipping::Inherit).ColorAndOpacity(Color)]
            ]
        ]
    ];
    if(IsTitle) {TitleText=Main;TitleGlow=Glow;} else {CaptionText=Main;CaptionGlow=Glow;}
    return Result;
}
void SSenderEndingOverlay::Construct(const FArguments& Args)
{
    const FLinearColor White(.98f,.97f,.94f);
    const auto BodyFont=FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),28);
    TSharedRef<SWidget> Title=MakeNeonText(SenderEndingReveal::Title(),Args._TitleFont,FLinearColor(.18f,1.f,.88f),true);
    TSharedRef<SWidget> Caption=MakeNeonText(SenderEndingReveal::Caption(),FCoreStyle::GetDefaultFontStyle(TEXT("Italic"),24),FLinearColor(1.f,.32f,.78f),false);
    ChildSlot
    [
        SNew(SOverlay)
        +SOverlay::Slot()
        [
            SNew(SConstraintCanvas)
            +SConstraintCanvas::Slot().Anchors(FAnchors(.5f,.88f)).Alignment(FVector2D(.5,.5)).AutoSize(true)
            [SAssignNew(Prompt,STextBlock).Text(FText::FromString(TEXT("F   Talk"))).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),22)).ColorAndOpacity(FLinearColor(.95f,.9f,.75f))]
            +SConstraintCanvas::Slot().Anchors(FAnchors(.5f,.76f)).Alignment(FVector2D(.5,.5)).AutoSize(true)
            [
                SNew(SBox).WidthOverride(1000)
                [
                    SAssignNew(DialogueBackdrop,SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                    .BorderBackgroundColor(FLinearColor(.015f,.018f,.025f,.78f)).Padding(FMargin(32,20))
                    [
                        SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
                        [SAssignNew(SpeakerText,STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),19)).ColorAndOpacity(FLinearColor(.92f,.81f,.55f)).Justification(ETextJustify::Center)]
                        +SVerticalBox::Slot().AutoHeight()
                        [SAssignNew(DialogueText,STextBlock).Font(BodyFont).ColorAndOpacity(White).AutoWrapText(true).WrapTextAt(936).Justification(ETextJustify::Center).ShadowColorAndOpacity(FLinearColor(0,0,0,.8f)).ShadowOffset(FVector2D(1,2))]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,0)
                        [SAssignNew(DirectionText,STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Italic"),20)).ColorAndOpacity(FLinearColor(.7f,.72f,.75f)).Justification(ETextJustify::Center)]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,18,0,0)
                        [SNew(STextBlock).Text(FText::FromString(TEXT("F   Continue"))).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),16)).ColorAndOpacity(FLinearColor(.6f,.62f,.67f)).Justification(ETextJustify::Center)]
                    ]
                ]
            ]
        ]
        +SOverlay::Slot()
        [SAssignNew(BlackOverlay,SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor::Black)]
        +SOverlay::Slot().Padding(60,40)
        [
            SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
            [
                SAssignNew(EndingCard,SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Title]
                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,8,0,0)[Caption]
            ]
        ]
    ];
    Prompt->SetVisibility(EVisibility::Collapsed);DialogueBackdrop->SetVisibility(EVisibility::Collapsed);
    BlackOverlay->SetRenderOpacity(0.f);EndingCard->SetRenderOpacity(0.f);
    SetVisibility(EVisibility::HitTestInvisible);
    SetRevealCounts(0,0);
}
void SSenderEndingOverlay::ShowPrompt(bool Show)
{
    Prompt->SetVisibility(Show?EVisibility::HitTestInvisible:EVisibility::Collapsed);
}
void SSenderEndingOverlay::ShowPage(const FString& Speaker,const FString& Body,const FString& Direction)
{
    ShowPrompt(false);DialogueBackdrop->SetVisibility(EVisibility::HitTestInvisible);
    SpeakerText->SetText(FText::FromString(Speaker));DialogueText->SetText(FText::FromString(Body));DirectionText->SetText(FText::FromString(Direction));
    DirectionText->SetVisibility(Direction.IsEmpty()?EVisibility::Collapsed:EVisibility::HitTestInvisible);
}
void SSenderEndingOverlay::ShowFade(float Alpha,float TitleAlpha)
{
    ShowPrompt(false);DialogueBackdrop->SetVisibility(EVisibility::Collapsed);
    BlackOverlay->SetRenderOpacity(Alpha);EndingCard->SetRenderOpacity(TitleAlpha);
}
void SSenderEndingOverlay::SetRevealCounts(int32 TitleCount,int32 CaptionCount)
{
    if(TitleCount!=LastTitleCount)
    {
        LastTitleCount=TitleCount;const FText Value=FText::FromString(SenderEndingReveal::Title().Left(TitleCount));
        TitleText->SetText(Value);TitleGlow->SetText(Value);
    }
    if(CaptionCount!=LastCaptionCount)
    {
        LastCaptionCount=CaptionCount;const FText Value=FText::FromString(SenderEndingReveal::Caption().Left(CaptionCount));
        CaptionText->SetText(Value);CaptionGlow->SetText(Value);
    }
}
