#include "SenderJumpTuner.h"

#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

bool USenderJumpTuner::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

void USenderJumpTuner::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 玩家 Pawn 可能稍后才生成或被 Possess，低频轮询保证绑定
	InWorld.GetTimerManager().SetTimer(
		BindTimerHandle, this, &USenderJumpTuner::PollBind, 0.5f, true, 0.2f);
}

void USenderJumpTuner::Deinitialize()
{
	UnbindCharacter();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BindTimerHandle);
	}

	Super::Deinitialize();
}

void USenderJumpTuner::PollBind()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (ACharacter* Char = UGameplayStatics::GetPlayerCharacter(World, 0))
	{
		if (Char != BoundCharacter.Get())
		{
			BindCharacter(Char);
		}
	}
}

void USenderJumpTuner::BindCharacter(ACharacter* InChar)
{
	UnbindCharacter();

	if (!IsValid(InChar))
	{
		return;
	}

	InChar->MovementModeChangedDelegate.AddDynamic(this, &USenderJumpTuner::HandleMovementModeChanged);
	BoundCharacter = InChar;
}

void USenderJumpTuner::UnbindCharacter()
{
	if (ACharacter* Char = BoundCharacter.Get())
	{
		Char->MovementModeChangedDelegate.RemoveAll(this);
	}
	BoundCharacter.Reset();
}

void USenderJumpTuner::HandleMovementModeChanged(
	ACharacter* Character, EMovementMode PrevMode, uint8 PrevCustomMode)
{
	if (!IsValid(Character) || PrevMode == MOVE_Falling)
	{
		return;
	}

	UCharacterMovementComponent* Move = Character->GetCharacterMovement();
	if (!Move || Move->MovementMode != MOVE_Falling)
	{
		return;
	}

	// 只处理真正向上的起跳；走下台阶/掉下悬崖时 Z 速度不为正
	const float CurrentZ = Move->Velocity.Z;
	if (CurrentZ <= 0.f)
	{
		return;
	}

	// 只补正"用 CharacterMovement 默认跳跃速度"的普通起跳。
	// 可预测跳跃/翻越等会写入自定义速度，不应被压平或抬高。
	if (CurrentZ > Move->JumpZVelocity * 1.05f)
	{
		return;
	}

	if (bOnlyFixWhenLower && CurrentZ >= MinJumpZVelocity)
	{
		return;
	}

	Move->Velocity.Z = MinJumpZVelocity;
}
