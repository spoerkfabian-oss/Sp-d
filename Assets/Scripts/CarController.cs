using UnityEngine;

/// <summary>
/// Simcade raycast car. Four suspension rays carry the body; each grounded wheel produces
/// drive, brake and lateral force limited by a friction circle (grip * load). Downforce adds
/// load with speed, so the car is planted when fast and snaps when the limit is exceeded.
/// </summary>
[RequireComponent(typeof(Rigidbody))]
public class CarController : MonoBehaviour
{
    [Header("Engine")]
    public float topSpeedKmh = 400f;
    public float engineForce = 17000f;
    public float brakeForce = 32000f;
    public float reverseSpeedKmh = 50f;
    public float drag = 0.3f;

    [Header("Grip")]
    public float tyreGrip = 1.9f;
    [Range(0.5f, 1f)] public float slideGripLoss = 0.88f;
    public float handbrakeRearGrip = 0.55f;
    public float downforce = 3.2f;
    [Range(0f, 1f)] public float antiRoll = 0.8f;

    [Header("Steering")]
    public float steerAngleLow = 30f;
    public float steerAngleHigh = 3f;
    public float steerRate = 5f;

    [Header("Suspension")]
    public float restLength = 0.2f;
    public float wheelRadius = 0.34f;
    public float spring = 90000f;
    public float damper = 6500f;
    public Vector3 wheelOffset = new Vector3(0.8f, 0f, 1.35f);

    public Rigidbody Body { get; private set; }
    public float SpeedMs { get; private set; }
    public float SpeedKmh => SpeedMs * 3.6f;
    public float NormalizedSpeed => Mathf.Clamp01(SpeedKmh / topSpeedKmh);
    public int GroundedWheels { get; private set; }
    public bool Sliding { get; private set; }

    readonly Vector3[] mounts = new Vector3[4];
    readonly Transform[] wheelVisuals = new Transform[4];
    readonly float[] springLengths = new float[4];
    float throttle, brake, steer, wheelSpin;
    bool handbrake;
    int rayMask;

    void Awake()
    {
        Body = GetComponent<Rigidbody>();
        Body.mass = 1200f;
        Body.centerOfMass = new Vector3(0f, -0.2f, 0.1f);
        Body.angularDamping = 0.6f;
        Body.linearDamping = 0f;
        Body.interpolation = RigidbodyInterpolation.Interpolate;
        Body.collisionDetectionMode = CollisionDetectionMode.ContinuousDynamic;
        rayMask = ~(1 << gameObject.layer);

        // Order: front-left, front-right, rear-left, rear-right.
        mounts[0] = new Vector3(-wheelOffset.x, wheelOffset.y, wheelOffset.z);
        mounts[1] = new Vector3(wheelOffset.x, wheelOffset.y, wheelOffset.z);
        mounts[2] = new Vector3(-wheelOffset.x, wheelOffset.y, -wheelOffset.z);
        mounts[3] = new Vector3(wheelOffset.x, wheelOffset.y, -wheelOffset.z);
        for (int i = 0; i < 4; i++) springLengths[i] = restLength;
    }

    public void SetWheelVisual(int index, Transform visual) => wheelVisuals[index] = visual;

    void Update()
    {
        float v = Input.GetAxis("Vertical");
        float forwardSpeed = Vector3.Dot(Body.linearVelocity, transform.forward);
        if (v >= 0f) { throttle = v; brake = 0f; }
        else if (forwardSpeed > 1.5f) { throttle = 0f; brake = -v; }
        else { throttle = v; brake = 0f; } // reverse
        handbrake = Input.GetKey(KeyCode.Space);
        steer = Mathf.MoveTowards(steer, Input.GetAxisRaw("Horizontal"), steerRate * Time.deltaTime);

        wheelSpin += forwardSpeed / wheelRadius * Mathf.Rad2Deg * Time.deltaTime;
        float steerAngle = CurrentSteerAngle();
        for (int i = 0; i < 4; i++)
        {
            if (wheelVisuals[i] == null) continue;
            wheelVisuals[i].localPosition = mounts[i] + Vector3.down * springLengths[i];
            wheelVisuals[i].localRotation =
                Quaternion.Euler(wheelSpin, i < 2 ? steerAngle : 0f, 0f) * Quaternion.Euler(0f, 0f, 90f);
        }
    }

