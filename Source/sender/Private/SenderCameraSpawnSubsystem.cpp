#include "SenderCameraSpawnSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSenderCameraSpawn, Log, All);

bool USenderCameraSpawnSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	if (!World || !World->IsGameWorld())
	{
		return false;
	}
	// 只在编辑器内的 PIE 接管出生点，独立运行 / 打包版本继续用 PlayerStart。
	return World->WorldType == EWorldType::PIE;
}

void USenderCameraSpawnSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

#if WITH_EDITOR
	if (!bSpawnAtEditorCamera)
	{
		return;
	}
	Attempts = 0;
	// 玩家 Pawn 通常比世界 BeginPlay 稍晚一点就位，先用短间隔轮询。
	InWorld.GetTimerManager().SetTimer(
		RetryHandle,
		FTimerDelegate::CreateUObject(this, &USenderCameraSpawnSubsystem::TryPlacePlayer),
		0.05f, true, 0.05f);
#endif
}

void USenderCameraSpawnSubsystem::TryPlacePlayer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn)
	{
		if (++Attempts > 100)
		{
			World->GetTimerManager().ClearTimer(RetryHandle);
			UE_LOG(LogSenderCameraSpawn, Warning, TEXT("Player pawn never showed up, keeping the PlayerStart spawn."));
		}
		return;
	}

	World->GetTimerManager().ClearTimer(RetryHandle);

#if WITH_EDITOR
	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	UUnrealEditorSubsystem* EditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>() : nullptr;
	if (!EditorSubsystem || !EditorSubsystem->GetLevelViewportCameraInfo(CameraLocation, CameraRotation))
	{
		return;
	}

	// 落在相机正下方的地面上；打不到地面就按相机高度悬空出生，交给物理接管。
	FVector Target = CameraLocation;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SenderCameraSpawn), false, Pawn);
	if (World->LineTraceSingleByChannel(
		Hit, CameraLocation, CameraLocation - FVector(0.f, 0.f, 10000.f), ECC_Visibility, Params))
	{
		Target.X = CameraLocation.X;
		Target.Y = CameraLocation.Y;
		Target.Z = Hit.ImpactPoint.Z + Pawn->GetSimpleCollisionHalfHeight() + GroundClearance;
	}

	Pawn->SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);

	const FRotator Facing(0.f, CameraRotation.Yaw, 0.f);
	Pawn->SetActorRotation(Facing);
	if (AController* Controller = Pawn->GetController())
	{
		Controller->SetControlRotation(Facing);
	}

	UE_LOG(LogSenderCameraSpawn, Log, TEXT("Spawned %s at %s, camera %s"),
		*Pawn->GetName(), *Target.ToString(), *CameraLocation.ToString());
#endif
}

void USenderCameraSpawnSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RetryHandle);
	}
	Super::Deinitialize();
}
