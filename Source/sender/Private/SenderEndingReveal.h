#pragma once
#include "CoreMinimal.h"

/** Deterministic reveal timing; independent of frame rate or Slate layout. */
namespace SenderEndingReveal
{
    inline const FString& Title()
    {
        static const FString Value=TEXT("SENDER");return Value;
    }
    inline const FString& Caption()
    {
        static const FString Value=TEXT("I wish everyone could face the past with honesty and strive to embrace the future.");return Value;
    }
    struct FFrame
    {
        int32 TitleCount=0;
        int32 CaptionCount=0;
        bool bComplete=false;
    };
    inline float CaptionStart(float TitleFade,float TitleInterval,float Pause)
    {
        return FMath::Max(FMath::Max(TitleFade,.1f),Title().Len()*FMath::Max(TitleInterval,.01f))+FMath::Max(Pause,0.f);
    }
    inline float Duration(float TitleFade,float TitleInterval,float CaptionInterval,float Pause)
    {
        return CaptionStart(TitleFade,TitleInterval,Pause)+Caption().Len()*FMath::Max(CaptionInterval,.01f);
    }
    inline FFrame At(float Time,float TitleFade,float TitleInterval,float CaptionInterval,float Pause)
    {
        FFrame F;
        F.TitleCount=FMath::Clamp(FMath::FloorToInt(FMath::Max(Time,0.f)/FMath::Max(TitleInterval,.01f)),0,Title().Len());
        const float CaptionTime=Time-CaptionStart(TitleFade,TitleInterval,Pause);
        F.CaptionCount=FMath::Clamp(FMath::FloorToInt(FMath::Max(CaptionTime,0.f)/FMath::Max(CaptionInterval,.01f)),0,Caption().Len());
        F.bComplete=Time>=Duration(TitleFade,TitleInterval,CaptionInterval,Pause);
        if(F.bComplete) {F.TitleCount=Title().Len();F.CaptionCount=Caption().Len();}
        return F;
    }
}
