#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpeedFXComponent.generated.h"

class APostProcessVolume;
class UCameraComponent;
class UCameraShakeBase;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USpringArmComponent;

/**
 * USpeedFXComponent
 *
 * Drives camera math, post-processing and camera shake from the owner's velocity to sell
 * an extreme sensation of speed. Add it to the vehicle Pawn; it is purely cosmetic and
 * therefore never ticks on a dedicated server.
 *
 *  1. Speed       : root velocity (cm/s) -> MPH -> NormalizedSpeed in [0..1].
 *  2. Camera      : FOV widening, spring-arm length + lag driven by speed and longitudinal G-force.
 *  3. PostProcess : vignette, chromatic aberration, motion blur and (material based) lens
 *                   distortion on an APostProcessVolume (unbound volume is auto-detected).
 *  4. Jitter      : procedural Perlin camera noise whose amplitude AND frequency ramp up
 *                   exponentially near top speed, plus an optional UCameraShakeBase that is
 *                   re-triggered at a speed-dependent rate.
 *
 * Every effect reads the same NormalizedSpeed, so all of them can be tuned from one place.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYGAME_API USpeedFXComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpeedFXComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Overrides the auto-detected camera / spring arm (either may be null). */
	UFUNCTION(BlueprintCallable, Category = "Speed FX")
	void SetCameraTargets(UCameraComponent* InCamera, USpringArmComponent* InSpringArm);

	/** Switches the driven post-process volume at runtime. Restores the previous volume's authored values. */
	UFUNCTION(BlueprintCallable, Category = "Speed FX")
	void SetPostProcessVolume(APostProcessVolume* InVolume);

	/** NormalizedSpeed after SpeedCurveExponent shaping - the value all camera / post-process effects use. */
	UFUNCTION(BlueprintPure, Category = "Speed FX")
	float GetSpeedEffectAlpha() const { return SpeedEffectAlpha; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Blueprint / subclass hook, called at the end of every FX update. Use it to drive audio, extra shakes, HUD, etc. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Speed FX")
	void OnSpeedFXUpdated(float InNormalizedSpeed, float InJitterAlpha, float InShakeAlpha);

	// Update steps, virtual so C++ subclasses can replace or extend individual effects.
	virtual void UpdateSpeed(float DeltaTime);
	virtual void UpdateCamera(float DeltaTime);
	virtual void UpdatePostProcess(float DeltaTime);
	virtual void UpdateCameraJitter(float DeltaTime);
	virtual void UpdateCameraShake(float DeltaTime);

	/** Owner velocity in cm/s. Uses the physics velocity when the root simulates physics (Chaos vehicles). */
	virtual FVector GetOwnerVelocity() const;

public:
	// ---------------------------------------------------------------------------------------------
	// General / speed mapping
	// ---------------------------------------------------------------------------------------------

	/** Master switch. When false, NormalizedSpeed is forced to 0 and every effect eases back to its baseline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX")
	bool bEffectsEnabled = true;

	/** Skip all FX when the owning Pawn is not locally controlled (AI cars, remote clients). Keeps the shared post-process volume yours. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX")
	bool bOnlyForLocallyControlled = true;

	/** Speed (MPH) at or below which NormalizedSpeed is 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0"))
	float MinSpeedThreshold = 50.f;

	/** Speed (MPH) at or above which NormalizedSpeed is 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0"))
	float MaxSpeedThreshold = 300.f;

	/** Shapes NormalizedSpeed for camera + post-process effects. 1 = linear, >1 = effects kick in late, <1 = early. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.1"))
	float SpeedCurveExponent = 1.f;

	// ---------------------------------------------------------------------------------------------
	// Runtime state (read-only)
	// ---------------------------------------------------------------------------------------------

	/** Current speed in miles per hour. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Speed FX")
	float CurrentSpeedMPH = 0.f;

	/** Speed mapped between Min/MaxSpeedThreshold and clamped to [0..1]. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Speed FX")
	float NormalizedSpeed = 0.f;

	/** Smoothed longitudinal acceleration in G (negative while braking). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Speed FX")
	float LongitudinalGForce = 0.f;

	/** LongitudinalGForce / AccelGForceForFullEffect, clamped to [-1..1]. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Speed FX")
	float AccelerationAlpha = 0.f;

	/** Current procedural jitter intensity in [0..1] (exponential in speed). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Speed FX")
	float JitterAlpha = 0.f;

	/** Current camera shake asset intensity in [0..1] (exponential in speed). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Speed FX")
	float ShakeAlpha = 0.f;

	// ---------------------------------------------------------------------------------------------
	// Camera targets (auto-detected on the owner in BeginPlay unless already set)
	// ---------------------------------------------------------------------------------------------

	/** Camera whose FOV and additive jitter offset are driven. Set via SetCameraTargets or auto-detected. */
	UPROPERTY(BlueprintReadWrite, Transient, Category = "Speed FX")
	TObjectPtr<UCameraComponent> CameraComponent;

	/** Spring arm whose length / lag are driven. Set via SetCameraTargets or auto-detected. */
	UPROPERTY(BlueprintReadWrite, Transient, Category = "Speed FX")
	TObjectPtr<USpringArmComponent> SpringArmComponent;

	// ---------------------------------------------------------------------------------------------
	// Camera: field of view
	// ---------------------------------------------------------------------------------------------

	/** FOV (degrees) at NormalizedSpeed 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "5.0", ClampMax = "170.0"))
	float BaseFOV = 80.f;

	/** FOV (degrees) at NormalizedSpeed 1 (high-speed distortion). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "5.0", ClampMax = "170.0"))
	float MaxFOV = 120.f;

	/** FInterpTo speed for the FOV. Higher = snappier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0"))
	float FOVInterpSpeed = 4.f;

	// ---------------------------------------------------------------------------------------------
	// Camera: spring arm (G-force simulation)
	// ---------------------------------------------------------------------------------------------

	/** Arm length (cm) at rest. Overwrites the value authored on the spring arm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0"))
	float BaseArmLength = 450.f;

	/** Extra arm length (cm) added at NormalizedSpeed 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX")
	float HighSpeedArmLengthOffset = 120.f;

	/** Extra arm length (cm) at full positive acceleration; applied negated (camera moves closer) when braking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX")
	float AccelArmLengthOffset = 80.f;

	/** Camera lag speed at NormalizedSpeed 0. Lower = more lag (0 disables lag; the component keeps it above a small minimum). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0"))
	float BaseLagSpeed = 12.f;

	/** Camera lag speed at NormalizedSpeed 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0"))
	float HighSpeedLagSpeed = 6.f;

	/** Camera lag speed at full positive acceleration (blended on top of the speed based lag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0"))
	float MaxAccelLagSpeed = 3.f;

	/** FInterpTo speed for arm length and lag speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0"))
	float ArmInterpSpeed = 3.f;

	/** Longitudinal acceleration (in G) that produces the full arm / lag effect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.01"))
	float AccelGForceForFullEffect = 1.f;

	/** FInterpTo speed used to smooth the (noisy) acceleration derived from velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0"))
	float GForceSmoothingSpeed = 3.f;

	// ---------------------------------------------------------------------------------------------
	// Post-processing
	// ---------------------------------------------------------------------------------------------

	/** Volume to drive. If null, an unbound APostProcessVolume is searched in the level on BeginPlay. It must be enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX")
	TObjectPtr<APostProcessVolume> PostProcessVolume;

	/** FInterpTo speed applied to all post-process values (smooths speed changes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0"))
	float PostProcessInterpSpeed = 5.f;

	/** Vignette intensity at NormalizedSpeed 0 / 1 (tunnel vision). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VignetteIntensityMin = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VignetteIntensityMax = 0.8f;

	/** Chromatic aberration (scene fringe) intensity at NormalizedSpeed 0 / 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float ChromaticAberrationMin = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float ChromaticAberrationMax = 3.5f;

	/** Motion blur amount at NormalizedSpeed 0 / 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MotionBlurAmountMin = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MotionBlurAmountMax = 0.9f;

	/**
	 * Lens distortion / fisheye value at NormalizedSpeed 0 / 1 (negative = barrel).
	 * The engine has no built-in lens distortion post-process property, so the value is written to
	 * LensDistortionParameterName of LensDistortionMaterial. Its meaning is defined by your material.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX")
	float LensDistortionMin = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX")
	float LensDistortionMax = -0.3f;

	/** Post-process (blendable) material implementing the distortion. Leave empty to skip lens distortion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX")
	TObjectPtr<UMaterialInterface> LensDistortionMaterial;

	/** Scalar parameter of LensDistortionMaterial that receives the distortion value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX")
	FName LensDistortionParameterName = TEXT("LensDistortion");

	// ---------------------------------------------------------------------------------------------
	// Procedural camera jitter
	// ---------------------------------------------------------------------------------------------

	/** Enables the procedural Perlin jitter (applied as additive camera offset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX")
	bool bEnableJitter = true;

	/** NormalizedSpeed below which jitter is 0; the [start..1] range is re-mapped to [0..1] before the exponent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0", ClampMax = "0.99"))
	float JitterStartNormalizedSpeed = 0.5f;

	/** Exponent of the jitter ramp (amplitude and frequency, and the shake asset ramp). Higher = violent only near max speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.1"))
	float JitterExponent = 3.f;

	/** Noise frequency (Hz) at the start of the jitter range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0"))
	float MinJitterFrequency = 8.f;

	/** Noise frequency (Hz) at max speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0"))
	float MaxJitterFrequency = 45.f;

	/** Camera-space location amplitude (cm) at max jitter: X = forward/back, Y = right/left, Z = up/down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX")
	FVector MaxJitterLocationAmplitude = FVector(0.3f, 1.f, 1.f);

	/** Camera-space rotation amplitude (degrees) at max jitter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX")
	FRotator MaxJitterRotationAmplitude = FRotator(0.25f, 0.25f, 0.75f);

	// ---------------------------------------------------------------------------------------------
	// Optional camera shake asset
	// ---------------------------------------------------------------------------------------------

	/**
	 * Optional shake played on the local PlayerCameraManager. It is re-triggered periodically while
	 * above ShakeStartNormalizedSpeed, so use a short one-shot shake (ideally with "single instance" enabled).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX")
	TSubclassOf<UCameraShakeBase> HighSpeedCameraShake;

	/** NormalizedSpeed at which the shake asset starts to be triggered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0", ClampMax = "0.99"))
	float ShakeStartNormalizedSpeed = 0.7f;

	/** Shake scale at max speed (scaled down exponentially below). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.0"))
	float ShakeMaxScale = 1.f;

	/** Seconds between shake triggers right at ShakeStartNormalizedSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.01"))
	float ShakeMaxInterval = 0.6f;

	/** Seconds between shake triggers at max speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed FX", meta = (ClampMin = "0.01"))
	float ShakeMinInterval = 0.1f;

private:
	/** Enables camera lag on the spring arm (lag speeds are meaningless without it). */
	void ApplySpringArmSetup();

	/** Finds the first unbound post-process volume in the world, or null. */
	APostProcessVolume* FindUnboundPostProcessVolume() const;

	/** Caches the authored volume values, force-enables the overrides we drive and creates the lens distortion MID. */
	void AcquirePostProcessVolume();

	/** Reverts everything AcquirePostProcessVolume changed. Safe to call repeatedly. */
	void ReleasePostProcessVolume();

	/** Authored volume values, restored in ReleasePostProcessVolume. */
	struct FCachedPostProcessState
	{
		bool bOverrideVignette = false;
		bool bOverrideFringe = false;
		bool bOverrideMotionBlur = false;
		float Vignette = 0.f;
		float Fringe = 0.f;
		float MotionBlur = 0.f;
	};

	FCachedPostProcessState CachedPostProcess;

	/** Dynamic instance of LensDistortionMaterial added to the volume's blendables. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LensDistortionMID;

	/** NormalizedSpeed shaped by SpeedCurveExponent. */
	float SpeedEffectAlpha = 0.f;

	float CurrentLensDistortion = 0.f;
	float PreviousSpeedCmPerSec = 0.f;
	float ShakeCooldown = 0.f;

	/** Accumulated noise time. Accumulating (instead of time * frequency) keeps the noise continuous while frequency changes. */
	double NoisePhase = 0.0;

	bool bHasPreviousSpeed = false;
	bool bJitterApplied = false;
	bool bPostProcessAcquired = false;
};
