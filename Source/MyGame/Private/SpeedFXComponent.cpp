#include "SpeedFXComponent.h"

#include "Camera/CameraComponent.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpeedFX, Log, All);

namespace
{
	/** cm/s -> MPH (1 MPH = 44.704 cm/s). */
	constexpr float CmPerSecToMph = 0.0223693629f;

	/** Standard gravity in cm/s^2, used to express acceleration in G. */
	constexpr float GravityCmPerSec2 = 980.665f;

	/** Spring arm lag speed of 0 disables lag entirely, so never interpolate below this. */
	constexpr float MinCameraLagSpeed = 0.1f;

	/**
	 * Remaps Normalized from [Start..1] to [0..1] and raises it to Exponent.
	 * With Exponent > 1 the result stays small for most of the range and ramps up steeply near 1.
	 */
	float ComputeExponentialAlpha(float Normalized, float Start, float Exponent)
	{
		const float Range = FMath::Max(1.f - Start, KINDA_SMALL_NUMBER);
		const float Linear = FMath::Clamp((Normalized - Start) / Range, 0.f, 1.f);
		return FMath::Pow(Linear, FMath::Max(Exponent, 0.01f));
	}
}

USpeedFXComponent::USpeedFXComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Run after physics so velocity and the vehicle transform are up to date for this frame's camera update.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void USpeedFXComponent::BeginPlay()
{
	Super::BeginPlay();

	// Everything here is cosmetic, a dedicated server has no camera or post-processing.
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false);
		return;
	}

	// Auto-detect camera / spring arm on the owner unless they were assigned beforehand.
	if (const AActor* Owner = GetOwner())
	{
		if (!CameraComponent)
		{
			CameraComponent = Owner->FindComponentByClass<UCameraComponent>();
		}
		if (!SpringArmComponent)
		{
			SpringArmComponent = Owner->FindComponentByClass<USpringArmComponent>();
		}
	}

	if (!CameraComponent)
	{
		UE_LOG(LogSpeedFX, Warning, TEXT("%s: no CameraComponent found - FOV and jitter are disabled."), *GetNameSafe(GetOwner()));
	}
	ApplySpringArmSetup();

	// Post-process volume: use the assigned one, otherwise fall back to an unbound volume in the level.
	if (!PostProcessVolume)
	{
		PostProcessVolume = FindUnboundPostProcessVolume();
	}
	if (PostProcessVolume)
	{
		AcquirePostProcessVolume();
	}
	else
	{
		UE_LOG(LogSpeedFX, Warning, TEXT("%s: no PostProcessVolume assigned and no unbound volume found - post-process effects are disabled."), *GetNameSafe(GetOwner()));
	}
}

void USpeedFXComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleasePostProcessVolume();

	if (IsValid(CameraComponent) && bJitterApplied)
	{
		CameraComponent->ClearAdditiveOffset();
		bJitterApplied = false;
	}

	Super::EndPlay(EndPlayReason);
}

void USpeedFXComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Acceleration is derived by dividing by DeltaTime, so skip degenerate frames (e.g. while paused).
	if (DeltaTime <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Never drive the shared volume / local camera from AI or remote vehicles.
	if (bOnlyForLocallyControlled)
	{
		const APawn* OwnerPawn = Cast<APawn>(GetOwner());
		if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
		{
			return;
		}
	}

	UpdateSpeed(DeltaTime);
	UpdateCamera(DeltaTime);
	UpdatePostProcess(DeltaTime);
	UpdateCameraJitter(DeltaTime);
	UpdateCameraShake(DeltaTime);

	OnSpeedFXUpdated(NormalizedSpeed, JitterAlpha, ShakeAlpha);
}

// -------------------------------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------------------------------

void USpeedFXComponent::SetCameraTargets(UCameraComponent* InCamera, USpringArmComponent* InSpringArm)
{
	// Never leave a stale additive jitter offset behind on the camera we stop driving.
	if (IsValid(CameraComponent) && CameraComponent != InCamera && bJitterApplied)
	{
		CameraComponent->ClearAdditiveOffset();
		bJitterApplied = false;
	}

	CameraComponent = InCamera;
	SpringArmComponent = InSpringArm;
	ApplySpringArmSetup();
}

