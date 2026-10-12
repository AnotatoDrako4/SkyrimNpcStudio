using System.Text.Json;
using SkyrimNpcStudio.TestFixtures;

if (args.Length != 1 || !Path.IsPathFullyQualified(args[0]))
{
    Console.Error.WriteLine("usage: SkyrimNpcStudio.FixtureGen <absolute output directory>");
    return 2;
}

var fixture = SyntheticLoadOrder.Create(args[0]);
var description = new
{
    dataDirectory = fixture.DataDirectory,
    plugins = fixture.LoadOrder.Select(p => new { fileName = p.FileName, path = p.Path }),
    lydia = SyntheticLoadOrder.Lydia.ToString(),
    steelBoots = SyntheticLoadOrder.SteelBoots.ToString(),
    steelCuirass = SyntheticLoadOrder.SteelCuirass.ToString(),
    elvenCuirass = SyntheticLoadOrder.ElvenCuirass.ToString(),
    crimsonCuirass = fixture.CrimsonCuirass.ToString(),
    crimsonCloak = fixture.CrimsonCloak.ToString(),
};
File.WriteAllText(Path.Combine(args[0], "fixture.json"), JsonSerializer.Serialize(description));
return 0;
