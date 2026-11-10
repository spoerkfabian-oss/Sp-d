using UnityEngine;

/// <summary>
/// Chase camera that sells speed without post-processing: FOV widening, the camera dropping
/// and closing in, look-ahead, cubic shake near top speed, G-force pull-back and speed streaks.
/// Port of the ideas in the old Unreal USpeedFXComponent, all driven by one normalized speed.
/// </summary>
[RequireComponent(typeof(Camera))]
public class ChaseCamera : MonoBehaviour
{
    public CarController car;

    [Header("Field of view")]
    public float fovSlow = 58f;
    public float fovFast = 112f;

    [Header("Rig")]
    public float distanceSlow = 6.5f, distanceFast = 4.4f;
    public float heightSlow = 2.3f, heightFast = 1.25f;
    public float turnLag = 6f;
    public float accelPullback = 0.035f;

    [Header("Shake")]
    public float shakeAmplitude = 0.11f;
    public float shakeRoll = 1.6f;
    public float shakeFrequencySlow = 7f, shakeFrequencyFast = 30f;

    [Header("Streaks")]
    public float streakRate = 260f;

    Camera cam;
    ParticleSystem streaks;
    Vector3 heading;
    float lastSpeed, pullback, shakeTime;

    void Start()
    {
        cam = GetComponent<Camera>();
        cam.farClipPlane = 4000f;
        cam.nearClipPlane = 0.2f;
        heading = car != null ? car.transform.forward : Vector3.forward;
        CreateStreaks();
    }

    void CreateStreaks()
    {
        var go = new GameObject("SpeedStreaks");
        go.transform.SetParent(transform, false);
        go.transform.localPosition = new Vector3(0f, 1f, 70f);
        streaks = go.AddComponent<ParticleSystem>();

        var main = streaks.main;
        main.simulationSpace = ParticleSystemSimulationSpace.World;
        main.startSpeed = 0f;
        main.startLifetime = 1.2f;
        main.startSize = 0.06f;
        main.startColor = new Color(1f, 1f, 1f, 0.35f);
        main.maxParticles = 600;

        var shape = streaks.shape;
        shape.shapeType = ParticleSystemShapeType.Box;
        shape.scale = new Vector3(26f, 9f, 50f);

        var emission = streaks.emission;
        emission.rateOverTime = 0f;

        var renderer = go.GetComponent<ParticleSystemRenderer>();
        renderer.renderMode = ParticleSystemRenderMode.Stretch;
        renderer.cameraVelocityScale = 0.03f;
        renderer.lengthScale = 1f;
        renderer.material = new Material(Shader.Find("Sprites/Default"));
    }

    void LateUpdate()
    {
        if (car == null) return;
        float dt = Time.deltaTime;
        if (dt <= 0f) return;
        float s = car.NormalizedSpeed;

        // Heading follows the velocity at speed, so slides show the car sideways on screen.
        Vector3 velocity = car.Body.linearVelocity;
        Vector3 target = car.transform.forward;
        if (velocity.sqrMagnitude > 25f && Vector3.Dot(velocity, target) > 0f)
            target = Vector3.Slerp(target, velocity.normalized, 0.6f);
        target.y *= 0.5f;
        heading = Vector3.Slerp(heading, target.normalized, 1f - Mathf.Exp(-turnLag * dt)).normalized;

        float accel = (car.SpeedMs - lastSpeed) / dt;
        lastSpeed = car.SpeedMs;
        pullback = Mathf.Lerp(pullback, Mathf.Clamp(accel * accelPullback, -1.2f, 1.5f), 1f - Mathf.Exp(-4f * dt));

        float distance = Mathf.Lerp(distanceSlow, distanceFast, s) + pullback;
        float height = Mathf.Lerp(heightSlow, heightFast, s);
        Vector3 carPos = car.transform.position;
        Vector3 position = carPos - heading * distance + Vector3.up * height;
        Vector3 lookAt = carPos + heading * Mathf.Lerp(4f, 16f, s) + Vector3.up * 0.9f;
        Quaternion rotation = Quaternion.LookRotation(lookAt - position, Vector3.up);

        float shake = s * s * s * (car.Sliding ? 1.6f : 1f);
        shakeTime += dt * Mathf.Lerp(shakeFrequencySlow, shakeFrequencyFast, s);
        float nx = Mathf.PerlinNoise(shakeTime, 0.3f) - 0.5f;
        float ny = Mathf.PerlinNoise(0.7f, shakeTime) - 0.5f;
        float nr = Mathf.PerlinNoise(shakeTime, shakeTime * 0.5f + 5f) - 0.5f;
        position += rotation * new Vector3(nx, ny, 0f) * (2f * shakeAmplitude * shake);
        rotation *= Quaternion.Euler(0f, 0f, nr * 2f * shakeRoll * shake);

        transform.SetPositionAndRotation(position, rotation);
        cam.fieldOfView = Mathf.Lerp(fovSlow, fovFast, s * (2f - s));

        var emission = streaks.emission;
        emission.rateOverTime = s * s * streakRate;
    }
}
