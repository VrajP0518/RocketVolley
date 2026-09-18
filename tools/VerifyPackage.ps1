param(
    [Parameter(Mandatory = $true)][string]$Archive,
    [Parameter(Mandatory = $true)][string]$Executable
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$expected = ((Get-Content -LiteralPath "$Archive.sha256" -Raw).Trim() -split '\s+')[0].ToLowerInvariant()
$actual = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -ne $expected) { throw 'Package checksum mismatch.' }

$zip = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Archive).Path)
try {
    $names = @($zip.Entries | ForEach-Object { $_.FullName })
    if (@($names | Select-Object -Unique).Count -ne $names.Count) { throw 'Duplicate package entries.' }
    foreach ($name in $names) {
        if ($name -match '(^[/\\]|(^|[/\\])\.\.([/\\]|$)|:)' -or
            $name -match '(^|[/\\])\.cache([/\\]|$)|Tests\.exe$') {
            throw "Unexpected package path: $name"
        }
    }
    foreach ($required in @('RocketVolley.exe', 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
        'README.md', 'docs/GAMEPLAY_AUDIT.md', 'docs/USE_CASES.md', 'docs/RELEASE.md',
        'docs/images/gameplay-ball.png', 'docs/images/gameplay-car.png', 'docs/images/gameplay-aerial.png')) {
        if ($names -cnotcontains $required) { throw "Missing package file: $required" }
    }
    $stream = $zip.GetEntry('RocketVolley.exe').Open()
    $hasher = [System.Security.Cryptography.SHA256]::Create()
    try {
        $packagedHash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '')
    } finally {
        $stream.Dispose()
        $hasher.Dispose()
    }
    if ($packagedHash -ne (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash) {
        throw 'Packaged executable differs from the tested build.'
    }
} finally {
    $zip.Dispose()
}
Write-Output 'Package verified: checksum, layout, runtime DLLs and tested executable.'
