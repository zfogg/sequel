$ErrorActionPreference = 'Stop'

foreach ($tool in @('cmake', 'ninja', 'python', 'cargo')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Required Windows build tool not found: $tool"
    }
}

Write-Host 'CMake, Ninja, Python, and Cargo are available.'
