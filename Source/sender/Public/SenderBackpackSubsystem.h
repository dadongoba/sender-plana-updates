// 主角挂件（Player Props）：
//   玩家的 Pawn 是 GameMode 运行时生成的（ALS_AnimMan_CharacterBP），关卡里没有实例，
//   所以挂件不能在编辑器里摆，只能等 Pawn 出生后挂上去。
//
// 支持两件：
//   1. 背包（bAttachToPlayer）—— 脊柱后方，默认关闭
//   2. 腰包（bAttachPouch）—— 别在腰后靠屁股的位置，邮差包风格，默认开启
//
// 配置（Config/DefaultGame.ini 可覆盖）：
//   [/Script/sender.SenderBackpackSubsystem]
//   bAttachToPlayer=False
//   bAttachPouch=True
//   PouchMesh=/Game/.../Backpack_MeshV4.Backpack_MeshV4
//   PouchScale=0.42

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SenderBackpackSubsystem.generated.h"

class ASkeletalMeshActor;
class USkeletalMeshComponent;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Sender Player Backpack"))
class SENDER_API USenderBackpackSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	USenderBackpackSubsystem();

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** 总开关。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Backpack")
	bool bAttachToPlayer = true;

	/** 背包网格（SkeletalMesh）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Backpack")
	FSoftObjectPath BackpackMesh;

	/** 挂载骨骼。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Backpack")
	FName AttachBone = TEXT("spine_03");

	/** 整体缩放。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Backpack")
	float Scale = 0.58f;

	/** 背包中心在脊柱后方多远（cm）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Backpack")
	float BackOffset = 13.f;

	/** 左右微调（cm，正数朝角色右侧）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Backpack")
	float SideOffset = -6.f;

	/** 相对脊柱的上下微调（cm，负数向下）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Backpack")
	float DownOffset = -4.f;

	/** 朝向额外旋转（度）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Backpack")
	float ExtraYaw = 180.f;

	/** 手动挂一次（蓝图可调）。 */
	UFUNCTION(BlueprintCallable, Category = "Sender|Backpack")
	bool AttachNow();

	// ------------------------------------------------------------------
	// 腰包（postman pouch，别在腰后）
	// ------------------------------------------------------------------

	/** 腰包总开关（和背包互不影响）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Pouch")
	bool bAttachPouch = true;

	/** 腰包网格（SkeletalMesh）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Pouch")
	FSoftObjectPath PouchMesh;

	/** 挂载骨骼（骨盆）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Pouch")
	FName PouchBone = TEXT("pelvis");

	/** 整体缩放。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Pouch")
	float PouchScale = 0.42f;

	/** 在骨盆后方多远（cm，正数朝屁股后面）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Pouch")
	float PouchBackOffset = 13.f;

	/** 左右偏移（cm，正数朝角色右侧）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Pouch")
	float PouchSideOffset = 9.f;

	/** 上下偏移（cm，负数向下）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Pouch")
	float PouchUpOffset = -7.f;

	/** 朝向：偏航（180 = 正面朝后）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Pouch")
	float PouchYaw = 180.f;

	/** 朝向：俯仰（正数朝下压）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Pouch")
	float PouchPitch = 0.f;

	/** 朝向：翻滚（让包贴住臀部）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Pouch")
	float PouchRoll = 0.f;

	/** 手动挂一次腰包（蓝图可调）。 */
	UFUNCTION(BlueprintCallable, Category = "Sender|Pouch")
	bool AttachPouchNow();

private:
	void TickTryAttach();
	bool TryAttach();
	bool TryAttachPouch();

	/** 生成一个挂在骨骼上的挂件，返回 Actor。 */
	ASkeletalMeshActor* SpawnAttachedBag(const FSoftObjectPath& MeshPath, FName Bone, float BackOffset,
		float SideOffset, float UpOffset, float Roll, float Pitch, float Yaw, float InScale);

	FTimerHandle RetryHandle;
	int32 Attempts = 0;
	bool bDone = false;
	bool bPouchDone = false;

	UPROPERTY()
	TObjectPtr<ASkeletalMeshActor> SpawnedBag = nullptr;

	UPROPERTY()
	TObjectPtr<ASkeletalMeshActor> SpawnedPouch = nullptr;
};
