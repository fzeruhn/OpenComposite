using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using RuntimeSwitcher.Core;
using Xunit;

namespace RuntimeSwitcher.Tests
{
    public class RuntimeListTests
    {
        private const string Ours = @"C:\Users\u\AppData\Local\Programs\OpenComposite";
        private const string Mirador = @"C:\Users\u\AppData\Local\Mirador\OpenComposite";
        private const string SteamVR = @"C:\Program Files (x86)\Steam\steamapps\common\SteamVR";
        private const string Other = @"D:\Runtimes\Something";

        [Fact]
        public void UseOpenCompositePutsOursFirstAndKeepsTheRest()
        {
            var result = RuntimeList.UseOpenComposite(new[] { SteamVR, Mirador, Other }, Ours, true);
            Assert.Equal(new[] { Ours, SteamVR, Mirador, Other }, result);
        }

        [Fact]
        public void UseOpenCompositeDoesNotDuplicateOursWhateverItsCaseOrTrailingSlash()
        {
            var result = RuntimeList.UseOpenComposite(new[] { SteamVR, Ours.ToUpperInvariant() + "\\" }, Ours, true);
            Assert.Equal(new[] { Ours, SteamVR }, result);
        }

        [Theory]
        [InlineData(@"C:\Users\u\AppData\Local\Programs\OpenComposite", true)]
        [InlineData("C:/Users/u/AppData/Local/Programs/OpenComposite/", true)]
        [InlineData(@"C:\Users\u/AppData/Local\Programs\OpenComposite\\", true)]
        [InlineData(@"C:\Users\u\AppData\Local\Programs\OpenComposite2", false)]
        public void SamePathFoldsSeparators(string path, bool same)
        {
            Assert.Equal(same, RuntimeList.SamePath(path, Ours, true));
        }

        [Fact]
        public void UseOpenCompositeOnAnEmptyList()
        {
            Assert.Equal(new[] { Ours }, RuntimeList.UseOpenComposite(Array.Empty<string>(), Ours, true));
        }

        [Fact]
        public void UseSteamVRRemovesOursAndMovesSteamVRFirst()
        {
            var result = RuntimeList.UseSteamVR(new[] { Ours, Mirador, SteamVR, Other }, Ours, true);
            Assert.Equal(new[] { SteamVR, Mirador, Other }, result);
        }

        [Fact]
        public void UseSteamVRLeavesMiradorsCopyInTheList()
        {
            var result = RuntimeList.UseSteamVR(new[] { Mirador, SteamVR }, Ours, true);
            Assert.Equal(new[] { SteamVR, Mirador }, result);
        }

        [Fact]
        public void UseSteamVRWithoutASteamVREntryOnlyRemovesOurs()
        {
            var result = RuntimeList.UseSteamVR(new[] { Ours, Other }, Ours, true);
            Assert.Equal(new[] { Other }, result);
        }

        [Fact]
        public void RemoveWhenOursIsFirstPutsSteamVRFirst()
        {
            var result = RuntimeList.Remove(new[] { Ours, Other, SteamVR }, Ours, true);
            Assert.Equal(new[] { SteamVR, Other }, result);
        }

        [Fact]
        public void RemoveWhenOursIsNotFirstKeepsTheOrder()
        {
            var result = RuntimeList.Remove(new[] { Mirador, Ours, SteamVR }, Ours, true);
            Assert.Equal(new[] { Mirador, SteamVR }, result);
        }

        [Fact]
        public void CaseMattersWhenAskedTo()
        {
            var result = RuntimeList.UseOpenComposite(new[] { "/home/u/OpenComposite" }, "/home/u/opencomposite", false);
            Assert.Equal(new[] { "/home/u/opencomposite", "/home/u/OpenComposite" }, result);
        }

