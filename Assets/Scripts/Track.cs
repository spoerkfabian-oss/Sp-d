using System.Collections.Generic;
using UnityEngine;

/// <summary>
/// Builds a closed circuit from control points: road, walls, centre dashes and roadside
/// pylons (close passing objects are what make speed readable).
/// </summary>
public class Track : MonoBehaviour
{
    public float width = 15f;
    public float sampleSpacing = 5f;
    public float wallHeight = 1.3f;

    public Vector3[] controlPoints =
    {
        new Vector3(0, 0, 0), new Vector3(0, 0, 500), new Vector3(120, 6, 820),
        new Vector3(420, 10, 900), new Vector3(700, 6, 760), new Vector3(760, 0, 460),
        new Vector3(560, 0, 260), new Vector3(620, 4, 0), new Vector3(480, 8, -260),
        new Vector3(180, 4, -320), new Vector3(20, 0, -200),
    };

    public Vector3[] Points { get; private set; }
    public Vector3[] Tangents { get; private set; }
    public float Length { get; private set; }

    public void Build()
    {
        Sample();
        int n = Points.Length;
        var left = new Vector3[n];
        var right = new Vector3[n];
        var leftTop = new Vector3[n];
        var rightTop = new Vector3[n];
        var leftLow = new Vector3[n];
        var rightLow = new Vector3[n];
        var dashA = new Vector3[n];
        var dashB = new Vector3[n];
        var sides = new Vector3[n];
        for (int i = 0; i < n; i++)
        {
            Vector3 side = Vector3.Cross(Vector3.up, Tangents[i]).normalized;
            sides[i] = side;
            left[i] = Points[i] - side * (width * 0.5f);
            right[i] = Points[i] + side * (width * 0.5f);
            leftTop[i] = left[i] + Vector3.up * wallHeight;
            rightTop[i] = right[i] + Vector3.up * wallHeight;
            leftLow[i] = left[i] + Vector3.down * 3f;
            rightLow[i] = right[i] + Vector3.down * 3f;
            dashA[i] = Points[i] - side * 0.15f + Vector3.up * 0.03f;
            dashB[i] = Points[i] + side * 0.15f + Vector3.up * 0.03f;
        }

        Material road = MakeMaterial(new Color(0.13f, 0.13f, 0.15f));
        Material wall = MakeMaterial(new Color(0.75f, 0.75f, 0.78f));
        Material paint = MakeMaterial(Color.white);
        Material red = MakeMaterial(new Color(0.85f, 0.1f, 0.1f));

        Strip("Road", left, right, road, true, 0);
        Strip("Dashes", dashA, dashB, paint, false, 2);
        // Each wall is built in both windings so it renders and collides from either side.
        Strip("WallLeftInner", leftTop, leftLow, wall, true, 0);
        Strip("WallLeftOuter", leftLow, leftTop, wall, true, 0);
        Strip("WallRightInner", rightLow, rightTop, wall, true, 0);
        Strip("WallRightOuter", rightTop, rightLow, wall, true, 0);

        var pylons = new GameObject("Pylons").transform;
        pylons.SetParent(transform, false);
        for (int i = 0; i < n; i += 4)
        {
            Material m = (i / 4) % 2 == 0 ? red : paint;
            Pylon(pylons, left[i] - sides[i] * 0.9f, m);
            Pylon(pylons, right[i] + sides[i] * 0.9f, m);
        }

        var ground = GameObject.CreatePrimitive(PrimitiveType.Plane);
        ground.name = "Ground";
        ground.transform.SetParent(transform, false);
        ground.transform.position = new Vector3(380f, -3f, 290f);
        ground.transform.localScale = new Vector3(400f, 1f, 400f);
        ground.GetComponent<Renderer>().sharedMaterial = MakeMaterial(new Color(0.2f, 0.36f, 0.18f));
    }

    void Sample()
    {
        var points = new List<Vector3>();
        int count = controlPoints.Length;
        for (int i = 0; i < count; i++)
        {
            Vector3 p0 = controlPoints[(i - 1 + count) % count];
            Vector3 p1 = controlPoints[i];
            Vector3 p2 = controlPoints[(i + 1) % count];
            Vector3 p3 = controlPoints[(i + 2) % count];
            int steps = Mathf.Max(2, Mathf.CeilToInt(Vector3.Distance(p1, p2) / sampleSpacing));
            for (int s = 0; s < steps; s++) points.Add(CatmullRom(p0, p1, p2, p3, s / (float)steps));
        }

        Points = points.ToArray();
        int n = Points.Length;
        Tangents = new Vector3[n];
        Length = 0f;
        for (int i = 0; i < n; i++)
        {
            Tangents[i] = (Points[(i + 1) % n] - Points[(i - 1 + n) % n]).normalized;
            Length += Vector3.Distance(Points[i], Points[(i + 1) % n]);
        }
    }

    static Vector3 CatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
    {
        float t2 = t * t, t3 = t2 * t;
        return 0.5f * (2f * p1 + (p2 - p0) * t + (2f * p0 - 5f * p1 + 4f * p2 - p3) * t2
                       + (3f * p1 - p0 - 3f * p2 + p3) * t3);
    }

    /// <summary>Closed quad strip between edges a and b; faces along cross(forward, b - a).</summary>
    void Strip(string stripName, Vector3[] a, Vector3[] b, Material material, bool collide, int dashEvery)
    {
        int n = a.Length;
        var vertices = new Vector3[n * 2];
        var triangles = new List<int>(n * 6);
        for (int i = 0; i < n; i++)
        {
            vertices[i * 2] = a[i];
            vertices[i * 2 + 1] = b[i];
            if (dashEvery > 0 && (i / dashEvery) % 2 != 0) continue;
            int next = (i + 1) % n;
            triangles.Add(i * 2); triangles.Add(next * 2); triangles.Add(i * 2 + 1);
            triangles.Add(i * 2 + 1); triangles.Add(next * 2); triangles.Add(next * 2 + 1);
        }

        var mesh = new Mesh { name = stripName };
        mesh.vertices = vertices;
        mesh.triangles = triangles.ToArray();
        mesh.RecalculateNormals();
        mesh.RecalculateBounds();

        var go = new GameObject(stripName);
        go.transform.SetParent(transform, false);
        go.AddComponent<MeshFilter>().sharedMesh = mesh;
        go.AddComponent<MeshRenderer>().sharedMaterial = material;
        if (collide) go.AddComponent<MeshCollider>().sharedMesh = mesh;
    }

    static void Pylon(Transform parent, Vector3 basePosition, Material material)
    {
        var go = GameObject.CreatePrimitive(PrimitiveType.Cube);
        Destroy(go.GetComponent<Collider>());
        go.transform.SetParent(parent, false);
        go.transform.position = basePosition + Vector3.up * 1.75f;
        go.transform.localScale = new Vector3(0.4f, 3.5f, 0.4f);
        go.GetComponent<Renderer>().sharedMaterial = material;
    }

    public static Material MakeMaterial(Color color)
    {
        Shader shader = Shader.Find("Universal Render Pipeline/Lit");
        if (shader == null || UnityEngine.Rendering.GraphicsSettings.currentRenderPipeline == null)
            shader = Shader.Find("Standard");
        var material = new Material(shader);
        if (material.HasProperty("_BaseColor")) material.SetColor("_BaseColor", color);
        if (material.HasProperty("_Color")) material.SetColor("_Color", color);
        return material;
    }
}
