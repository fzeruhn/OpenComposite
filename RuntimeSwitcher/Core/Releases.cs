using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using Newtonsoft.Json.Linq;

namespace RuntimeSwitcher.Core
{
    public sealed class ReleaseAsset
    {
        public string Name { get; set; }
        public string DownloadUrl { get; set; }

        /// <summary>The SHA-256 GitHub lists for the file (its "digest"), in lower-case hex; null if it lists none.</summary>
        public string Sha256 { get; set; }

        public long Size { get; set; }
    }

    public sealed class Release
    {
        public string Tag { get; set; }

        /// <summary>The tag without its "v": 2026.11.06 for v2026.11.06.</summary>
        public string Version => ReleaseVersion.FromTag(Tag);

        public bool Prerelease { get; set; }
        public bool Draft { get; set; }
        public string HtmlUrl { get; set; }
        public List<ReleaseAsset> Assets { get; set; } = new List<ReleaseAsset>();

        public ReleaseAsset Asset(string name) =>
            Assets.FirstOrDefault(a => string.Equals(a.Name, name, StringComparison.OrdinalIgnoreCase));
    }

    /// <summary>The fork's releases, from GitHub's API (GET /repos/{owner}/{repo}/releases).</summary>
    public static class Releases
    {
        public const string Repository = "fzeruhn/OpenLamina";
        public static readonly string ApiUrl = $"https://api.github.com/repos/{Repository}/releases?per_page=30";
        public static readonly string PageUrl = $"https://github.com/{Repository}/releases";

        /// <summary>
        /// The releases in GitHub's answer. Throws a JsonException for text that isn't JSON, and an
        /// <see cref="InvalidDataException"/> for JSON of another shape.
        /// </summary>
        public static List<Release> Parse(string json)
        {
            try
            {
                return ParseReleases(json);
            }
            catch (Exception e) when (e is InvalidOperationException || e is FormatException ||
                                      e is InvalidCastException || e is ArgumentException || e is OverflowException)
            {
                throw new InvalidDataException($"GitHub's list of releases isn't as expected: {e.Message}", e);
            }
        }

        private static List<Release> ParseReleases(string json)
        {
            var releases = new List<Release>();
            foreach (JToken item in JArray.Parse(json))
            {
                var release = new Release
                {
                    Tag = (string)item["tag_name"],
                    Prerelease = (bool?)item["prerelease"] ?? false,
                    Draft = (bool?)item["draft"] ?? false,
                    HtmlUrl = (string)item["html_url"],
                };
                if (release.Tag == null)
                    continue;
                foreach (JToken asset in item["assets"] as JArray ?? new JArray())
                {
                    string digest = (string)asset["digest"];
                    release.Assets.Add(new ReleaseAsset
                    {
                        Name = (string)asset["name"],
                        DownloadUrl = (string)asset["browser_download_url"],
                        Sha256 = digest != null && Regex.IsMatch(digest, "^sha256:[0-9a-fA-F]{64}$")
                            ? digest.Substring(7).ToLowerInvariant()
                            : null,
                        Size = (long?)asset["size"] ?? 0,
                    });
                }

                releases.Add(release);
            }

            return releases;
        }

        /// <summary>
        /// The newest release newer than <paramref name="installed"/> (null: a build of no release, older than all)
        /// that has <paramref name="assetName"/> with a SHA-256, or null. Drafts never; pre-releases only when asked.
        /// </summary>
        public static Release FindUpdate(IEnumerable<Release> releases, string installed, bool includePrereleases,
            string assetName)
        {
            return releases
                .Where(r => !r.Draft && (includePrereleases || !r.Prerelease))
                .Where(r => ReleaseVersion.IsValid(r.Version))
                .Where(r => r.Asset(assetName)?.Sha256 != null && r.Asset(assetName).DownloadUrl != null)
                .Where(r => installed == null || ReleaseVersion.Compare(r.Version, installed) > 0)
                .OrderByDescending(r => r.Version, Comparer<string>.Create(ReleaseVersion.Compare))
                .FirstOrDefault();
        }
    }

