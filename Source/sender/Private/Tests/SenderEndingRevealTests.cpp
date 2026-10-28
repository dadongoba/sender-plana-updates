#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../SenderEndingReveal.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSenderEndingRevealTest,"Sender.Ending.Typewriter",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSenderEndingRevealTest::RunTest(const FString& Parameters)
{
    using namespace SenderEndingReveal;
    auto Frame=[](float T){return At(T,2.f,.16f,.035f,.5f);};
    TestEqual(TEXT("Exact title"),Title(),FString(TEXT("SENDER")));
    TestEqual(TEXT("Exact requested message"),Caption(),FString(TEXT("I wish everyone could face the past with honesty and strive to embrace the future.")));
    TestEqual(TEXT("No title before first letter interval"),Frame(.15f).TitleCount,0);
    TestEqual(TEXT("First title letter"),Frame(.17f).TitleCount,1);
    TestEqual(TEXT("Full title by one second"),Frame(1.f).TitleCount,Title().Len());
    TestEqual(TEXT("Caption waits for title and pause"),Frame(2.49f).CaptionCount,0);
    TestEqual(TEXT("Caption begins one character at a time"),Frame(2.54f).CaptionCount,1);
    const float End=Duration(2.f,.16f,.035f,.5f);
    TestFalse(TEXT("Do not finish before last character"),Frame(End-.02f).bComplete);
    const SenderEndingReveal::FFrame Final=Frame(End+.01f);
    TestTrue(TEXT("Finishes after last character"),Final.bComplete);
    TestEqual(TEXT("Every caption character visible"),Final.CaptionCount,Caption().Len());
    // A stalled frame must catch up without losing letters or exceeding text length.
    const SenderEndingReveal::FFrame Jump=Frame(1000.f);
    TestEqual(TEXT("Large frame step preserves full title"),Jump.TitleCount,Title().Len());
    TestEqual(TEXT("Large frame step preserves full caption"),Jump.CaptionCount,Caption().Len());
    TestEqual(TEXT("Negative time reveals nothing"),Frame(-1.f).TitleCount,0);
    TestTrue(TEXT("Zero configuration is clamped safely"),At(1000.f,0.f,0.f,0.f,0.f).bComplete);
    return true;
}
#endif

