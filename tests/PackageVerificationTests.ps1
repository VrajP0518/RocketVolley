$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$verify = Join-Path $PSScriptRoot '../tools/VerifyPackage.ps1'
$workingPath = [System.IO.Path]::GetFullPath((Get-Location).Path)
$scratch = Join-Path $workingPath ('package-test-' + [Guid]::NewGuid().ToString('N'))
[void][System.IO.Directory]::CreateDirectory($scratch)
$archive = Join-Path $scratch 'game.zip'
$executable = Join-Path $scratch 'RocketVolley.exe'
[System.IO.File]::WriteAllText($executable, 'tested executable fixture')
$required = @('RocketVolley.exe', 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
    'README.md', 'docs/GAMEPLAY_AUDIT.md', 'docs/USE_CASES.md', 'docs/RELEASE.md',
    'docs/images/gameplay-ball.png', 'docs/images/gameplay-car.png', 'docs/images/gameplay-aerial.png')

function Write-Fixture([string]$Omit = '', [bool]$Stale = $false, [string]$Extra = '', [bool]$BadChecksum = $false) {
    # A fresh artifact per case avoids rewriting a file still being inspected by
    # Windows file watchers; invalid-checksum fixtures write their bytes only once.
    $script:archive = Join-Path $scratch ([Guid]::NewGuid().ToString('N') + '.zip')
    $zip = [System.IO.Compression.ZipFile]::Open($archive, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        $entries = @($required | Where-Object { $_ -ne $Omit })
        if ($Extra) { $entries += $Extra }
        foreach ($name in $entries) {
            $stream = $zip.CreateEntry($name).Open()
            try {
                $bytes = if ($name -eq 'RocketVolley.exe' -and -not $Stale) {
                    [System.IO.File]::ReadAllBytes($executable)
                } else { [System.Text.Encoding]::UTF8.GetBytes('fixture') }
                $stream.Write($bytes, 0, $bytes.Length)
            } finally { $stream.Dispose() }
        }
    } finally { $zip.Dispose() }
    $checksum = if ($BadChecksum) { '0' * 64 } else { (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash }
    [System.IO.File]::WriteAllText("$archive.sha256", $checksum)
}

function Expect-Rejection([string]$Message) {
    $rejected = $false
    try { & $verify -Archive $archive -Executable $executable | Out-Null }
    catch {
        if ($_.Exception.Message -notlike "*$Message*") { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw "Verifier accepted invalid fixture: $Message" }
    Write-Output "PASS rejects $Message"
}

try {
    Write-Fixture
    & $verify -Archive $archive -Executable $executable
    Write-Fixture -Omit 'vcruntime140.dll'
    Expect-Rejection 'Missing package file'
    Write-Fixture -Stale $true
    Expect-Rejection 'differs from the tested build'
    Write-Fixture -Extra '../outside.txt'
    Expect-Rejection 'Unexpected package path'
    Write-Fixture -Extra 'RocketVolleySaveTests.exe'
    Expect-Rejection 'Unexpected package path'
    Write-Fixture -BadChecksum $true
    Expect-Rejection 'checksum mismatch'
} finally {
    # Only delete the freshly created, direct child of this test's working directory.
    $resolved = [System.IO.Path]::GetFullPath($scratch)
    if ([System.IO.Path]::GetDirectoryName($resolved) -eq $workingPath -and
        [System.IO.Path]::GetFileName($resolved) -match '^package-test-[a-f0-9]{32}$') {
        for ($attempt = 0; $attempt -lt 5; ++$attempt) {
            try {
                if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction Stop }
                break
            } catch {
                if ($_.Exception -isnot [System.IO.IOException] -or $attempt -eq 4) { throw }
                # ZIP scanners can briefly retain a read handle after verification.
                Start-Sleep -Milliseconds (150 * [Math]::Pow(2, $attempt))
            }
        }
    }
}
