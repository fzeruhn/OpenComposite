using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Threading.Tasks;
using System.Windows.Forms;
using RuntimeSwitcher.Core;

namespace RuntimeSwitcher
{
    /// <summary>
    /// What OpenVR games use now, the switch both ways, per-app settings and updates. The paths file is read again
    /// whenever the window is activated, since SteamVR rewrites it when it starts.
    /// </summary>
    public sealed class MainWindow : Form
    {
        private const int TextWidth = 460;

        private readonly Label _status = new Label { AutoSize = true };
        private readonly Label _statusDetail = Wrapping(gray: true);
        private readonly Button _useOpenComposite = NewButton("Use OpenLamina");
        private readonly Button _useSteamVR = NewButton("Use SteamVR");
        private readonly Button _perApp = NewButton("Per-app settings…");
        private readonly Label _openXR = Wrapping();
        private readonly Label _mirador = Wrapping(gray: true);
        private readonly Label _version = Wrapping();
        private readonly Label _updateStatus = Wrapping();
        private readonly Button _update = NewButton("Update");
        private readonly LinkLabel _whatsNew = new LinkLabel
        {
            AutoSize = true, Text = "What's new", Visible = false, Anchor = AnchorStyles.Left,
        };
        private readonly ProgressBar _progress = new ProgressBar { Width = 200, Visible = false, Anchor = AnchorStyles.Left };
        private readonly CheckBox _includePrereleases = new CheckBox { AutoSize = true, Text = "Include pre-releases" };

        private List<Release> _releases;
        private Release _pending;
        private AppListForm _appList;

        public MainWindow()
        {
            SuspendLayout();
            Text = "OpenLamina";
            Icon = AppIcon.Get();
            Font = SystemFonts.MessageBoxFont;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            AutoSize = true;
            AutoSizeMode = AutoSizeMode.GrowAndShrink;

            _status.Font = new Font(Font.FontFamily, Font.Size * 1.4f, FontStyle.Bold);
            var statusCaption = new Label
            {
                AutoSize = true, Text = "OpenVR games use:", Anchor = AnchorStyles.Left, Margin = new Padding(0, 0, 4, 0),
            };

            var root = new TableLayoutPanel
            {
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                ColumnCount = 1,
                Padding = new Padding(12),
            };
            root.Controls.Add(Row(statusCaption, _status));
            root.Controls.Add(_statusDetail);
            root.Controls.Add(Row(_useOpenComposite, _useSteamVR, _perApp));
            root.Controls.Add(Wrapping(gray: true,
                text: "Starting SteamVR from Steam also switches OpenVR games back to SteamVR."));
            root.Controls.Add(_openXR);
            root.Controls.Add(_mirador);
            root.Controls.Add(new Label
            {
                BorderStyle = BorderStyle.Fixed3D, AutoSize = false, Height = 2, Width = TextWidth,
                Margin = new Padding(0, 10, 0, 10),
            });
            root.Controls.Add(_version);
            root.Controls.Add(_updateStatus);
            root.Controls.Add(Row(_update, _progress, _whatsNew));
            root.Controls.Add(_includePrereleases);
            Controls.Add(root);

            _useOpenComposite.Click += (s, e) => Switch(Switcher.UseOpenComposite);
            _useSteamVR.Click += (s, e) => Switch(Switcher.UseSteamVR);
            _perApp.Click += (s, e) => ShowAppList();
            _update.Click += async (s, e) => await UpdateAsync();
            _update.Visible = false;
            _whatsNew.LinkClicked += (s, e) => Open(_pending?.HtmlUrl ?? Releases.PageUrl);
            _includePrereleases.Checked = Install.IncludePrereleases;
            _includePrereleases.CheckedChanged += (s, e) =>
            {
                Install.IncludePrereleases = _includePrereleases.Checked;
                ShowUpdate();
            };
            Activated += (s, e) => RefreshStatus();
            Shown += async (s, e) => await CheckForUpdatesAsync();

            AutoScaleDimensions = new SizeF(96F, 96F);
            AutoScaleMode = AutoScaleMode.Dpi;
            ResumeLayout(false);
            PerformLayout();

            RefreshStatus();
        }

        private static Button NewButton(string text) =>
            new Button
            {
                Text = text, AutoSize = true, MinimumSize = new Size(140, 30), Padding = new Padding(6, 0, 6, 0),
                UseVisualStyleBackColor = true,
            };