void USpeedFXComponent::SetPostProcessVolume(APostProcessVolume* InVolume)
{
	if (InVolume == PostProcessVolume)
	{
		return;
	}

	ReleasePostProcessVolume();
	PostProcessVolume = InVolume;

	// Before BeginPlay the volume is picked up by BeginPlay itself.
	if (HasBegunPlay())
	{
		AcquirePostProcessVolume();
	}
}

// -------------------------------------------------------------------------------------------------
// Update steps
// -------------------------------------------------------------------------------------------------

FVector USpeedFXComponent::GetOwnerVelocity() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}

	// Physics-driven vehicles: read the rigid body velocity directly, it is the authoritative value.
	if (const UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(Owner->GetRootComponent()))
	{
		if (RootPrimitive->IsSimulatingPhysics())
		{
			return RootPrimitive->GetPhysicsLinearVelocity();
		}
	}

	// Kinematic / movement-component driven vehicles.
	return Owner->GetVelocity();
}

void USpeedFXComponent::UpdateSpeed(float DeltaTime)
{
	// --- Speed (cm/s -> MPH) and normalisation ---
	const float SpeedCmPerSec = GetOwnerVelocity().Size();
	CurrentSpeedMPH = SpeedCmPerSec * CmPerSecToMph;

	const float SpeedRange = FMath::Max(MaxSpeedThreshold - MinSpeedThreshold, KINDA_SMALL_NUMBER);
	NormalizedSpeed = bEffectsEnabled
		? FMath::Clamp((CurrentSpeedMPH - MinSpeedThreshold) / SpeedRange, 0.f, 1.f)
		: 0.f;

	SpeedEffectAlpha = FMath::Pow(NormalizedSpeed, FMath::Max(SpeedCurveExponent, 0.01f));

	// --- Longitudinal acceleration in G (rate of change of speed, so it needs no vehicle orientation) ---
	if (bHasPreviousSpeed)
	{
		const float RawGForce = ((SpeedCmPerSec - PreviousSpeedCmPerSec) / DeltaTime) / GravityCmPerSec2;
		LongitudinalGForce = FMath::FInterpTo(LongitudinalGForce, RawGForce, DeltaTime, GForceSmoothingSpeed);
	}
	PreviousSpeedCmPerSec = SpeedCmPerSec;
	bHasPreviousSpeed = true;

	AccelerationAlpha = bEffectsEnabled
		? FMath::Clamp(LongitudinalGForce / FMath::Max(AccelGForceForFullEffect, KINDA_SMALL_NUMBER), -1.f, 1.f)
		: 0.f;
}

void USpeedFXComponent::UpdateCamera(float DeltaTime)
{
	// --- Field of view ---
	if (CameraComponent)
	{
		const float TargetFOV = FMath::Lerp(BaseFOV, MaxFOV, SpeedEffectAlpha);
		CameraComponent->SetFieldOfView(FMath::FInterpTo(CameraComponent->FieldOfView, TargetFOV, DeltaTime, FOVInterpSpeed));
	}

	// --- Spring arm: pulls back and lags more with speed and forward acceleration, moves in when braking ---
	if (SpringArmComponent)
	{
		const float TargetArmLength = FMath::Max(0.f,
			BaseArmLength
			+ FMath::Lerp(0.f, HighSpeedArmLengthOffset, SpeedEffectAlpha)
			+ AccelArmLengthOffset * AccelerationAlpha);

		// Speed based lag first, then blend towards the acceleration lag (positive acceleration only).
		const float SpeedLagSpeed = FMath::Lerp(BaseLagSpeed, HighSpeedLagSpeed, SpeedEffectAlpha);
		const float TargetLagSpeed = FMath::Lerp(SpeedLagSpeed, MaxAccelLagSpeed, FMath::Max(AccelerationAlpha, 0.f));

		SpringArmComponent->TargetArmLength = FMath::FInterpTo(SpringArmComponent->TargetArmLength, TargetArmLength, DeltaTime, ArmInterpSpeed);
		SpringArmComponent->CameraLagSpeed = FMath::Max(
			FMath::FInterpTo(SpringArmComponent->CameraLagSpeed, TargetLagSpeed, DeltaTime, ArmInterpSpeed),
			MinCameraLagSpeed);
	}
}

