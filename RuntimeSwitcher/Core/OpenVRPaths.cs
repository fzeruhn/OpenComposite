using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace RuntimeSwitcher.Core
{
    /// <summary>
    /// The user's openvrpaths.vrpath, which tells every OpenVR program which runtime to load: the first folder of
    /// "runtime" with a bin\vrclient_x64.dll (bin\vrclient.dll for 32-bit programs). Only "runtime" is changed; every
    /// other key (config, log, external_drivers, jsonid, version and unknown ones) is written back as it was read.
    /// </summary>
    public sealed class OpenVRPaths
    {
        private readonly JObject _json;

        public string FilePath { get; }

        /// <summary>False when there was no file: Save then creates one.</summary>
        public bool Existed { get; }

        public IReadOnlyList<string> Runtimes { get; private set; }

        private OpenVRPaths(string filePath, JObject json, bool existed)
        {
            FilePath = filePath;
            _json = json;
            Existed = existed;
            Runtimes = ReadRuntimes(json);
        }

        /// <summary>%LOCALAPPDATA%\openvr\openvrpaths.vrpath, where Windows' openvr_api.dll looks.</summary>
        public static string DefaultFile =>
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "openvr",
                "openvrpaths.vrpath");

        /// <summary>Reads the file. Throws <see cref="InvalidDataException"/> if it isn't a paths file.</summary>
        public static OpenVRPaths Load(string filePath)
        {
            if (!File.Exists(filePath))
                return new OpenVRPaths(filePath, NewFile(), false);
            return Parse(filePath, File.ReadAllText(filePath));
        }

        public static OpenVRPaths Parse(string filePath, string text)
        {
            JObject json;
            try
            {
                // Strings are kept as they are: no dates, and numbers as written.
                using (var reader = new JsonTextReader(new StringReader(text)))
                {
                    reader.DateParseHandling = DateParseHandling.None;
                    reader.FloatParseHandling = FloatParseHandling.Decimal;
                    json = JObject.Load(reader);
                    // Nothing but whitespace may follow the object.
                    if (reader.Read())
                        throw new JsonReaderException("Text after the end of the file's object.");
                }
            }
            catch (JsonException e)
            {
                throw new InvalidDataException($"{filePath} isn't valid JSON: {e.Message}", e);
            }

            ReadRuntimes(json, filePath);
            return new OpenVRPaths(filePath, json, true);
        }

        private static JObject NewFile() =>
            new JObject { ["jsonid"] = "vrpathreg", ["runtime"] = new JArray(), ["version"] = 1 };

        private static IReadOnlyList<string> ReadRuntimes(JObject json, string filePath = null)
        {
            JToken token = json["runtime"];
            if (token == null || token.Type == JTokenType.Null)
                return Array.Empty<string>();
            if (token is JArray array && array.All(t => t.Type == JTokenType.String))
                return array.Select(t => (string)t).ToList();
            throw new InvalidDataException($"\"runtime\" in {filePath} isn't a list of folders.");
        }

        public void SetRuntimes(IEnumerable<string> runtimes)
        {
            List<string> list = runtimes.ToList();
            _json["runtime"] = new JArray(list);
            Runtimes = list;
        }

        /// <summary>
        /// The file's text as Save writes it: tab-indented, with "\n" line ends. Steam's is the same JSON laid out a
        /// little differently (a space before each colon, a list's "[" on the next line).
        /// </summary>
        public string ToText()
        {
            var text = new StringWriter { NewLine = "\n" };
            using (var writer = new JsonTextWriter(text)
                   {
                       Formatting = Formatting.Indented, Indentation = 1, IndentChar = '\t', CloseOutput = false,
                   })
                _json.WriteTo(writer);
            return text + "\n";
        }

        /// <summary>
        /// Writes the file through a temporary file next to it, so a reader never sees half a file; the file it
        /// replaces is kept as openvrpaths.vrpath.bak. A read-only file isn't changed (someone made it so, to keep
        /// Steam from rewriting it).
        /// </summary>
        public void Save()
        {
            string folder = Path.GetDirectoryName(FilePath);
            if (!string.IsNullOrEmpty(folder))
                Directory.CreateDirectory(folder);
            if (File.Exists(FilePath) && (File.GetAttributes(FilePath) & FileAttributes.ReadOnly) != 0)
                throw new IOException($"{FilePath} is read-only. Clear its read-only attribute to switch.");

            string temp;
            using (Process self = Process.GetCurrentProcess())
                temp = $"{FilePath}.opencomposite-{self.Id}.tmp";
            try
            {
                byte[] bytes = new UTF8Encoding(false).GetBytes(ToText());
                using (var stream = new FileStream(temp, FileMode.Create, FileAccess.Write, FileShare.None))
                {
                    stream.Write(bytes, 0, bytes.Length);
                    // On the disk before it takes the file's place.
                    stream.Flush(true);
                }

                if (File.Exists(FilePath))
                    File.Replace(temp, FilePath, FilePath + ".bak");
                else
                    File.Move(temp, FilePath);
            }
            finally
            {
                if (File.Exists(temp))
                    File.Delete(temp);
            }
        }
    }
}
