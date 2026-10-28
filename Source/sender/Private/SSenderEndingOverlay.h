#pragma once
#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Widgets/SCompoundWidget.h"
class SBorder;
class STextBlock;

/** Entire ending presentation is Slate; UMG HUDs are only hidden/restored by the subsystem. */
class SSenderEndingOverlay : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SSenderEndingOverlay) {}
        SLATE_ARGUMENT(FSlateFontInfo,TitleFont)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    void ShowPrompt(bool bShow);
    void ShowPage(const FString& Speaker,const FString& Text,const FString& Direction);
    void ShowFade(float Alpha,float TitleAlpha);
    void SetRevealCounts(int32 TitleCharacters,int32 CaptionCharacters);
private:
    TSharedRef<SWidget> MakeNeonText(const FString& FullText,const FSlateFontInfo& Font,const FLinearColor& Color,bool bIsTitle);
    TSharedPtr<STextBlock> Prompt;
    TSharedPtr<STextBlock> SpeakerText;
    TSharedPtr<STextBlock> DialogueText;
    TSharedPtr<STextBlock> DirectionText;
    TSharedPtr<SBorder> DialogueBackdrop;
    TSharedPtr<SBorder> BlackOverlay;
    TSharedPtr<SWidget> EndingCard;
    TSharedPtr<STextBlock> TitleText;
    TSharedPtr<STextBlock> TitleGlow;
    TSharedPtr<STextBlock> CaptionText;
    TSharedPtr<STextBlock> CaptionGlow;
    int32 LastTitleCount=INDEX_NONE;
    int32 LastCaptionCount=INDEX_NONE;
};