void USpeedFXComponent::UpdatePostProcess(float DeltaTime)
{
	if (!bPostProcessAcquired || !IsValid(PostProcessVolume))
	{
		return;
	}

	FPostProcessSettings& Settings = PostProcessVolume->Settings;

	// Every value is scaled by the shaped speed, then eased towards its target to avoid visible pops.
	Settings.VignetteIntensity = FMath::FInterpTo(
		Settings.VignetteIntensity, FMath::Lerp(VignetteIntensityMin, VignetteIntensityMax, SpeedEffectAlpha), DeltaTime, PostProcessInterpSpeed);

	Settings.SceneFringeIntensity = FMath::FInterpTo(
		Settings.SceneFringeIntensity, FMath::Lerp(ChromaticAberrationMin, ChromaticAberrationMax, SpeedEffectAlpha), DeltaTime, PostProcessInterpSpeed);

	Settings.MotionBlurAmount = FMath::FInterpTo(
		Settings.MotionBlurAmount, FMath::Lerp(MotionBlurAmountMin, MotionBlurAmountMax, SpeedEffectAlpha), DeltaTime, PostProcessInterpSpeed);

	// Lens distortion has no native post-process property, it is a scalar parameter on a blendable material.
	if (LensDistortionMID)
	{
		CurrentLensDistortion = FMath::FInterpTo(
			CurrentLensDistortion, FMath::Lerp(LensDistortionMin, LensDistortionMax, SpeedEffectAlpha), DeltaTime, PostProcessInterpSpeed);
		LensDistortionMID->SetScalarParameterValue(LensDistortionParameterName, CurrentLensDistortion);
	}
}

void USpeedFXComponent::UpdateCameraJitter(float DeltaTime)
{
	if (!CameraComponent)
	{
		return;
	}

	// Amplitude and frequency both follow the same steep exponential ramp.
	JitterAlpha = bEnableJitter ? ComputeExponentialAlpha(NormalizedSpeed, JitterStartNormalizedSpeed, JitterExponent) : 0.f;

	if (JitterAlpha <= 0.f)
	{
		// Remove our additive offset exactly once when the jitter dies down.
		if (bJitterApplied)
		{
			CameraComponent->ClearAdditiveOffset();
			bJitterApplied = false;
		}
		return;
	}

	// Integrate the frequency so changing speed never makes the noise jump.
	NoisePhase += static_cast<double>(DeltaTime) * FMath::Lerp(MinJitterFrequency, MaxJitterFrequency, JitterAlpha);

	// One decorrelated Perlin channel per axis: the seed offsets are non-integer so the channels don't share zero crossings.
	auto SampleNoise = [this](float Seed)
	{
		return FMath::Clamp(FMath::PerlinNoise1D(static_cast<float>(NoisePhase) + Seed), -1.f, 1.f);
	};

	const FVector LocationOffset(
		SampleNoise(0.f)     * MaxJitterLocationAmplitude.X,
		SampleNoise(17.31f)  * MaxJitterLocationAmplitude.Y,
		SampleNoise(34.62f)  * MaxJitterLocationAmplitude.Z);

	const FRotator RotationOffset(
		SampleNoise(51.93f)  * MaxJitterRotationAmplitude.Pitch,
		SampleNoise(69.24f)  * MaxJitterRotationAmplitude.Yaw,
		SampleNoise(86.55f)  * MaxJitterRotationAmplitude.Roll);

	// Additive offset is applied in camera space on top of the camera's view without touching its relative transform.
	CameraComponent->AddAdditiveOffset(FTransform(RotationOffset, LocationOffset * JitterAlpha), 0.f);
	bJitterApplied = true;
}

