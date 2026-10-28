#include "SenderEndingSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "SSenderEndingOverlay.h"
#include "SenderEndingReveal.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/InputComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fonts/CompositeFont.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSenderEnding,Log,All);
namespace
{
    struct FPage { FString Speaker; FString Text; FString Direction; };
    const TArray<FPage>& Pages()
    {
        static const TArray<FPage> Data={
            {TEXT("Me"),TEXT("Someone asked me to pass a message to you—someone who knows your story over these past few years."),TEXT("")},
            {TEXT("Me"),TEXT("You’re about to quit your job to pursue your dreams."),TEXT("")},
            {TEXT("Me"),TEXT("But you’re afraid that when you look back at age thirty, you’ll find you have nothing to show for it."),TEXT("")},
            {TEXT("He"),TEXT("Who are you?"),TEXT("His expression shifted.")},
            {TEXT("Me"),TEXT("Because that person... is you, ten years from now."),TEXT("He froze.")},
            {TEXT("Me"),TEXT("He asked me to tell you: you actually went through with it."),TEXT("")},
            {TEXT("Me"),TEXT("The first two years were incredibly tough, but you persevered—and you found genuine happiness in what you were doing."),TEXT("")},
            {TEXT("Me"),TEXT("He wanted me to let you know—don’t be afraid."),TEXT("")},
            {TEXT("Me"),TEXT("The path you’re hesitant to take is the right one."),TEXT("")},
            {TEXT("Me"),TEXT("And there’s no need to prove anything to anyone—not even to yourself."),TEXT("")},
            {TEXT("He"),TEXT("Did I... did I end up becoming the person he wanted me to be?"),TEXT("")},
            {TEXT("Me"),TEXT("You don’t need to become who anyone else wants you to be."),TEXT("")},
            {TEXT("Me"),TEXT("You only need to become yourself."),TEXT("")},
            {TEXT("Me"),TEXT("And that person—standing on the other side of time—is happy for you."),TEXT("")}
        };
        return Data;
    }
    float Smooth(float T) { T=FMath::Clamp(T,0.f,1.f);return T*T*(3.f-2.f*T); }
}
USenderEndingSubsystem::USenderEndingSubsystem()
{
    static ConstructorHelpers::FObjectFinder<UObject> Typeface(TEXT("/Game/Game/Fonts/VujahdayScript-Regular.VujahdayScript-Regular"));
    CursiveTypeface=Typeface.Object;
}