    float CurrentSteerAngle() => steer * Mathf.Lerp(steerAngleLow, steerAngleHigh, Mathf.Sqrt(NormalizedSpeed));

    void FixedUpdate()
    {
        float dt = Time.fixedDeltaTime;
        Vector3 velocity = Body.linearVelocity;
        SpeedMs = velocity.magnitude;
        Vector3 up = transform.up;
        float topSpeed = topSpeedKmh / 3.6f;
        float steerAngle = CurrentSteerAngle();
        float wheelMass = Body.mass * 0.25f;
        float comHeightRef = Vector3.Dot(Body.worldCenterOfMass, up);

        Body.AddForce(-velocity * (SpeedMs * drag));

        int grounded = 0;
        bool sliding = false;
        for (int i = 0; i < 4; i++)
        {
            Vector3 origin = transform.TransformPoint(mounts[i]);
            float maxLength = restLength + wheelRadius;
            if (!Physics.Raycast(origin, -up, out RaycastHit hit, maxLength, rayMask, QueryTriggerInteraction.Ignore))
            {
                springLengths[i] = restLength;
                continue;
            }
            grounded++;

            Vector3 pointVelocity = Body.GetPointVelocity(hit.point);
            float springLength = hit.distance - wheelRadius;
            springLengths[i] = springLength;
            float load = spring * (restLength - springLength) - damper * Vector3.Dot(pointVelocity, up);
            if (load <= 0f) continue;
            Body.AddForceAtPosition(up * load, origin);

            Quaternion wheelRotation = transform.rotation * Quaternion.Euler(0f, i < 2 ? steerAngle : 0f, 0f);
            Vector3 forward = Vector3.ProjectOnPlane(wheelRotation * Vector3.forward, hit.normal).normalized;
            Vector3 right = Vector3.Cross(hit.normal, forward);
            float vLong = Vector3.Dot(pointVelocity, forward);
            float vLat = Vector3.Dot(pointVelocity, right);

            float longForce;
            if (throttle > 0f) longForce = throttle * engineForce * 0.25f * Mathf.Clamp01(1f - vLong / topSpeed);
            else if (throttle < 0f) longForce = throttle * engineForce * 0.25f * Mathf.Clamp01(1f + vLong * 3.6f / reverseSpeedKmh);
            else longForce = -Mathf.Clamp(vLong * wheelMass / dt, -150f, 150f); // rolling resistance

            bool locked = handbrake && i >= 2;
            float braking = locked ? brakeForce : brake * brakeForce * 0.25f;
            if (braking > 0f) longForce = -Mathf.Sign(vLong) * Mathf.Min(braking, Mathf.Abs(vLong) * wheelMass / dt);

            float latForce = -vLat * wheelMass / dt * 0.5f;

            float limit = load * tyreGrip * (locked ? handbrakeRearGrip : 1f);
            Vector2 demand = new Vector2(longForce, latForce);
            if (demand.magnitude > limit)
            {
                demand = demand.normalized * (limit * slideGripLoss);
                if (i >= 2 && SpeedMs > 5f) sliding = true; // only a rear slide counts as losing the car
            }

            // Raise the application point towards the centre of mass to limit body roll.
            Vector3 point = hit.point + up * ((comHeightRef - Vector3.Dot(hit.point, up)) * antiRoll);
            Body.AddForceAtPosition(forward * demand.x + right * demand.y, point);
        }

        GroundedWheels = grounded;
        Sliding = sliding;
        if (grounded > 0)
            Body.AddForce(-up * (downforce * SpeedMs * SpeedMs * (grounded * 0.25f)));
    }

    public void Teleport(Vector3 position, Quaternion rotation)
    {
        Body.position = position;
        Body.rotation = rotation;
        transform.SetPositionAndRotation(position, rotation);
        Body.linearVelocity = Vector3.zero;
        Body.angularVelocity = Vector3.zero;
    }
}
