using System;
using System.IO;
using System.Linq;
using Microsoft.Win32;
using Newtonsoft.Json.Linq;
using RuntimeSwitcher.Core;

namespace RuntimeSwitcher
{
    /// <summary>
    /// Where things are. OpenComposite.exe sits in the OpenComposite folder the installer made
    /// (%LOCALAPPDATA%\Programs\OpenComposite): that folder, with bin\vrclient_x64.dll and bin\vrclient.dll, is the
    /// runtime folder it puts in openvrpaths.vrpath.
    /// </summary>
    internal static class Install
    {
        public const string SetupAssetName = "OpenComposite-Setup.exe";

        public static string Folder { get; } = RuntimeList.Normalize(AppDomain.CurrentDomain.BaseDirectory);

        public static string Bin => Path.Combine(Folder, "bin");

        public static string[] Libraries => new[] { "vrclient_x64.dll", "vrclient.dll" }
            .Select(name => Path.Combine(Bin, name)).ToArray();

        /// <summary>Both DLLs there: a folder OpenVR games can be pointed at.</summary>
        public static bool IsComplete => Libraries.All(File.Exists);

        public static string Version => InstalledVersion.Read(Folder);

        /// <summary>Mirador's own per-user copy, which Mirador's "Set OpenComposite" puts first.</summary>
        public static string MiradorCopy { get; } =
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Mirador",
                "OpenComposite");

        public static bool MiradorInstalled =>
            Directory.Exists(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "Mirador"));

        /// <summary>True if a game (or anything else) has one of the DLLs loaded, so it can't be replaced now.</summary>
        public static bool InUse()
        {
            foreach (string library in Libraries.Where(File.Exists))
            {
                try
                {
                    // A loaded DLL can't be opened for writing.
                    using (new FileStream(library, FileMode.Open, FileAccess.ReadWrite, FileShare.None))
                    {
                    }
                }
                catch (IOException)
                {
                    return true;
                }
                catch (UnauthorizedAccessException)
                {
                    // Not ours to write (read-only): the installer reports it.
                }
            }

            return false;
        }

        /// <summary>
        /// The active OpenXR runtime's name (its manifest's runtime.name, or the manifest's file name), or null with
        /// none set. OpenComposite runs games on it. Never throws.
        /// </summary>
        public static string OpenXRRuntime()
        {
            try
            {
                using (RegistryKey hklm = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64))
                using (RegistryKey key = hklm.OpenSubKey(@"SOFTWARE\Khronos\OpenXR\1"))
                {
                    if (!(key?.GetValue("ActiveRuntime") is string manifest) || manifest.Trim().Length == 0)
                        return null;
                    try
                    {
                        string name = (string)JObject.Parse(File.ReadAllText(manifest))["runtime"]?["name"];
                        if (!string.IsNullOrWhiteSpace(name))
                            return name.Trim();
                    }
                    catch (Exception)
                    {
                        return Path.GetFileName(manifest) + " (can't be read)";
                    }

                    return Path.GetFileName(manifest);
                }
            }
            catch (Exception)
            {
                // Only shown: whatever went wrong, the window still opens.
                return "unknown (the registry can't be read)";
            }
        }

        private static string SettingsFile => Path.Combine(Folder, "updates.json");

        /// <summary>The "Include pre-releases" box, kept in updates.json next to the app.</summary>
        public static bool IncludePrereleases
        {
            get
            {
                try
                {
                    return File.Exists(SettingsFile) &&
                           ((bool?)JObject.Parse(File.ReadAllText(SettingsFile))["include_prereleases"] ?? false);
                }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException ||
                                          e is Newtonsoft.Json.JsonException || e is FormatException ||
                                          e is ArgumentException)
                {
                    return false;
                }
            }
            set
            {
                try
                {
                    File.WriteAllText(SettingsFile, new JObject { ["include_prereleases"] = value } + "\n");
                }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
                {
                    // Only a preference: the box keeps its state until the app closes.
                }
            }
        }
    }
}