bool USenderEndingSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* W=Cast<UWorld>(Outer);return W && W->IsGameWorld();
}
void USenderEndingSubsystem::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);State=ESenderEndingState::Waiting;PageIndex=INDEX_NONE;
    World.GetTimerManager().SetTimer(FindTimer,this,&USenderEndingSubsystem::FindParticipants,.5f,true);
}
TStatId USenderEndingSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(USenderEndingSubsystem,STATGROUP_Tickables);
}
void USenderEndingSubsystem::FindParticipants()
{
    if(State!=ESenderEndingState::Waiting) return;
    PlayerController=UGameplayStatics::GetPlayerController(GetWorld(),0);
    if(!IsValid(NPC))
        for(TActorIterator<ACharacter> It(GetWorld());It;++It)
            if(It->GetClass()->GetName().StartsWith(TEXT("ALS_Taro_CharacterBP_C")))
            {NPC=*It;UE_LOG(LogSenderEnding,Log,TEXT("Final NPC found: %s"),*NPC->GetName());break;}
    if(IsValid(NPC)) WarmUpTalkingBody();
    if(IsValid(NPC) && PlayerController && PlayerController->IsLocalController() && !EndingUI)
    {
        UGameViewportClient* Viewport=GetWorld()->GetGameViewport();
        if(!Viewport) return;
        FSlateFontInfo TitleFont=FCoreStyle::GetDefaultFontStyle(TEXT("Italic"),110);
        if(Cast<UFont>(CursiveTypeface)) TitleFont=FSlateFontInfo(CursiveTypeface,130);
        else if(Cast<UFontFace>(CursiveTypeface))
        {
            TSharedRef<FCompositeFont> Composite=MakeShared<FCompositeFont>();
            FTypefaceEntry Entry(TEXT("Regular"));Entry.Font=FFontData(CursiveTypeface);
            Composite->DefaultTypeface.Fonts.Add(Entry);
            TitleFont=FSlateFontInfo(Composite,130,TEXT("Regular"));
        }
        EndingUI=SNew(SSenderEndingOverlay).TitleFont(TitleFont);
        TWeakObjectPtr<UGameViewportClient> WeakViewport=Viewport;
        EndingLayer=SNew(SDPIScaler).DPIScale_Lambda([WeakViewport]()
        {
            FVector2D Size(1920,1080);
            if(WeakViewport.IsValid()) WeakViewport->GetViewportSize(Size);
            return GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(FMath::RoundToInt(Size.X),FMath::RoundToInt(Size.Y)));
        })[EndingUI.ToSharedRef()];
        Viewport->AddViewportWidgetContent(EndingLayer.ToSharedRef(),10000);EndingViewport=Viewport;
        UE_LOG(LogSenderEnding,Log,TEXT("Native Slate ending ready: cursive=%s; title and caption typewriter."),CursiveTypeface?*CursiveTypeface->GetClass()->GetName():TEXT("fallback"));
        EndingInput=NewObject<UInputComponent>(PlayerController,TEXT("SenderEndingInput"));EndingInput->RegisterComponent();EndingInput->Priority=1000;
        FInputKeyBinding& Binding=EndingInput->BindKey(EKeys::F,IE_Pressed,this,&USenderEndingSubsystem::Interact);
        Binding.bConsumeInput=true;
    }
}
float USenderEndingSubsystem::GetSurfaceDistance() const
{
    const ACharacter* Player=PlayerController?Cast<ACharacter>(PlayerController->GetPawn()):nullptr;
    if(!Player || !IsValid(NPC)) return TNumericLimits<float>::Max();
    const FVector Delta=Player->GetActorLocation()-NPC->GetActorLocation();
    if(FMath::Abs(Delta.Z)>120.f) return TNumericLimits<float>::Max();
    const float Radii=Player->GetCapsuleComponent()->GetScaledCapsuleRadius()+NPC->GetCapsuleComponent()->GetScaledCapsuleRadius();
    return FMath::Max(0.f,float(Delta.Size2D())-Radii);
}
void USenderEndingSubsystem::SetInputActive(bool Active)
{
    if(!PlayerController || !EndingInput || Active==bInputPushed) return;
    if(Active) PlayerController->PushInputComponent(EndingInput);else PlayerController->PopInputComponent(EndingInput);
    bInputPushed=Active;
}
void USenderEndingSubsystem::Tick(float DT)
{
    Super::Tick(DT);
    if(!EndingUI) return;
    if(State==ESenderEndingState::Waiting)
    {
        const bool Near=GetSurfaceDistance()<=FMath::Max(InteractionDistance,0.f);
        EndingUI->ShowPrompt(Near);SetInputActive(Near);
    }
    else if(State==ESenderEndingState::FadeToBlack)
    {
        PhaseTime+=DT;EndingUI->ShowFade(Smooth(PhaseTime/FMath::Max(FadeSeconds,.1f)),0.f);
        if(PhaseTime>=FMath::Max(FadeSeconds,.1f)+.6f)
        {State=ESenderEndingState::Title;PhaseTime=0.f;UE_LOG(LogSenderEnding,Log,TEXT("Scene black; SENDER title fade begins."));}
    }
    else if(State==ESenderEndingState::Title)
    {
        PhaseTime+=DT;
        const SenderEndingReveal::FFrame Reveal=SenderEndingReveal::At(PhaseTime,TitleFadeSeconds,TitleLetterInterval,CaptionLetterInterval,CaptionStartDelay);
        EndingUI->ShowFade(1.f,Smooth(PhaseTime/FMath::Max(TitleFadeSeconds,.1f)));
        EndingUI->SetRevealCounts(Reveal.TitleCount,Reveal.CaptionCount);
        if(Reveal.bComplete)
        {
            State=ESenderEndingState::Complete;EndingUI->ShowFade(1.f,1.f);
            UE_LOG(LogSenderEnding,Log,TEXT("Slate ending complete: title=%s caption=%s; all characters visible."),*SenderEndingReveal::Title(),*SenderEndingReveal::Caption());
        }
    }
}
void USenderEndingSubsystem::Interact()
{
    if(!GetWorld() || GetWorld()->GetTimeSeconds()-LastAdvanceTime<.2f) return;
    if(State==ESenderEndingState::Waiting)
    {if(GetSurfaceDistance()<=FMath::Max(InteractionDistance,0.f)) BeginDialogue();}
    else if(State==ESenderEndingState::Dialogue)
    {
        LastAdvanceTime=GetWorld()->GetTimeSeconds();++PageIndex;
        if(Pages().IsValidIndex(PageIndex)) DisplayPage();else StartEnding();
    }
}
void USenderEndingSubsystem::BeginDialogue()
{
    ACharacter* Player=PlayerController?Cast<ACharacter>(PlayerController->GetPawn()):nullptr;
    if(!Player || !IsValid(NPC)) return;
    const FVector NPCPos=NPC->GetActorLocation();
    FVector Out=Player->GetActorLocation()-NPCPos;Out.Z=0;
    if(!Out.Normalize()) Out=NPC->GetActorForwardVector();
    const float Radius=Player->GetCapsuleComponent()->GetScaledCapsuleRadius()+NPC->GetCapsuleComponent()->GetScaledCapsuleRadius()+InteractionDistance;
    FVector Position=NPCPos+Out*Radius;Position.Z=Player->GetActorLocation().Z;
    Player->GetCharacterMovement()->StopMovementImmediately();NPC->GetCharacterMovement()->StopMovementImmediately();
    Player->SetActorLocationAndRotation(Position,(-Out).Rotation(),false,nullptr,ETeleportType::TeleportPhysics);
    NPC->SetActorRotation(Out.Rotation());
    PlayerController->SetControlRotation(FRotator(-5.f,(-Out).Rotation().Yaw,0.f));
    PlayerController->SetIgnoreMoveInput(true);PlayerController->SetIgnoreLookInput(true);bOwnsInputLock=true;
    SavedNPCMovementMode=NPC->GetCharacterMovement()->MovementMode;SavedNPCCustomMode=NPC->GetCharacterMovement()->CustomMovementMode;
    bNPCTickWasEnabled=NPC->GetCharacterMovement()->IsComponentTickEnabled();NPC->GetCharacterMovement()->DisableMovement();NPC->GetCharacterMovement()->SetComponentTickEnabled(false);
    EndingInput->bBlockInput=true;SetInputActive(true);
    TArray<UUserWidget*> Widgets;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,Widgets,UUserWidget::StaticClass(),true);
    for(UUserWidget* W:Widgets) if(W)
    {HiddenHUD.Add(W);SavedHUDVisibility.Add(W->GetVisibility());W->SetVisibility(ESlateVisibility::Hidden);}
    State=ESenderEndingState::Dialogue;PageIndex=0;LastAdvanceTime=GetWorld()->GetTimeSeconds();
    UE_LOG(LogSenderEnding,Log,TEXT("Dialogue started: surface gap=%.3fcm pages=%d"),GetSurfaceDistance(),Pages().Num());DisplayPage();
}
void USenderEndingSubsystem::DisplayPage()
{
    if(!EndingUI || !Pages().IsValidIndex(PageIndex)) return;
    const FPage& P=Pages()[PageIndex];UpdateSpeakingAnimation(P.Speaker==TEXT("He"));EndingUI->ShowPage(P.Speaker,P.Text,P.Direction);
    UE_LOG(LogSenderEnding,Log,TEXT("Dialogue page %d/%d [%s]: %s"),PageIndex+1,Pages().Num(),*P.Speaker,*P.Text);
}
void USenderEndingSubsystem::StartEnding()
{
    PlayIdleAnimation();
    State=ESenderEndingState::FadeToBlack;PhaseTime=0.f;EndingUI->SetRevealCounts(0,0);EndingUI->ShowFade(0.f,0.f);
    UE_LOG(LogSenderEnding,Log,TEXT("Dialogue finished; scene fade started, %.2fs."),FadeSeconds);
}
void USenderEndingSubsystem::RestorePresentation()
{
    PlayIdleAnimation();
    if(bOwnsInputLock && IsValid(PlayerController))
    {PlayerController->SetIgnoreMoveInput(false);PlayerController->SetIgnoreLookInput(false);bOwnsInputLock=false;}
    if(IsValid(NPC) && State!=ESenderEndingState::Waiting)
    {NPC->GetCharacterMovement()->SetMovementMode(EMovementMode(SavedNPCMovementMode),SavedNPCCustomMode);NPC->GetCharacterMovement()->SetComponentTickEnabled(bNPCTickWasEnabled);}
    for(int32 I=0;I<HiddenHUD.Num();++I) if(IsValid(HiddenHUD[I]) && SavedHUDVisibility.IsValidIndex(I)) HiddenHUD[I]->SetVisibility(SavedHUDVisibility[I]);
    HiddenHUD.Reset();SavedHUDVisibility.Reset();
}
void USenderEndingSubsystem::Deinitialize()
{
    if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(FindTimer);
    SetInputActive(false);RestorePresentation();
    if(EndingInput) EndingInput->DestroyComponent();
    if(EndingViewport.IsValid() && EndingLayer.IsValid()) EndingViewport->RemoveViewportWidgetContent(EndingLayer.ToSharedRef());
    EndingLayer.Reset();EndingUI.Reset();EndingViewport.Reset();
    EndingInput=nullptr;PlayerController=nullptr;NPC=nullptr;
    Super::Deinitialize();
}