void USpeedFXComponent::UpdateCameraShake(float DeltaTime)
{
	ShakeAlpha = ComputeExponentialAlpha(NormalizedSpeed, ShakeStartNormalizedSpeed, JitterExponent);

	if (!HighSpeedCameraShake)
	{
		return;
	}

	// Rearm so the first shake fires immediately when the speed threshold is crossed again.
	if (ShakeAlpha <= 0.f)
	{
		ShakeCooldown = 0.f;
		return;
	}

	ShakeCooldown -= DeltaTime;
	if (ShakeCooldown > 0.f)
	{
		return;
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const APlayerController* PlayerController = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
	if (PlayerController && PlayerController->PlayerCameraManager)
	{
		PlayerController->PlayerCameraManager->StartCameraShake(HighSpeedCameraShake, ShakeAlpha * ShakeMaxScale);
	}

	// Faster re-triggering the closer we get to top speed, i.e. the shake frequency scales with the same exponential.
	ShakeCooldown = FMath::Lerp(ShakeMaxInterval, ShakeMinInterval, ShakeAlpha);
}

// -------------------------------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------------------------------

void USpeedFXComponent::ApplySpringArmSetup()
{
	if (SpringArmComponent)
	{
		SpringArmComponent->bEnableCameraLag = true;
	}
}

APostProcessVolume* USpeedFXComponent::FindUnboundPostProcessVolume() const
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<APostProcessVolume> It(World); It; ++It)
		{
			if (It->bUnbound)
			{
				return *It;
			}
		}
	}
	return nullptr;
}

void USpeedFXComponent::AcquirePostProcessVolume()
{
	if (bPostProcessAcquired || !IsValid(PostProcessVolume))
	{
		return;
	}

	FPostProcessSettings& Settings = PostProcessVolume->Settings;

	// Remember what the level designer authored so it can be restored in EndPlay.
	CachedPostProcess.bOverrideVignette = Settings.bOverride_VignetteIntensity;
	CachedPostProcess.bOverrideFringe = Settings.bOverride_SceneFringeIntensity;
	CachedPostProcess.bOverrideMotionBlur = Settings.bOverride_MotionBlurAmount;
	CachedPostProcess.Vignette = Settings.VignetteIntensity;
	CachedPostProcess.Fringe = Settings.SceneFringeIntensity;
	CachedPostProcess.MotionBlur = Settings.MotionBlurAmount;

	// Values are ignored by the renderer unless their override flag is set.
	Settings.bOverride_VignetteIntensity = true;
	Settings.bOverride_SceneFringeIntensity = true;
	Settings.bOverride_MotionBlurAmount = true;

	if (LensDistortionMaterial)
	{
		LensDistortionMID = UMaterialInstanceDynamic::Create(LensDistortionMaterial, this);
		if (LensDistortionMID)
		{
			CurrentLensDistortion = LensDistortionMin;
			LensDistortionMID->SetScalarParameterValue(LensDistortionParameterName, CurrentLensDistortion);
			Settings.AddBlendable(LensDistortionMID.Get(), 1.f);
		}
	}

	bPostProcessAcquired = true;
}

void USpeedFXComponent::ReleasePostProcessVolume()
{
	if (!bPostProcessAcquired)
	{
		return;
	}

	if (IsValid(PostProcessVolume))
	{
		FPostProcessSettings& Settings = PostProcessVolume->Settings;

		Settings.VignetteIntensity = CachedPostProcess.Vignette;
		Settings.SceneFringeIntensity = CachedPostProcess.Fringe;
		Settings.MotionBlurAmount = CachedPostProcess.MotionBlur;
		Settings.bOverride_VignetteIntensity = CachedPostProcess.bOverrideVignette;
		Settings.bOverride_SceneFringeIntensity = CachedPostProcess.bOverrideFringe;
		Settings.bOverride_MotionBlurAmount = CachedPostProcess.bOverrideMotionBlur;

		if (LensDistortionMID)
		{
			Settings.RemoveBlendable(LensDistortionMID.Get());
		}
	}

	LensDistortionMID = nullptr;
	bPostProcessAcquired = false;
}
