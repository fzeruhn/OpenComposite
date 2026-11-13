using System;
using System.IO;
using System.Linq;
using RuntimeSwitcher.Core;

namespace RuntimeSwitcher
{
    /// <summary>A change refused on purpose (a game uses OpenComposite, or the paths file is read-only), not a failure.</summary>
    internal sealed class RefusedException : InvalidOperationException
    {
        public RefusedException(string message) : base(message)
        {
        }
    }

    /// <summary>The switch, on the user's openvrpaths.vrpath, as the window's buttons and the command line do it.</summary>
    internal static class Switcher
    {
        // Windows paths: case doesn't matter.
        private const bool IgnoreCase = true;

        public static OpenVRPaths Load() => OpenVRPaths.Load(OpenVRPaths.DefaultFile);

        public static RuntimeKind Current(OpenVRPaths paths) =>
            RuntimeList.First(paths.Runtimes, Install.Folder, Install.MiradorCopy, IgnoreCase);

        public static bool Lists(OpenVRPaths paths, string folder) =>
            RuntimeList.Contains(paths.Runtimes, folder, IgnoreCase);

        public static bool HasSteamVR(OpenVRPaths paths) => paths.Runtimes.Any(RuntimeList.IsSteamVR);

        /// <summary>This install's folder first; every other entry kept after it.</summary>
        public static void UseOpenComposite()
        {
            if (!Install.IsComplete)
                throw new InvalidOperationException(
                    $"{Install.Bin} doesn't have vrclient_x64.dll and vrclient.dll. Install OpenComposite again.");
            OpenVRPaths paths = Load();
            paths.SetRuntimes(RuntimeList.UseOpenComposite(paths.Runtimes, Install.Folder, IgnoreCase));
            paths.Save();
        }

        /// <summary>This install's folder removed and SteamVR first (with no SteamVR entry, ours is only removed).</summary>
        public static void UseSteamVR()
        {
            OpenVRPaths paths = Load();
            if (!paths.Existed)
                return;
            paths.SetRuntimes(RuntimeList.UseSteamVR(paths.Runtimes, Install.Folder, IgnoreCase));
            paths.Save();
        }

        /// <summary>
        /// For the uninstaller, before it asks anything: throws if <see cref="Remove"/> would fail (the paths file can't
        /// be read or is read-only), or if a game has the DLLs loaded, so nothing is left pointing at a removed DLL.
        /// A game or a read-only file is a <see cref="RefusedException"/>.
        /// </summary>
        public static void CheckRemovable()
        {
            if (Install.InUse())
                throw new RefusedException("A game is using OpenComposite. Close it, then uninstall.");
            OpenVRPaths paths = Load();
            if (paths.Existed && Lists(paths, Install.Folder) &&
                (File.GetAttributes(paths.FilePath) & FileAttributes.ReadOnly) != 0)
                throw new RefusedException(
                    $"{paths.FilePath} is read-only, so OpenComposite can't take itself out of it. Clear its " +
                    "read-only attribute, then uninstall.");
        }

        /// <summary>For the uninstaller: this install's folder taken out, SteamVR first if ours was.</summary>
        public static void Remove()
        {
            OpenVRPaths paths = Load();
            if (!paths.Existed || !Lists(paths, Install.Folder))
                return;
            paths.SetRuntimes(RuntimeList.Remove(paths.Runtimes, Install.Folder, IgnoreCase));
            paths.Save();
        }
    }
}
