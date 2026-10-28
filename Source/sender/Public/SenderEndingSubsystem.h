#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystems/WorldSubsystem.h"
#include "SenderEndingSubsystem.generated.h"
class UInputComponent;
class ACharacter;
class APlayerController;
class UGameViewportClient;
class SSenderEndingOverlay;
class USkeletalMeshComponent;
class UAnimSequence;

UENUM(BlueprintType)
enum class ESenderEndingState : uint8 { Waiting, Dialogue, FadeToBlack, Title, Complete };

UCLASS(Config=Game, DefaultConfig)
class SENDER_API USenderEndingSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    USenderEndingSubsystem();
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& World) override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual void Deinitialize() override;

    /** Distance between the character capsule surfaces, in cm. */
    UPROPERTY(Config, EditAnywhere, Category="Sender|Ending") float InteractionDistance=50.f;
    UPROPERTY(Config, EditAnywhere, Category="Sender|Ending") float FadeSeconds=3.f;
    UPROPERTY(Config, EditAnywhere, Category="Sender|Ending") float TitleFadeSeconds=2.f;
    UPROPERTY(Config, EditAnywhere, Category="Sender|Ending", meta=(ClampMin="0.01")) float TitleLetterInterval=.16f;
    UPROPERTY(Config, EditAnywhere, Category="Sender|Ending", meta=(ClampMin="0.01")) float CaptionLetterInterval=.035f;
    UPROPERTY(Config, EditAnywhere, Category="Sender|Ending", meta=(ClampMin="0.0")) float CaptionStartDelay=.5f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sender|Ending") ESenderEndingState State=ESenderEndingState::Waiting;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sender|Ending") int32 PageIndex=INDEX_NONE;

    UFUNCTION(BlueprintCallable, Category="Sender|Ending") void Interact();
    UFUNCTION(BlueprintPure, Category="Sender|Ending") float GetSurfaceDistance() const;
private:
    void FindParticipants();
    void SetInputActive(bool bActive);
    void BeginDialogue();
    void DisplayPage();
    void StartEnding();
    void UpdateSpeakingAnimation(bool bSpeaking);
    void EnsureTalkingAssets();
    void PlayIdleAnimation();
    UPROPERTY() TObjectPtr<USkeletalMeshComponent> TalkingBody;
    UPROPERTY() TObjectPtr<UAnimSequence> TalkingIdle;
    UPROPERTY() TObjectPtr<UAnimSequence> TalkQuestion;
    UPROPERTY() TObjectPtr<UAnimSequence> TalkDoubt;
    UPROPERTY() TObjectPtr<UAnimSequence> ListenLoop;
    void RestorePresentation();

    UPROPERTY() TObjectPtr<ACharacter> NPC;
    UPROPERTY() TObjectPtr<APlayerController> PlayerController;
    UPROPERTY() TObjectPtr<UInputComponent> EndingInput;
    UPROPERTY() TObjectPtr<UObject> CursiveTypeface;
    TSharedPtr<SSenderEndingOverlay> EndingUI;
    TSharedPtr<SWidget> EndingLayer;
    TWeakObjectPtr<UGameViewportClient> EndingViewport;
    UPROPERTY() TArray<TObjectPtr<UUserWidget>> HiddenHUD;
    TArray<ESlateVisibility> SavedHUDVisibility;
    FTimerHandle FindTimer;
    float PhaseTime=0.f;
    float LastAdvanceTime=-10.f;
    bool bInputPushed=false;
    bool bOwnsInputLock=false;
    bool bNPCTickWasEnabled=false;
    uint8 SavedNPCMovementMode=0;
    uint8 SavedNPCCustomMode=0;
};
