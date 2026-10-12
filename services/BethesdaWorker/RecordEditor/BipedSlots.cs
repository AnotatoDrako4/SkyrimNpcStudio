using System.Numerics;

namespace SkyrimNpcStudio.Worker.RecordEditor;

/// <summary>
/// Biped object slots. Skyrim stores them as a 32-bit mask where bit (N - 30) is slot N,
/// so slot 30 (head) is bit 0 and slot 61 (FX01) is bit 31.
/// </summary>
public static class BipedSlots
{
    public const int FirstSlot = 30;
    public const int LastSlot = 61;

    private static readonly Dictionary<int, string> Names = new()
    {
        [30] = "Head", [31] = "Hair", [32] = "Body", [33] = "Hands", [34] = "Forearms",
        [35] = "Amulet", [36] = "Ring", [37] = "Feet", [38] = "Calves", [39] = "Shield",
        [40] = "Tail", [41] = "LongHair", [42] = "Circlet", [43] = "Ears",
        [50] = "DecapitateHead", [51] = "Decapitate", [61] = "FX01",
    };

    public static IReadOnlyList<int> ToSlots(uint mask)
    {
        var slots = new List<int>(BitOperations.PopCount(mask));
        for (var bit = 0; bit < 32; bit++)
        {
            if ((mask & (1u << bit)) != 0)
            {
                slots.Add(FirstSlot + bit);
            }
        }

        return slots;
    }

    public static string Describe(int slot) =>
        Names.TryGetValue(slot, out var name) ? $"{slot} - {name}" : $"{slot}";

    public static string DescribeMask(uint mask) => string.Join(", ", ToSlots(mask).Select(Describe));
}
