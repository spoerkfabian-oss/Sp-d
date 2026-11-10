using UnityEngine;

/// <summary>Lap timing from progress along the track centreline, reset handling and the HUD.</summary>
public class LapTimer : MonoBehaviour
{
    public CarController car;
    public Track track;

    const int Sectors = 4;
    int index, lastSector, lap;
    float lapStart, lastLap, bestLap;
    bool running;
    GUIStyle big, small;

    void Update()
    {
        if (car == null || track == null) return;
        Vector3[] points = track.Points;
        int n = points.Length;
        Vector3 position = car.transform.position;

        // Nearest centreline sample, searched near the previous one so crossings cannot confuse it.
        float best = float.MaxValue;
        int bestIndex = index;
        for (int offset = -25; offset <= 25; offset++)
        {
            int i = ((index + offset) % n + n) % n;
            float d = (points[i] - position).sqrMagnitude;
            if (d < best) { best = d; bestIndex = i; }
        }
        index = bestIndex;

        if (!running && car.SpeedMs > 1f) { running = true; lapStart = Time.time; }

        int sector = index * Sectors / n;
        if (sector == (lastSector + 1) % Sectors)
        {
            lastSector = sector;
            if (sector == 0 && running)
            {
                lastLap = Time.time - lapStart;
                if (bestLap <= 0f || lastLap < bestLap) bestLap = lastLap;
                lapStart = Time.time;
                lap++;
            }
        }

        bool fellOff = position.y < points[index].y - 8f;
        if (Input.GetKeyDown(KeyCode.R) || fellOff) ResetCar();
    }

    public void ResetCar()
    {
        car.Teleport(track.Points[index] + Vector3.up * 1f, Quaternion.LookRotation(track.Tangents[index], Vector3.up));
    }

    void OnGUI()
    {
        if (car == null) return;
        if (big == null)
        {
            big = new GUIStyle(GUI.skin.label) { fontSize = 56, fontStyle = FontStyle.Bold, alignment = TextAnchor.LowerRight };
            small = new GUIStyle(GUI.skin.label) { fontSize = 22 };
        }

        GUI.Label(new Rect(Screen.width - 420, Screen.height - 110, 400, 90), Mathf.RoundToInt(car.SpeedKmh) + " km/h", big);
        float current = running ? Time.time - lapStart : 0f;
        GUI.Label(new Rect(20, 15, 500, 30), "Lap " + (lap + 1) + "   " + Format(current), small);
        GUI.Label(new Rect(20, 45, 500, 30), "Last " + Format(lastLap) + "   Best " + Format(bestLap), small);
        GUI.Label(new Rect(20, Screen.height - 40, 800, 30), "W/S throttle+brake   A/D steer   Space handbrake   R reset", small);
    }

    static string Format(float seconds)
    {
        if (seconds <= 0f) return "--:--.---";
        int minutes = (int)(seconds / 60f);
        return minutes.ToString("00") + ":" + (seconds - minutes * 60f).ToString("00.000");
    }
}
