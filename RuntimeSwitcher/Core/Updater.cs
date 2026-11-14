using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Http;
using System.Security.Cryptography;
using System.Threading.Tasks;

namespace RuntimeSwitcher.Core
{
    /// <summary>Asks GitHub for the fork's releases and downloads a release's file, checked against its SHA-256.</summary>
    public static class Updater
    {
        private static readonly Lazy<HttpClient> Client = new Lazy<HttpClient>(() =>
        {
            var client = new HttpClient { Timeout = TimeSpan.FromMinutes(5) };
            // GitHub's API refuses requests without a User-Agent.
            client.DefaultRequestHeaders.UserAgent.ParseAdd("OpenLamina-updater");
            client.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
            return client;
        });

        public static async Task<List<Release>> GetReleasesAsync()
        {
            using (HttpResponseMessage response = await Client.Value.GetAsync(Releases.ApiUrl).ConfigureAwait(false))
            {
                if (!response.IsSuccessStatusCode)
                    throw new HttpRequestException($"GitHub answered {(int)response.StatusCode} {response.ReasonPhrase}.");
                return Releases.Parse(await response.Content.ReadAsStringAsync().ConfigureAwait(false));
            }
        }

        /// <summary>
        /// Downloads <paramref name="asset"/> to <paramref name="destination"/> and keeps it only if its SHA-256 is the
        /// one GitHub lists. <paramref name="progress"/> gets 0–100. Returns the file open for reading, with only
        /// reading shared, so it can't be changed or replaced between the check and its use; dispose it after.
        /// </summary>
        public static async Task<FileStream> DownloadAsync(ReleaseAsset asset, string destination,
            IProgress<int> progress)
        {
            if (asset.Sha256 == null)
                throw new InvalidDataException($"GitHub lists no SHA-256 for {asset.Name}.");
            Directory.CreateDirectory(Path.GetDirectoryName(destination));
            string part = destination + ".part";

            using (HttpResponseMessage response = await Client.Value
                       .GetAsync(asset.DownloadUrl, HttpCompletionOption.ResponseHeadersRead).ConfigureAwait(false))
            {
                response.EnsureSuccessStatusCode();
                long total = response.Content.Headers.ContentLength ?? asset.Size;
                using (Stream input = await response.Content.ReadAsStreamAsync().ConfigureAwait(false))
                using (var output = new FileStream(part, FileMode.Create, FileAccess.Write, FileShare.None))
                {
                    var buffer = new byte[81920];
                    long done = 0;
                    int read;
                    while ((read = await input.ReadAsync(buffer, 0, buffer.Length).ConfigureAwait(false)) > 0)
                    {
                        await output.WriteAsync(buffer, 0, read).ConfigureAwait(false);
                        done += read;
                        if (total > 0)
                            progress?.Report((int)Math.Min(100, done * 100 / total));
                    }
                }
            }

            if (File.Exists(destination))
                File.Delete(destination);
            File.Move(part, destination);

            // Checked through the handle that is returned, so what was checked is what runs.
            var file = new FileStream(destination, FileMode.Open, FileAccess.Read, FileShare.Read);
            try
            {
                string actual = Sha256Of(file);
                if (actual == asset.Sha256)
                {
                    file.Position = 0;
                    return file;
                }

                file.Dispose();
                File.Delete(destination);
                throw new InvalidDataException(
                    $"{asset.Name} was refused: its SHA-256 is {actual}, but GitHub lists {asset.Sha256}.");
            }
            catch
            {
                file.Dispose();
                throw;
            }
        }

        /// <summary>Deletes the folders in <paramref name="folder"/> other than <paramref name="keep"/>, as far as it can.</summary>
        public static void DeleteOtherFolders(string folder, string keep)
        {
            try
            {
                foreach (string old in Directory.GetDirectories(folder))
                {
                    if (string.Equals(Path.GetFileName(old), keep, StringComparison.OrdinalIgnoreCase))
                        continue;
                    try
                    {
                        Directory.Delete(old, true);
                    }
                    catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
                    {
                        // In use or not ours: left for next time.
                    }
                }
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                // No folder yet, or it can't be listed.
            }
        }

        public static string Sha256Of(string file)
        {
            using (FileStream stream = File.OpenRead(file))
                return Sha256Of(stream);
        }

        public static string Sha256Of(Stream stream)
        {
            using (SHA256 sha = SHA256.Create())
                return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();
        }
    }
}
