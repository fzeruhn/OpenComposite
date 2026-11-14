using System;
using System.Collections.Generic;
using System.Linq;

namespace RuntimeSwitcher.Core
{
    public enum RuntimeKind
    {
        /// <summary>The list is empty.</summary>
        None,

        /// <summary>This install's OpenLamina folder.</summary>
        OpenComposite,

        /// <summary>Mirador's per-user copy of OpenComposite (%LOCALAPPDATA%\Mirador\OpenComposite).</summary>
        Mirador,

        SteamVR,
        Other,
    }

    /// <summary>
    /// The changes to openvrpaths.vrpath's "runtime" list. The first entry is the runtime; the others stay in the list,
    /// in their order: OpenLamina's per-app settings send a program to SteamVR through the list's SteamVR entry, and
    /// Mirador's copy is never removed or reordered here except by moving another entry before it.
    /// </summary>
    public static class RuntimeList
    {
        public static string Normalize(string path) => path.Trim().TrimEnd('\\', '/');

        /// <summary>The same folder, whichever separators and trailing separator each is written with.</summary>
        public static bool SamePath(string a, string b, bool ignoreCase) =>
            string.Equals(Normalize(a).Replace('/', '\\'), Normalize(b).Replace('/', '\\'),
                ignoreCase ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);

        /// <summary>SteamVR's folder (steamapps\common\SteamVR), as Steam registers it.</summary>
        public static bool IsSteamVR(string path)
        {
            string normalized = Normalize(path);
            int slash = normalized.LastIndexOfAny(new[] { '\\', '/' });
            return string.Equals(normalized.Substring(slash + 1), "SteamVR", StringComparison.OrdinalIgnoreCase);
        }

        public static RuntimeKind Classify(string path, string ours, string mirador, bool ignoreCase)
        {
            if (path == null)
                return RuntimeKind.None;
            if (SamePath(path, ours, ignoreCase))
                return RuntimeKind.OpenComposite;
            if (mirador != null && SamePath(path, mirador, ignoreCase))
                return RuntimeKind.Mirador;
            return IsSteamVR(path) ? RuntimeKind.SteamVR : RuntimeKind.Other;
        }

        public static RuntimeKind First(IReadOnlyList<string> runtimes, string ours, string mirador, bool ignoreCase) =>
            Classify(runtimes.FirstOrDefault(), ours, mirador, ignoreCase);

        public static bool Contains(IEnumerable<string> runtimes, string path, bool ignoreCase) =>
            runtimes.Any(r => SamePath(r, path, ignoreCase));

        /// <summary>Ours first, every other entry after it in its order.</summary>
        public static List<string> UseOpenComposite(IEnumerable<string> runtimes, string ours, bool ignoreCase)
        {
            var list = runtimes.Where(r => !SamePath(r, ours, ignoreCase)).ToList();
            list.Insert(0, Normalize(ours));
            return list;
        }

        /// <summary>Ours removed and SteamVR's folder first. Without a SteamVR entry, ours is only removed.</summary>
        public static List<string> UseSteamVR(IEnumerable<string> runtimes, string ours, bool ignoreCase)
        {
            var list = runtimes.Where(r => !SamePath(r, ours, ignoreCase)).ToList();
            int steamVR = list.FindIndex(IsSteamVR);
            if (steamVR > 0)
            {
                string entry = list[steamVR];
                list.RemoveAt(steamVR);
                list.Insert(0, entry);
            }

            return list;
        }

        /// <summary>
        /// For the uninstall: ours removed. If it was first, SteamVR's folder becomes first (as UseSteamVR); if it
        /// wasn't, the order of the others is left as it is.
        /// </summary>
        public static List<string> Remove(IReadOnlyList<string> runtimes, string ours, bool ignoreCase)
        {
            if (runtimes.Count > 0 && SamePath(runtimes[0], ours, ignoreCase))
                return UseSteamVR(runtimes, ours, ignoreCase);
            return runtimes.Where(r => !SamePath(r, ours, ignoreCase)).ToList();
        }
    }
}