        [Theory]
        [InlineData(Ours, RuntimeKind.OpenComposite)]
        [InlineData(Mirador, RuntimeKind.Mirador)]
        [InlineData(SteamVR, RuntimeKind.SteamVR)]
        [InlineData(@"C:\Steam\steamapps\common\SteamVR\", RuntimeKind.SteamVR)]
        [InlineData("/home/u/.local/share/Steam/steamapps/common/SteamVR", RuntimeKind.SteamVR)]
        [InlineData(@"C:\SteamVR Tools\Other", RuntimeKind.Other)]
        [InlineData(null, RuntimeKind.None)]
        public void Classify(string path, RuntimeKind kind)
        {
            Assert.Equal(kind, RuntimeList.Classify(path, Ours, Mirador, true));
        }
    }

    public class OpenVRPathsTests : IDisposable
    {
        private readonly string _folder = Path.Combine(Path.GetTempPath(), "oc-tests-" + Guid.NewGuid().ToString("N"));

        public void Dispose()
        {
            if (Directory.Exists(_folder))
                Directory.Delete(_folder, true);
        }

        private const string SteamsFile = "{\n" +
                                          "\t\"config\" : \n\t[\n\t\t\"C:\\\\Program Files (x86)\\\\Steam\\\\config\"\n\t],\n" +
                                          "\t\"external_drivers\" : \n\t[\n\t\t\"C:\\\\Drivers\\\\example\"\n\t],\n" +
                                          "\t\"jsonid\" : \"vrpathreg\",\n" +
                                          "\t\"log\" : \n\t[\n\t\t\"C:\\\\Program Files (x86)\\\\Steam\\\\logs\"\n\t],\n" +
                                          "\t\"runtime\" : \n\t[\n\t\t\"C:\\\\Program Files (x86)\\\\Steam\\\\steamapps\\\\common\\\\SteamVR\"\n\t],\n" +
                                          "\t\"unknown\" : { \"when\": \"2026-11-04T10:00:00Z\", \"ratio\": 1.50 },\n" +
                                          "\t\"version\" : 1\n}\n";

        [Fact]
        public void KeepsEveryOtherKeyAndTheirOrder()
        {
            string file = Path.Combine(_folder, "openvrpaths.vrpath");
            Directory.CreateDirectory(_folder);
            File.WriteAllText(file, SteamsFile);

            OpenVRPaths paths = OpenVRPaths.Load(file);
            Assert.True(paths.Existed);
            paths.SetRuntimes(RuntimeList.UseOpenComposite(paths.Runtimes, @"C:\OC", true));
            paths.Save();

            var json = Newtonsoft.Json.Linq.JObject.Parse(File.ReadAllText(file),
                new Newtonsoft.Json.Linq.JsonLoadSettings());
            Assert.Equal(new[] { "config", "external_drivers", "jsonid", "log", "runtime", "unknown", "version" },
                json.Properties().Select(p => p.Name));
            Assert.Equal(new[] { @"C:\OC", @"C:\Program Files (x86)\Steam\steamapps\common\SteamVR" },
                json["runtime"].Select(t => (string)t));
            Assert.Equal(@"C:\Drivers\example", (string)json["external_drivers"][0]);
            string text = File.ReadAllText(file);
            Assert.Contains("\"2026-11-04T10:00:00Z\"", text);
            Assert.Contains("1.50", text);
            Assert.Contains("\n\t\"jsonid\"", text);
            Assert.DoesNotContain("\r", text);
            Assert.Empty(Directory.GetFiles(_folder, "*.tmp"));
            Assert.Equal(SteamsFile, File.ReadAllText(file + ".bak"));
        }

        [Fact]
        public void SwitchingThereAndBackGivesTheSameRuntimes()
        {
            string file = Path.Combine(_folder, "openvrpaths.vrpath");
            Directory.CreateDirectory(_folder);
            File.WriteAllText(file, SteamsFile);
            IReadOnlyList<string> before = OpenVRPaths.Load(file).Runtimes;

            OpenVRPaths paths = OpenVRPaths.Load(file);
            paths.SetRuntimes(RuntimeList.UseOpenComposite(paths.Runtimes, @"C:\OC", true));
            paths.Save();
            paths = OpenVRPaths.Load(file);
            paths.SetRuntimes(RuntimeList.UseSteamVR(paths.Runtimes, @"C:\OC", true));
            paths.Save();

            Assert.Equal(before, OpenVRPaths.Load(file).Runtimes);
        }

        [Fact]
        public void AMissingFileIsCreatedWithItsFolder()
        {
            string file = Path.Combine(_folder, "openvr", "openvrpaths.vrpath");
            OpenVRPaths paths = OpenVRPaths.Load(file);
            Assert.False(paths.Existed);
            Assert.Empty(paths.Runtimes);
            paths.SetRuntimes(new[] { @"C:\OC" });
            paths.Save();

            var json = Newtonsoft.Json.Linq.JObject.Parse(File.ReadAllText(file));
            Assert.Equal("vrpathreg", (string)json["jsonid"]);
            Assert.Equal(1, (int)json["version"]);
            Assert.Equal(@"C:\OC", (string)json["runtime"][0]);
        }

        [Theory]
        [InlineData("not json")]
        [InlineData("{\"runtime\": \"C:\\\\one\"}")]
        [InlineData("{\"runtime\": [1, 2]}")]
        [InlineData("{\"runtime\": []} trailing")]
        [InlineData("[]")]
        public void ABrokenFileIsRefused(string text)
        {
            Assert.Throws<InvalidDataException>(() => OpenVRPaths.Parse("openvrpaths.vrpath", text));
        }

        [Fact]
        public void NoRuntimeKeyIsAnEmptyList()
        {
            Assert.Empty(OpenVRPaths.Parse("f", "{\"jsonid\": \"vrpathreg\"}").Runtimes);
        }

        [Fact]
        public void AReadOnlyFileIsNotChanged()
        {
            if (!OperatingSystem.IsWindows())
                return; // The read-only attribute is Windows'.
            string file = Path.Combine(_folder, "openvrpaths.vrpath");
            Directory.CreateDirectory(_folder);
            File.WriteAllText(file, SteamsFile);
            File.SetAttributes(file, FileAttributes.ReadOnly);
            try
            {
                OpenVRPaths paths = OpenVRPaths.Load(file);
                paths.SetRuntimes(new[] { @"C:\OC" });
                Assert.Throws<IOException>(paths.Save);
                Assert.Equal(SteamsFile, File.ReadAllText(file));
            }
            finally
            {
                File.SetAttributes(file, FileAttributes.Normal);
            }
        }
    }

    public class ReleaseTests
    {
        private const string Sha = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

        private static string Json(params (string tag, bool pre, bool draft, bool asset, bool digest)[] releases) =>
            "[" + string.Join(",", releases.Select(r =>
                $"{{\"tag_name\":\"{r.tag}\",\"prerelease\":{(r.pre ? "true" : "false")},\"draft\":{(r.draft ? "true" : "false")}," +
                $"\"html_url\":\"https://github.com/x/y/releases/tag/{r.tag}\",\"assets\":[" +
                (r.asset
                    ? $"{{\"name\":\"OpenComposite-Setup.exe\",\"browser_download_url\":\"https://example/{r.tag}/OpenComposite-Setup.exe\",\"size\":10" +
                      (r.digest ? $",\"digest\":\"sha256:{Sha.ToUpperInvariant()}\"" : ",\"digest\":null") + "}"
                    : "") +
                "]}")) + "]";

        private static Release Find(string json, string installed, bool pre) =>
            Releases.FindUpdate(Releases.Parse(json), installed, pre, "OpenComposite-Setup.exe");

        [Fact]
        public void TheNewestStableReleaseWithTheSetup()
        {
            string json = Json(("v2026.12.01", false, false, true, true), ("v2026.12.10", true, false, true, true),
                ("v2026.11.20", false, false, true, true));
            Assert.Equal("2026.12.01", Find(json, "2026.11.06", false)?.Version);
            Assert.Equal("2026.12.10", Find(json, "2026.11.06", true)?.Version);
        }

        [Fact]
        public void NothingWhenUpToDate()
        {
            string json = Json(("v2026.11.06", false, false, true, true));
            Assert.Null(Find(json, "2026.11.06", true));
            Assert.Null(Find(json, "2026.11.07", true));
        }

        [Fact]
        public void ABuildOfNoReleaseIsOlderThanAll()
        {
            Assert.Equal("2026.11.06", Find(Json(("v2026.11.06", false, false, true, true)), null, false)?.Version);
        }

        [Fact]
        public void DraftsAndReleasesWithoutTheSetupOrItsDigestAreSkipped()
        {
            string json = Json(("v2026.12.03", false, true, true, true), ("v2026.12.02", false, false, false, false),
                ("v2026.12.01", false, false, true, false), ("v2026.11.20", false, false, true, true));
            Release found = Find(json, "2026.11.06", true);
            Assert.Equal("2026.11.20", found?.Version);
            Assert.Equal(Sha, found?.Asset("OpenComposite-Setup.exe")?.Sha256);
        }

        [Fact]
        public void TheOnlyPreReleaseIsFoundOnlyWhenAskedFor()
        {
            // A repository whose only release is a pre-release.
            string json = Json(("v2026.11.06", true, false, true, true));
            Assert.Null(Find(json, null, false));
            Assert.NotNull(Find(json, null, true));
        }

        [Theory]
        [InlineData("[1]")]
        [InlineData("[{\"tag_name\":\"v1\",\"prerelease\":\"maybe\"}]")]
        [InlineData("[{\"tag_name\":\"v1\",\"assets\":[{\"name\":\"a\",\"size\":\"big\"}]}]")]
        [InlineData("[{\"tag_name\":\"v1\",\"assets\":[\"a\"]}]")]
        public void ReleasesOfAnotherShapeAreRefused(string json)
        {
            Assert.Throws<InvalidDataException>(() => Releases.Parse(json));
        }

        [Theory]
        [InlineData("2026.11.06", "2026.11.6", 0)]
        [InlineData("2026.11.10", "2026.11.9", 1)]
        [InlineData("2026.12.01", "2026.11.30", 1)]
        [InlineData("2026.11.06.1", "2026.11.06", 1)]
        [InlineData("0.0.0-test.1", "0.0.0", -1)]
        [InlineData("0.0.0-test.2", "0.0.0-test.1", 1)]
        [InlineData("1.0.0-rc.10", "1.0.0-rc.2", 1)]
        [InlineData("1.0.0-rc.1", "1.0.0-rc", 1)]
        [InlineData("1.0.0-beta", "1.0.0-2", 1)]
        [InlineData("1.0.0-beta.2", "1.0.0-alpha.10", 1)]
        [InlineData("1.0.0-rc.1", "1.0.0-rc.1", 0)]
        [InlineData("1.0.0-rc.a-1", "1.0.0-rc.1", 1)]
        [InlineData("not-a-version", "2026.11.06", -1)]
        public void VersionsCompareAsNumbers(string a, string b, int sign)
        {
            Assert.Equal(sign, Math.Sign(ReleaseVersion.Compare(a, b)));
            Assert.Equal(-sign, Math.Sign(ReleaseVersion.Compare(b, a)));
        }

        [Theory]
        [InlineData("2026.11.06", "2026.11.6.0")]
        [InlineData("1.2", "1.2.0.0")]
        [InlineData("0.0.0-test.1", "0.0.0.0")]
        [InlineData("dev-0123456789ab", null)]
        [InlineData("70000.1.1", null)]
        public void NumericVersions(string version, string numeric)
        {
            Assert.Equal(numeric, ReleaseVersion.Numeric(version));
        }

        [Theory]
        [InlineData("OpenComposite 2026.11.06, commit 7534249abc\nFrom https://…\n", "2026.11.06")]
        [InlineData("OpenComposite dev-0123456789ab, commit 0123456789abcdef\n", null)]
        [InlineData("something else\n", null)]
        [InlineData("", null)]
        public void TheInstalledVersionFromSourceTxt(string text, string version)
        {
            Assert.Equal(version, InstalledVersion.FromSourceText(text));
        }

        [Fact]
        public void OldDownloadsAreDeletedButNotTheCurrentOne()
        {
            string folder = Path.Combine(Path.GetTempPath(), "oc-tests-" + Guid.NewGuid().ToString("N"));
            try
            {
                Directory.CreateDirectory(Path.Combine(folder, "2026.11.06"));
                File.WriteAllText(Path.Combine(folder, "2026.11.06", "OpenComposite-Setup.exe"), "old");
                Directory.CreateDirectory(Path.Combine(folder, "2026.12.01"));
                Updater.DeleteOtherFolders(folder, "2026.12.01");
                Assert.Equal(new[] { "2026.12.01" }, Directory.GetDirectories(folder).Select(Path.GetFileName));
                Updater.DeleteOtherFolders(Path.Combine(folder, "missing"), "2026.12.01");
            }
            finally
            {
                Directory.Delete(folder, true);
            }
        }

        [Fact]
        public void TheDownloadIsCheckedAgainstTheDigest()
        {
            string file = Path.GetTempFileName();
            try
            {
                File.WriteAllText(file, "abc");
                Assert.Equal("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", Updater.Sha256Of(file));
            }
            finally
            {
                File.Delete(file);
            }
        }
    }
}
