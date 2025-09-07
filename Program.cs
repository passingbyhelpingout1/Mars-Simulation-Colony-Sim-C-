// Program.cs (dotnet 8)
// dotnet publish -c Release -r win-x64 -p:PublishSingleFile=true -p:PublishTrimmed=true
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Net.Http.Json;
using System.Text.Json;

class Program {
    static async Task<int> Main(string[] args) {
        var root = AppContext.BaseDirectory;
        var bin  = Path.Combine(root, "bin");
        Directory.CreateDirectory(bin);

        // 1) Try newest local exe
        var exe = new DirectoryInfo(bin)
            .EnumerateFiles("*.exe", SearchOption.TopDirectoryOnly)
            .OrderByDescending(f => f.LastWriteTimeUtc)
            .FirstOrDefault();

        if (exe == null) {
            // 2) Optionally call your existing Get-MarsExe.* here or pull release asset via GitHub API
            // (omitted for brevity)
            Console.Error.WriteLine("No game exe found in /bin");
            return 1;
        }

        // 3) Launch
        var psi = new ProcessStartInfo(exe.FullName) {
            WorkingDirectory = bin,
            Arguments = string.Join(' ', args)
        };
        using var p = Process.Start(psi)!;
        p.WaitForExit();
        return p.ExitCode;
    }
}
