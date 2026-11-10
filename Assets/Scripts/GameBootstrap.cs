using UnityEngine;

/// <summary>
/// Builds the whole prototype from code when Play is pressed in any scene, so no scene or
/// prefab setup is needed. Skipped if the scene already contains a CarController.
/// </summary>
public static class GameBootstrap
{
    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    static void Init()
    {
        if (Object.FindFirstObjectByType<CarController>() != null) return;
        Time.fixedDeltaTime = 0.01f;

        var track = new GameObject("Track").AddComponent<Track>();
        track.Build();

        CarController car = BuildCar();
        car.Teleport(track.Points[0] + Vector3.up * 1f, Quaternion.LookRotation(track.Tangents[0], Vector3.up));

        Camera cam = Camera.main;
        if (cam == null)
        {
            var camObject = new GameObject("Main Camera") { tag = "MainCamera" };
            cam = camObject.AddComponent<Camera>();
            camObject.AddComponent<AudioListener>();
        }
        cam.gameObject.AddComponent<ChaseCamera>().car = car;

        if (Object.FindFirstObjectByType<Light>() == null)
        {
            var sun = new GameObject("Sun").AddComponent<Light>();
            sun.type = LightType.Directional;
            sun.shadows = LightShadows.Soft;
            sun.transform.rotation = Quaternion.Euler(45f, -30f, 0f);
        }

        var timer = new GameObject("LapTimer").AddComponent<LapTimer>();
        timer.car = car;
        timer.track = track;
    }

    static CarController BuildCar()
    {
        const int carLayer = 2; // Ignore Raycast: keeps the suspension rays off the car itself.
        var root = new GameObject("Car") { layer = carLayer };
        root.AddComponent<Rigidbody>();
        var box = root.AddComponent<BoxCollider>();
        box.size = new Vector3(1.8f, 0.5f, 4.2f);
        box.center = new Vector3(0f, 0.1f, 0f);
        // Frictionless body: all grip comes from the tyre model, walls just deflect the car.
        box.material = new PhysicsMaterial("CarBody")
        {
            dynamicFriction = 0f, staticFriction = 0f, bounciness = 0.1f,
            frictionCombine = PhysicsMaterialCombine.Minimum,
        };

        Material paint = Track.MakeMaterial(new Color(0.9f, 0.35f, 0.05f));
        Material dark = Track.MakeMaterial(new Color(0.06f, 0.06f, 0.07f));
        Part(root.transform, PrimitiveType.Cube, new Vector3(0f, 0.1f, 0f), new Vector3(1.8f, 0.5f, 4.2f), paint);
        Part(root.transform, PrimitiveType.Cube, new Vector3(0f, 0.5f, -0.3f), new Vector3(1.4f, 0.4f, 1.8f), dark);
        Part(root.transform, PrimitiveType.Cube, new Vector3(0f, 0.75f, -2f), new Vector3(1.8f, 0.06f, 0.4f), dark);

        var car = root.AddComponent<CarController>();
        float diameter = car.wheelRadius * 2f;
        for (int i = 0; i < 4; i++)
            car.SetWheelVisual(i, Part(root.transform, PrimitiveType.Cylinder, Vector3.zero, new Vector3(diameter, 0.14f, diameter), dark));
        return car;
    }

    static Transform Part(Transform parent, PrimitiveType type, Vector3 localPosition, Vector3 scale, Material material)
    {
        var go = GameObject.CreatePrimitive(type);
        Object.Destroy(go.GetComponent<Collider>());
        go.layer = parent.gameObject.layer;
        go.transform.SetParent(parent, false);
        go.transform.localPosition = localPosition;
        go.transform.localScale = scale;
        go.GetComponent<Renderer>().sharedMaterial = material;
        return go.transform;
    }
}
