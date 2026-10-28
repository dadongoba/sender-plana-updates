#include "SenderBackpackSubsystem.h"

#include "Animation/SkeletalMeshActor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSenderBackpack, Log, All);

USenderBackpackSubsystem::USenderBackpackSubsystem()
{
	BackpackMesh = FSoftObjectPath(TEXT("/Game/AdvancedLocomotionV4/Props/Meshes/Backpack/Backpack3/Backpack_Mesh3.Backpack_Mesh3"));
	// 带翻盖的帆布小包，缩到 0.42 大约 25x22x19cm，正好是腰包的体量。
	PouchMesh = FSoftObjectPath(TEXT("/Game/AdvancedLocomotionV4/Props/Meshes/Backpack/Backpack4/Backpack_MeshV4.Backpack_MeshV4"));
}

bool USenderBackpackSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return (bAttachToPlayer || bAttachPouch) && World && World->IsGameWorld();
}

void USenderBackpackSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!bAttachToPlayer && !bAttachPouch)
	{
		return;
	}

	bDone = !bAttachToPlayer;
	bPouchDone = !bAttachPouch;
	Attempts = 0;

	InWorld.GetTimerManager().SetTimer(
		RetryHandle,
		FTimerDelegate::CreateUObject(this, &USenderBackpackSubsystem::TickTryAttach),
		0.5f, true);
}

void USenderBackpackSubsystem::TickTryAttach()
{
	if (bDone && bPouchDone)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(RetryHandle);
		}
		return;
	}

	++Attempts;

	if (!bDone && TryAttach())
	{
		bDone = true;
	}
	if (!bPouchDone && TryAttachPouch())
	{
		bPouchDone = true;
	}

	if (bDone && bPouchDone)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(RetryHandle);
		}
		return;
	}

	// 最多等 6 秒；Pawn 还没出生就算了。
	if (Attempts >= 12)
	{
		UE_LOG(LogSenderBackpack, Warning, TEXT("Gave up waiting for player pawn after %d attempts."), Attempts);
		bDone = true;
		bPouchDone = true;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(RetryHandle);
		}
	}
}

namespace
{
	/** 找到本地玩家的 Pawn（还没出生就返回 nullptr）。 */
	APawn* FindLocalPlayerPawn(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (const APlayerController* PC = It->Get())
			{
				if (APawn* PlayerPawn = PC->GetPawn())
				{
					return PlayerPawn;
				}
			}
		}
		return nullptr;
	}
}

ASkeletalMeshActor* USenderBackpackSubsystem::SpawnAttachedBag(const FSoftObjectPath& MeshPath, FName Bone,
	float InBackOffset, float InSideOffset, float InUpOffset, float InRoll, float InPitch, float InYaw, float InScale)
{
	UWorld* World = GetWorld();
	APawn* Pawn = FindLocalPlayerPawn(World);
	if (!Pawn)
	{
		return nullptr;
	}

	USkeletalMeshComponent* SkelComp = Pawn->FindComponentByClass<USkeletalMeshComponent>();
	if (!SkelComp || !SkelComp->GetSkeletalMeshAsset())
	{
		return nullptr;
	}

	USkeletalMesh* Mesh = Cast<USkeletalMesh>(MeshPath.TryLoad());
	if (!Mesh)
	{
		UE_LOG(LogSenderBackpack, Warning, TEXT("Mesh not found: %s"), *MeshPath.ToString());
		return nullptr;
	}

	const FVector BoneLoc = SkelComp->GetSocketLocation(Bone);
	const FRotator PawnRot = Pawn->GetActorRotation();
	const FVector Back = -Pawn->GetActorForwardVector();
	const FVector Right = FRotationMatrix(PawnRot).GetUnitAxis(EAxis::Y);

	const FVector Target = BoneLoc + Back * InBackOffset + Right * InSideOffset + FVector(0.f, 0.f, InUpOffset);
	const FRotator BagRot = PawnRot + FRotator(InRoll, InPitch, InYaw);

	FActorSpawnParameters Params;
	Params.Owner = Pawn;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ASkeletalMeshActor* Bag = World->SpawnActor<ASkeletalMeshActor>(
		ASkeletalMeshActor::StaticClass(), Target, BagRot, Params);
	if (!Bag)
	{
		return nullptr;
	}

	USkeletalMeshComponent* BagMesh = Bag->GetSkeletalMeshComponent();
	if (!BagMesh)
	{
		Bag->Destroy();
		return nullptr;
	}

	BagMesh->SetSkeletalMeshAsset(Mesh);
	BagMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	BagMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bag->SetActorScale3D(FVector(InScale));

	// 网格自身的几何中心不在原点，迭代几次把「实际中心」对到目标点。
	for (int32 Iter = 0; Iter < 4; ++Iter)
	{
		const FBox Box = Bag->GetComponentsBoundingBox(true);
		const FVector Delta = Target - Box.GetCenter();
		if (Delta.Size() < 1.f)
		{
			break;
		}
		Bag->SetActorLocation(Bag->GetActorLocation() + Delta, false, nullptr, ETeleportType::TeleportPhysics);
	}

	Bag->AttachToComponent(SkelComp, FAttachmentTransformRules::KeepWorldTransform, Bone);
	return Bag;
}

bool USenderBackpackSubsystem::TryAttach()
{
	ASkeletalMeshActor* Bag = SpawnAttachedBag(BackpackMesh, AttachBone, BackOffset, SideOffset, DownOffset, 0.f, 0.f, ExtraYaw, Scale);
	if (!Bag)
	{
		return false;
	}
	SpawnedBag = Bag;
	UE_LOG(LogSenderBackpack, Log, TEXT("Backpack attached (bone %s, scale %.2f)"), *AttachBone.ToString(), Scale);
	return true;
}

bool USenderBackpackSubsystem::TryAttachPouch()
{
	ASkeletalMeshActor* Bag = SpawnAttachedBag(PouchMesh, PouchBone, PouchBackOffset, PouchSideOffset, PouchUpOffset,
		PouchRoll, PouchPitch, PouchYaw, PouchScale);
	if (!Bag)
	{
		return false;
	}
	SpawnedPouch = Bag;

	if (UWorld* World = GetWorld())
	{
		if (APawn* Pawn = FindLocalPlayerPawn(World))
		{
			UE_LOG(LogSenderBackpack, Log, TEXT("Waist pouch attached to %s (bone %s, scale %.2f, back %.1f side %.1f up %.1f)"),
				*Pawn->GetName(), *PouchBone.ToString(), PouchScale, PouchBackOffset, PouchSideOffset, PouchUpOffset);
		}
	}
	return true;
}

bool USenderBackpackSubsystem::AttachNow()
{
	if (TryAttach())
	{
		bDone = true;
		return true;
	}
	return false;
}

bool USenderBackpackSubsystem::AttachPouchNow()
{
	if (TryAttachPouch())
	{
		bPouchDone = true;
		return true;
	}
	return false;
}

void USenderBackpackSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RetryHandle);
	}
	Super::Deinitialize();
}
