using System;
using System.Threading;
using System.Windows.Forms;

namespace RuntimeSwitcher
{
    internal static class Program
    {
        private static readonly string[] Commands = { "--use-opencomposite", "--use-steamvr", "--remove", "--can-remove" };

        private const string Usage =
            "OpenComposite.exe                      opens the window\n" +
            "OpenComposite.exe --use-opencomposite  OpenVR games use OpenComposite\n" +
            "OpenComposite.exe --use-steamvr        OpenVR games use SteamVR\n" +
            "OpenComposite.exe --remove             takes OpenComposite out of openvrpaths.vrpath (the uninstaller)\n" +
            "OpenComposite.exe --can-remove         fails if --remove would, or if a game uses OpenComposite\n" +
            "Add --quiet to show no message on failure; the exit code is then 0 for done, 1 for failed, 2 for usage,\n" +
            "3 for refused (a game uses OpenComposite, or openvrpaths.vrpath is read-only).";

        /// <summary>Exists while the window is open: the uninstaller asks for the window to be closed first.</summary>
        public const string WindowMutexName = "OpenComposite.RuntimeSwitcher";

        private static Mutex _windowMutex;

        public static void HoldWindowMutex()
        {
            if (_windowMutex == null)
                _windowMutex = new Mutex(false, WindowMutexName);
        }

        public static void ReleaseWindowMutex()
        {
            _windowMutex?.Dispose();
            _windowMutex = null;
        }

        /// <summary>
        /// With no arguments, the window. With one of the commands of <see cref="Usage"/>, that change and no window
        /// (the installer runs them).
        /// </summary>
        [STAThread]
        private static int Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            if (args.Length == 0)
            {
                HoldWindowMutex();
                Application.Run(new MainWindow());
                ReleaseWindowMutex();
                return 0;
            }

            bool quiet = Array.IndexOf(args, "--quiet") >= 0;
            string command = null;
            foreach (string arg in args)
            {
                if (arg == "--quiet")
                    continue;
                if (command != null || Array.IndexOf(Commands, arg) < 0)
                {
                    if (!quiet)
                        MessageBox.Show(Usage, "OpenComposite", MessageBoxButtons.OK, MessageBoxIcon.Information);
                    return 2;
                }

                command = arg;
            }

            if (command == null)
                return 2;

            try
            {
                switch (command)
                {
                    case "--use-opencomposite":
                        Switcher.UseOpenComposite();
                        break;
                    case "--use-steamvr":
                        Switcher.UseSteamVR();
                        break;
                    case "--remove":
                        Switcher.Remove();
                        break;
                    default:
                        Switcher.CheckRemovable();
                        break;
                }

                return 0;
            }
            catch (Exception e) when (!(e is OutOfMemoryException))
            {
                if (!quiet)
                    MessageBox.Show(e.Message, "OpenComposite", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return e is RefusedException ? 3 : 1;
            }
        }
    }
}