        private static Label Wrapping(bool gray = false, string text = "") =>
            new Label
            {
                AutoSize = true,
                MaximumSize = new Size(TextWidth, 0),
                Text = text,
                ForeColor = gray ? SystemColors.GrayText : SystemColors.ControlText,
                Margin = new Padding(0, 3, 0, 3),
            };

        private static FlowLayoutPanel Row(params Control[] controls)
        {
            var row = new FlowLayoutPanel
            {
                AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, WrapContents = false,
                Margin = new Padding(0, 3, 0, 3),
            };
            row.Controls.AddRange(controls);
            return row;
        }

        private void RefreshStatus()
        {
            ShowOpenXR();
            OpenVRPaths paths;
            try
            {
                paths = Switcher.Load();
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is InvalidDataException)
            {
                _status.Text = "Unknown";
                _statusDetail.Text = $"{OpenVRPaths.DefaultFile} can't be read: {e.Message}";
                _useOpenComposite.Enabled = _useSteamVR.Enabled = false;
                _mirador.Visible = Install.MiradorInstalled;
                return;
            }

            RuntimeKind current = Switcher.Current(paths);
            string first = paths.Runtimes.Count > 0 ? paths.Runtimes[0] : null;
            switch (current)
            {
                case RuntimeKind.OpenComposite:
                    _status.Text = "OpenLamina";
                    _statusDetail.Text = $"This copy, in {Install.Folder}.";
                    break;
                case RuntimeKind.Mirador:
                    _status.Text = "OpenLamina (Mirador's copy)";
                    _statusDetail.Text = $"Mirador's copy, in {first}. \"Use OpenLamina\" switches to this one.";
                    break;
                case RuntimeKind.SteamVR:
                    _status.Text = "SteamVR";
                    _statusDetail.Text = first;
                    break;
                case RuntimeKind.Other:
                    _status.Text = "Another runtime";
                    _statusDetail.Text = first;
                    break;
                default:
                    _status.Text = "None";
                    _statusDetail.Text = paths.Existed
                        ? "openvrpaths.vrpath lists no runtime."
                        : "There's no openvrpaths.vrpath yet: \"Use OpenLamina\" makes one.";
                    break;
            }

            // OpenVR skips a listed folder that isn't there.
            if (first != null && !Directory.Exists(first))
                _status.Text += " (folder missing)";

            if (!Install.IsComplete)
                _statusDetail.Text += "\nThis copy has no bin\\vrclient_x64.dll and bin\\vrclient.dll: install " +
                                      "OpenLamina again to use it.";

            _useOpenComposite.Enabled = Install.IsComplete && current != RuntimeKind.OpenComposite;
            bool oursListed = Switcher.Lists(paths, Install.Folder);
            bool steamVR = Switcher.HasSteamVR(paths);
            _useSteamVR.Text = steamVR ? "Use SteamVR" : "Stop using OpenLamina";
            _useSteamVR.Enabled = steamVR ? current != RuntimeKind.SteamVR || oursListed : oursListed;

            _mirador.Visible = Install.MiradorInstalled || Switcher.Lists(paths, Install.MiradorCopy);
            _mirador.Text = "Mirador is installed: it has its own copy of OpenLamina for Monado, which its \"Set " +
                            "OpenLamina\" button puts first. Either copy works; OpenVR games use the one shown above.";
        }

        private void ShowOpenXR()
        {
            string runtime = Install.OpenXRRuntime();
            _openXR.Text = runtime != null
                ? $"OpenXR runtime: {runtime}. OpenLamina runs games on it."
                : "No OpenXR runtime is set. OpenLamina needs one: make your headset's runtime the active OpenXR " +
                  "runtime (in its own app).";
            _openXR.ForeColor = runtime != null ? SystemColors.ControlText : Color.Firebrick;
        }

