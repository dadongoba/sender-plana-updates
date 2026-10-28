// PIE 出生点跟随编辑器视口相机：
//   按 Play 时，玩家直接出现在你当前正在看的位置（相机正下方的地面上），
//   朝向也对齐相机朝向，省去每次进游戏再手动跑过去 / 传送。
//
// 行为约束：
//   - 只在 PIE（编辑器内运行）生效，独立运行 / 打包版本完全不受影响。
//   - 只对 0 号玩家生效。
//   - 相机正下方打不到地面时，直接落在相机原位置（悬空则由物理接管）。

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SenderCameraSpawnSubsystem.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Sender Camera Spawn"))
class SENDER_API USenderCameraSpawnSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** 关掉后 PIE 就恢复用场景里的 PlayerStart 出生。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Spawn")
	bool bSpawnAtEditorCamera = true;

	/** 出生点相对地面的抬高量，避免胶囊和地板穿插。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Spawn")
	float GroundClearance = 4.f;

private:
	/** 玩家 Pawn 还没生成时反复重试，直到找到为止。 */
	void TryPlacePlayer();
	void Finish();

	FTimerHandle RetryHandle;
	int32 Attempts = 0;
};
