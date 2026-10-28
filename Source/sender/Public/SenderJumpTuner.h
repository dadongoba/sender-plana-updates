// 起跳高度补正：AGLS/ALS 的 BP_TraversalActionsComponent 每次起跳前都会把
// CharacterMovement->JumpZVelocity 写成 DefJumpMaxZ（当前工程里是 300），
// 导致实际跳高只有 ~33cm，且改蓝图默认值会被运行时覆盖。
// 这里在角色真正起跳进入 Falling 的瞬间把垂直速度补到目标值，绕开蓝图逻辑。

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SenderJumpTuner.generated.h"

class ACharacter;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Sender Jump Tuner"))
class SENDER_API USenderJumpTuner : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/**
	 * 起跳瞬间保证的最低垂直速度（cm/s）。
	 * 跳高 ≈ v² / (2 * 980 * GravityScale)，工程里 GravityScale=1.4：
	 *   704.5 → 约 181cm，800 → 约 233cm，900 → 约 295cm
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Jump")
	float MinJumpZVelocity = 800.f;

	/** 只在起跳速度低于目标值时才补正，不压制本来就更高的特殊跳跃。 */
	UPROPERTY(Config, EditAnywhere, Category = "Jump")
	bool bOnlyFixWhenLower = true;

private:
	void PollBind();
	void BindCharacter(ACharacter* InChar);
	void UnbindCharacter();

	UFUNCTION()
	void HandleMovementModeChanged(ACharacter* Character, EMovementMode PrevMode, uint8 PrevCustomMode);

	TWeakObjectPtr<ACharacter> BoundCharacter;
	FTimerHandle BindTimerHandle;
};