        private void Switch(Action change)
        {
            try
            {
                change();
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException ||
                                      e is InvalidDataException || e is InvalidOperationException)
            {
                MessageBox.Show(this, e.Message, "OpenLamina", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }

            RefreshStatus();
        }

        private void ShowAppList()
        {
            if (_appList != null && !_appList.IsDisposed)
            {
                _appList.Focus();
                return;
            }

            try
            {
                _appList = new AppListForm(Path.Combine(Install.Bin, "applist.json"),
                    Path.Combine(Install.Bin, "apps-config.json"));
                _appList.Show(this);
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException ||
                                      e is Newtonsoft.Json.JsonException)
            {
                MessageBox.Show(this, e.Message, "OpenLamina", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private async Task CheckForUpdatesAsync()
        {
            _updateStatus.Text = "Checking for updates…";
            try
            {
                string installed = Install.Version;
                _version.Text = installed != null
                    ? $"Installed: OpenLamina {installed}"
                    : "Installed: a build that isn't a release (its SOURCE.txt names no release)";
                _releases = await Updater.GetReleasesAsync();
            }
            catch (Exception e) when (e is System.Net.Http.HttpRequestException || e is TaskCanceledException ||
                                      e is Newtonsoft.Json.JsonException || e is InvalidCastException ||
                                      e is ArgumentException || e is InvalidDataException || e is IOException ||
                                      e is UnauthorizedAccessException)
            {
                _updateStatus.Text = $"Couldn't check for updates: {e.Message}";
                return;
            }

            ShowUpdate();
        }

        private void ShowUpdate()
        {
            if (_releases == null)
                return;
            string installed;
            try
            {
                installed = Install.Version;
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                _updateStatus.Text = $"Couldn't check for updates: {e.Message}";
                return;
            }

            _pending = Releases.FindUpdate(_releases, installed, _includePrereleases.Checked,
                Install.SetupAssetName);
            _update.Visible = _whatsNew.Visible = _pending != null;
            if (_pending == null)
            {
                _updateStatus.Text = "You have the newest release.";
                return;
            }

            _updateStatus.Text =
                $"OpenLamina {_pending.Version}{(_pending.Prerelease ? " (a pre-release)" : "")} is out.";
            _update.Text = $"Update to {_pending.Version}";
            _update.Enabled = true;
        }

        /// <summary>Downloads the release's OpenLamina-Setup.exe, checks it, and runs it; the window closes.</summary>
        private async Task UpdateAsync()
        {
            Release release = _pending;
            if (release == null)
                return;
            // Setup installs into this user's folder: refuse when that isn't this copy (another account's elevation, or a
            // copy run from elsewhere), so the update doesn't land somewhere else
            string target = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "Programs", "OpenLamina");
            if (!RuntimeList.SamePath(Install.Folder, target, true))
            {
                MessageBox.Show(this, "The update installs into " + target + ", but this copy of OpenLamina is in " +
                                      Install.Folder + ". Open OpenLamina from the Start menu, then update.",
                    "OpenLamina", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            if (Install.InUse())
            {
                MessageBox.Show(this, "A game is using OpenLamina. Close it, then update.", "OpenLamina",
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            _update.Enabled = _includePrereleases.Enabled = false;
            _progress.Value = 0;
            _progress.Visible = true;
            _updateStatus.Text = $"Downloading OpenLamina {release.Version}…";
            string downloads = Path.Combine(Path.GetTempPath(), "OpenLamina-update");
            string setup = Path.Combine(downloads, release.Version, Install.SetupAssetName);
            Updater.DeleteOtherFolders(downloads, release.Version);
            try
            {
                // Held open, so the checked file is the one started.
                using (await Updater.DownloadAsync(release.Asset(Install.SetupAssetName), setup,
                           new Progress<int>(p => _progress.Value = p)))
                {
                    // /SILENT: only a progress window. /CLOSEAPPLICATIONS: this window, if still open. /OPENSWITCHER:
                    // the window again afterwards. The switch stays as it is, since the folder doesn't change.
                    Program.ReleaseWindowMutex();
                    Process.Start(new ProcessStartInfo(setup, "/SILENT /CLOSEAPPLICATIONS /OPENSWITCHER")
                    {
                        UseShellExecute = true,
                    });
                }
            }
            catch (Exception e) when (e is System.Net.Http.HttpRequestException || e is TaskCanceledException ||
                                      e is IOException || e is UnauthorizedAccessException ||
                                      e is InvalidDataException || e is System.ComponentModel.Win32Exception)
            {
                Program.HoldWindowMutex();
                _progress.Visible = false;
                _update.Enabled = _includePrereleases.Enabled = true;
                _updateStatus.Text = $"The update failed: {e.Message}";
                return;
            }

            Close();
        }

        private void Open(string url)
        {
            try
            {
                Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
            }
            catch (System.ComponentModel.Win32Exception e)
            {
                MessageBox.Show(this, $"{url}\n\n{e.Message}", "OpenLamina", MessageBoxButtons.OK,
                    MessageBoxIcon.Information);
            }
        }
    }

    internal static class AppIcon
    {
        /// <summary>The EXE's own icon (icon.ico, built in), for the windows' title bars.</summary>
        public static Icon Get()
        {
            try
            {
                return Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            }
            catch (ArgumentException)
            {
                return null;
            }
        }
    }
}
