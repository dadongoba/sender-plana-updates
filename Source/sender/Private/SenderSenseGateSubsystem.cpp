#include "SenderSenseGateSubsystem.h"

#include "Components/MeshComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSenderSenseGate, Log, All);

namespace
{
	/** Actor 上任意一个材质名的关键字命中即算匹配。 */
	bool ActorUsesMaterialKeyword(const AActor* Actor, const FString& Keyword)
	{
		if (Keyword.IsEmpty())
		{
			return false;
		}

		TArray<UMeshComponent*> MeshComponents;
		Actor->GetComponents<UMeshComponent>(MeshComponents);
		for (const UMeshComponent* MeshComponent : MeshComponents)
		{
			const int32 NumMaterials = MeshComponent->GetNumMaterials();
			for (int32 Index = 0; Index < NumMaterials; ++Index)
			{
				const UMaterialInterface* Material = MeshComponent->GetMaterial(Index);
				if (Material && Material->GetName().Contains(Keyword, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
		}
		return false;
	}

	/** LevelInstance 内部（/Temp/..._LevelInstance_...）的 Actor 不参与，避免同名材质误触发。 */
	bool IsInsideLevelInstance(const AActor* Actor)
	{
		const ULevel* Level = Actor ? Actor->GetLevel() : nullptr;
		if (!Level)
		{
			return true;
		}
		if (!Level->IsPersistentLevel())
		{
			return true;
		}
		const UObject* LevelPackage = Level->GetOutermost();
		return LevelPackage && LevelPackage->GetName().Contains(TEXT("LevelInstance"), ESearchCase::IgnoreCase);
	}
}

bool USenderSenseGateSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void USenderSenseGateSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bTriggered = false;

	// 等一小会儿，让关卡里所有 Actor（含运行时生成的）都就位后再扫描。
	FTimerHandle DeferredHandle;
	InWorld.GetTimerManager().SetTimer(
		DeferredHandle,
		FTimerDelegate::CreateUObject(this, &USenderSenseGateSubsystem::CollectActors),
		0.2f, false);
}

void USenderSenseGateSubsystem::CollectActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	SenseActors.Reset();
	TargetActors.Reset();
	TargetOriginalCollision.Reset();

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || IsInsideLevelInstance(Actor))
		{
			continue;
		}

		const bool bIsSense =
			(!SenseActorTag.IsNone() && Actor->Tags.Contains(SenseActorTag)) ||
			ActorUsesMaterialKeyword(Actor, SenseMaterialKeyword);

		if (bIsSense)
		{
			SenseActors.Add(Actor);
			continue;
		}

		const bool bIsTarget =
			(!TargetActorTag.IsNone() && Actor->Tags.Contains(TargetActorTag)) ||
			ActorUsesMaterialKeyword(Actor, TargetMaterialKeyword);

		if (bIsTarget)
		{
			TargetActors.Add(Actor);
			TargetOriginalCollision.Add(Actor, Actor->GetActorEnableCollision());
		}
	}

	UE_LOG(LogSenderSenseGate, Log, TEXT("Scanned level: %d sense actor(s), %d parclose actor(s)."),
		SenseActors.Num(), TargetActors.Num());

	if (SenseActors.Num() == 0 || TargetActors.Num() == 0)
	{
		return;
	}

	// 保持初始状态：玩家还没踩上来，隔断可见。
	SetTargetsHidden(false);
	bTriggered = false;

	World->GetTimerManager().SetTimer(
		CheckHandle,
		FTimerDelegate::CreateUObject(this, &USenderSenseGateSubsystem::Evaluate),
		FMath::Max(CheckInterval, 0.01f), true);
}

void USenderSenseGateSubsystem::Evaluate()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bool bTouching = false;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PlayerController = It->Get();
		const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}

		// 用玩家胶囊的包围盒，方块侧用网格包围盒，相交就算接触。
		float Radius = 0.f;
		float HalfHeight = 0.f;
		Pawn->GetSimpleCollisionCylinder(Radius, HalfHeight);
		const FVector Center = Pawn->GetActorLocation();
		const FBox PawnBox(Center - FVector(Radius, Radius, HalfHeight), Center + FVector(Radius, Radius, HalfHeight));

		for (const AActor* SenseActor : SenseActors)
		{
			if (!IsValid(SenseActor))
			{
				continue;
			}

			FBox SenseBox = SenseActor->GetComponentsBoundingBox(true);
			if (!SenseBox.IsValid)
			{
				continue;
			}

			if (SenseBox.ExpandBy(SensePadding).Intersect(PawnBox))
			{
				bTouching = true;
				break;
			}
		}

		if (bTouching)
		{
			break;
		}
	}

	// 一次性模式下触发后就锁死在"已消失"。
	if (!bRestoreWhenReleased && bTriggered)
	{
		return;
	}

	if (bTouching == bTriggered)
	{
		return;
	}

	bTriggered = bTouching;
	SetTargetsHidden(bTouching);
}

void USenderSenseGateSubsystem::SetTargetsHidden(bool bHide)
{
	for (AActor* Target : TargetActors)
	{
		if (!IsValid(Target))
		{
			continue;
		}

		Target->SetActorHiddenInGame(bHide);

		const bool* bOriginalCollision = TargetOriginalCollision.Find(Target);
		const bool bCanCollide = bOriginalCollision ? *bOriginalCollision : true;
		Target->SetActorEnableCollision(bHide ? false : bCanCollide);
	}

	UE_LOG(LogSenderSenseGate, Log, TEXT("%s %d parclose actor(s)."),
		bHide ? TEXT("Hiding") : TEXT("Restoring"), TargetActors.Num());
}

void USenderSenseGateSubsystem::ForceEvaluate()
{
	Evaluate();
}

void USenderSenseGateSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CheckHandle);
	}
	Super::Deinitialize();
}