    /// <summary>
    /// Release versions: the tag's numbers (2026.11.06), compared as numbers, then a suffix after "-" (0.0.0-test.1)
    /// which is older than none.
    /// </summary>
    public static class ReleaseVersion
    {
        private static readonly Regex Pattern = new Regex(@"^(\d+(?:\.\d+)*)(?:-([0-9A-Za-z.-]+))?$");

        public static string FromTag(string tag) =>
            tag != null && tag.Length > 1 && (tag[0] == 'v' || tag[0] == 'V') ? tag.Substring(1) : tag;

        public static bool IsValid(string version) => version != null && Pattern.IsMatch(version);

        public static int Compare(string a, string b)
        {
            Match ma = Pattern.Match(a ?? ""), mb = Pattern.Match(b ?? "");
            if (!ma.Success || !mb.Success)
                return ma.Success.CompareTo(mb.Success);

            long[] na = Numbers(ma), nb = Numbers(mb);
            for (int i = 0; i < Math.Max(na.Length, nb.Length); i++)
            {
                long x = i < na.Length ? na[i] : 0, y = i < nb.Length ? nb[i] : 0;
                if (x != y)
                    return x.CompareTo(y);
            }

            string sa = ma.Groups[2].Success ? ma.Groups[2].Value : null;
            string sb = mb.Groups[2].Success ? mb.Groups[2].Value : null;
            if (sa == null || sb == null)
                return (sa == null).CompareTo(sb == null);
            return CompareSuffixes(sa, sb);
        }

        /// <summary>
        /// As semver compares pre-release parts: dot by dot, numbers as numbers (rc.2 before rc.10) and before words,
        /// words by their characters; with all of one's parts equal, the one with fewer parts is older.
        /// </summary>
        private static int CompareSuffixes(string a, string b)
        {
            string[] pa = a.Split('.'), pb = b.Split('.');
            for (int i = 0; i < Math.Min(pa.Length, pb.Length); i++)
            {
                bool na = long.TryParse(pa[i], NumberStyles.None, CultureInfo.InvariantCulture, out long x);
                bool nb = long.TryParse(pb[i], NumberStyles.None, CultureInfo.InvariantCulture, out long y);
                int c = na && nb ? x.CompareTo(y) : na != nb ? (na ? -1 : 1) : string.CompareOrdinal(pa[i], pb[i]);
                if (c != 0)
                    return c;
            }

            return pa.Length.CompareTo(pb.Length);
        }

        /// <summary>Four numbers for a Windows file version: 2026.11.06 is 2026.11.6.0; null if it has none.</summary>
        public static string Numeric(string version)
        {
            Match m = Pattern.Match(version ?? "");
            if (!m.Success)
                return null;
            long[] n = Numbers(m);
            if (n.Any(x => x > 65535))
                return null;
            return string.Join(".", n.Concat(new long[4]).Take(4));
        }

        private static long[] Numbers(Match m) =>
            m.Groups[1].Value.Split('.').Select(s => long.TryParse(s, out long x) ? x : long.MaxValue).ToArray();
    }

    /// <summary>The release an OpenLamina folder holds, from its SOURCE.txt (written by scripts/ci/package.py).</summary>
    public static class InstalledVersion
    {
        /// <summary>
        /// "2026.11.06" from a first line "OpenLamina 2026.11.06, commit …" (or "OpenComposite …", as releases before OpenLamina wrote it); null for a build of no release
        /// (dev-…), a missing file or another first line.
        /// </summary>
        public static string FromSourceText(string text)
        {
            string first = text?.Split('\n').FirstOrDefault()?.Trim() ?? "";
            Match m = Regex.Match(first, @"^(?:OpenLamina|OpenComposite) (\S+), commit [0-9a-f]+$");
            return m.Success && ReleaseVersion.IsValid(m.Groups[1].Value) ? m.Groups[1].Value : null;
        }

        public static string Read(string openCompositeFolder)
        {
            string file = Path.Combine(openCompositeFolder, "SOURCE.txt");
            return File.Exists(file) ? FromSourceText(File.ReadAllText(file)) : null;
        }
    }
}