void USenderEndingSubsystem::EnsureTalkingAssets()
{
    if(TalkingBody) return;
    if(!IsValid(NPC)) return;
    TArray<USkeletalMeshComponent*> Meshes;NPC->GetComponents(Meshes);
    for(USkeletalMeshComponent* M:Meshes) if(M->GetName()==TEXT("TaroMesh-Combined")) {TalkingBody=M;break;}
    TalkingIdle=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Characters/MetahumanTaro/Anims/Taro_Idle_Breathe.Taro_Idle_Breathe"));
    TalkQuestion=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Characters/MetahumanTaro/Anims/Talk/Taro_Talk_Question.Taro_Talk_Question"));
    TalkDoubt=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Characters/MetahumanTaro/Anims/Talk/Taro_Talk_Doubt.Taro_Talk_Doubt"));
    ListenLoop=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Characters/MetahumanTaro/Anims/Talk/Taro_Listen_Loop.Taro_Listen_Loop"));
    if(!TalkingIdle) UE_LOG(LogSenderEnding,Warning,TEXT("Missing idle breathing clip"));
}
void USenderEndingSubsystem::PlayIdleAnimation()
{
    EnsureTalkingAssets();
    if(TalkingBody && TalkingIdle) TalkingBody->PlayAnimation(TalkingIdle,true);
}
void USenderEndingSubsystem::WarmUpTalkingBody()
{
    EnsureTalkingAssets();
    if(!TalkingBody) return;
    // 幂等：已经是单节点模式就不用再切。
    if(TalkingBody->GetAnimationMode()==EAnimationMode::AnimationSingleNode) return;
    // PlayAnimation 内部走 SetAnimation：组件当前在 AnimBP 模式下 GetSingleNodeInstance() 为空，
    // 于是强制切到单节点模式、销毁 AnimBP 实例重建一个。重建那一帧输出参考姿势，交互瞬间就是
    // "手抬起来卡一下"。这里在世界开始、玩家还没走到交互距离时就把模式换好，之后换片段不再有切换开销。
    TalkingBody->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    if(TalkingIdle) TalkingBody->PlayAnimation(TalkingIdle,true);
    UE_LOG(LogSenderEnding,Log,TEXT("Taro mesh pre-warmed to single-node playback (%s)"),TalkingIdle?*TalkingIdle->GetName():TEXT("no idle clip"));
}
void USenderEndingSubsystem::UpdateSpeakingAnimation(bool bSpeaking)
{
    EnsureTalkingAssets();
    if(!TalkingBody) return;
    if(bSpeaking)
    {
        // Taro 只有两处台词：第 4 页质问、第 11 页迟疑。片段首尾都与底姿对齐，播完停在末帧即可回到待机。
        UAnimSequence* Clip=(PageIndex==3)?TalkQuestion:TalkDoubt;
        if(!Clip) Clip=TalkDoubt?TalkDoubt:TalkQuestion;
        if(!Clip) return;
        TalkingBody->PlayAnimation(Clip,false);
        UE_LOG(LogSenderEnding,Log,TEXT("NPC speaking animation: %s (%.2fs, page %d)"),*Clip->GetName(),Clip->GetPlayLength(),PageIndex+1);
    }
    else
    {
        // 对话进行中玩家独白：播放倾听反应；对话外：呼吸待机。
        UAnimSequence* Clip=(State==ESenderEndingState::Dialogue)?ListenLoop:TalkingIdle;
        if(!Clip) Clip=TalkingIdle;
        if(Clip) TalkingBody->PlayAnimation(Clip,true);
    }
}
