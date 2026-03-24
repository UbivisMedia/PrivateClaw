param(
    [Parameter(Mandatory = $true)]
    [string]$TargetWikiDir,

    [string]$SourceWikiDir = "docs/wiki",
    [string]$SourceExamplesDir = "docs/examples",
    [string]$RepositorySlug = "",
    [string]$RepositoryRevision = "main"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Normalize-RelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $normalized = $Path -replace "\\", "/"
    while ($normalized.StartsWith("./")) {
        $normalized = $normalized.Substring(2)
    }

    return $normalized
}

function Get-OutputRelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    if ($RelativePath -ieq "README.md") {
        return "Home.md"
    }

    return $RelativePath
}

function Get-RepositoryBlobUrl {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    if ([string]::IsNullOrWhiteSpace($script:RepositorySlug)) {
        return $RelativePath
    }

    $normalized = Normalize-RelativePath $RelativePath
    return "https://github.com/$($script:RepositorySlug)/blob/$($script:RepositoryRevision)/$normalized"
}

function Convert-MarkdownContent {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Content
    )

    $updated = $Content

    $updated = [System.Text.RegularExpressions.Regex]::Replace(
        $updated,
        '\]\(\.\./examples/README\.md\)',
        '](Examples)'
    )

    $updated = [System.Text.RegularExpressions.Regex]::Replace(
        $updated,
        '\]\(\.\./examples/([^)]+)\)',
        {
            param($match)
            $examplePath = Normalize-RelativePath $match.Groups[1].Value
            if ($examplePath -ieq "README.md") {
                return "](Examples)"
            }

            return "]($([string](Get-RepositoryBlobUrl "docs/examples/$examplePath")))"
        }
    )

    $updated = [System.Text.RegularExpressions.Regex]::Replace(
        $updated,
        '\]\((?:\./)?([^)\s]+?)\.md(#[^)]+)?\)',
        {
            param($match)
            $pagePath = Normalize-RelativePath $match.Groups[1].Value
            $anchor = $match.Groups[2].Value
            if ($pagePath -ieq "README") {
                $pagePath = "Home"
            }

            return "]($pagePath$anchor)"
        }
    )

    return $updated.TrimEnd() + "`n"
}

function Write-ManagedTextFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath,

        [Parameter(Mandatory = $true)]
        [string]$Content
    )

    $normalizedRelativePath = Normalize-RelativePath $RelativePath
    $absolutePath = Join-Path $script:TargetWikiRoot ($normalizedRelativePath -replace "/", [System.IO.Path]::DirectorySeparatorChar)
    $parentDirectory = Split-Path -Path $absolutePath -Parent
    if (![string]::IsNullOrWhiteSpace($parentDirectory)) {
        New-Item -ItemType Directory -Force -Path $parentDirectory | Out-Null
    }

    Set-Content -Path $absolutePath -Value $Content -Encoding utf8
    $script:ManagedFiles.Add($normalizedRelativePath) | Out-Null
}

function Copy-ManagedBinaryFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourcePath,

        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    $normalizedRelativePath = Normalize-RelativePath $RelativePath
    $absolutePath = Join-Path $script:TargetWikiRoot ($normalizedRelativePath -replace "/", [System.IO.Path]::DirectorySeparatorChar)
    $parentDirectory = Split-Path -Path $absolutePath -Parent
    if (![string]::IsNullOrWhiteSpace($parentDirectory)) {
        New-Item -ItemType Directory -Force -Path $parentDirectory | Out-Null
    }

    Copy-Item -Path $SourcePath -Destination $absolutePath -Force
    $script:ManagedFiles.Add($normalizedRelativePath) | Out-Null
}

if (!(Test-Path -Path $SourceWikiDir -PathType Container)) {
    throw "Source wiki directory not found: $SourceWikiDir"
}

if (!(Test-Path -Path $TargetWikiDir -PathType Container)) {
    New-Item -ItemType Directory -Force -Path $TargetWikiDir | Out-Null
}

$script:RepositorySlug = $RepositorySlug.Trim()
$script:RepositoryRevision = $RepositoryRevision.Trim()
$script:TargetWikiRoot = (Resolve-Path -Path $TargetWikiDir).Path
$script:ManagedFiles = [System.Collections.Generic.List[string]]::new()
$manifestPath = Join-Path $script:TargetWikiRoot ".privateclaw-wiki-sync-manifest"

if (Test-Path -Path $manifestPath -PathType Leaf) {
    foreach ($managedRelativePath in Get-Content -Path $manifestPath) {
        $trimmedPath = $managedRelativePath.Trim()
        if ([string]::IsNullOrWhiteSpace($trimmedPath)) {
            continue
        }

        $absoluteManagedPath = Join-Path $script:TargetWikiRoot ($trimmedPath -replace "/", [System.IO.Path]::DirectorySeparatorChar)
        if (Test-Path -Path $absoluteManagedPath) {
            Remove-Item -Path $absoluteManagedPath -Force -Recurse
        }
    }
}

$sourceWikiRoot = (Resolve-Path -Path $SourceWikiDir).Path
$sourceWikiFiles = @(Get-ChildItem -Path $SourceWikiDir -Recurse -File | Sort-Object FullName)
foreach ($sourceFile in $sourceWikiFiles) {
    $relativePath = Normalize-RelativePath ([System.IO.Path]::GetRelativePath($sourceWikiRoot, $sourceFile.FullName))
    $outputRelativePath = Get-OutputRelativePath $relativePath

    if ($sourceFile.Extension -ieq ".md") {
        $note = "<!-- Auto-synced from $relativePath in the main repository. Do not edit this page in the wiki repo. -->`n`n"
        $content = Convert-MarkdownContent (Get-Content -Path $sourceFile.FullName -Raw)
        Write-ManagedTextFile -RelativePath $outputRelativePath -Content ($note + $content)
        continue
    }

    Copy-ManagedBinaryFile -SourcePath $sourceFile.FullName -RelativePath $outputRelativePath
}

$examplesReadmePath = Join-Path $SourceExamplesDir "README.md"
if (Test-Path -Path $examplesReadmePath -PathType Leaf) {
    $examplesContent = Convert-MarkdownContent (Get-Content -Path $examplesReadmePath -Raw)
    $exampleFiles = @(Get-ChildItem -Path $SourceExamplesDir -Recurse -File |
        Where-Object { $_.Name -ine "README.md" } |
        Sort-Object FullName)

    if ($exampleFiles.Count -gt 0) {
        $examplesContent += "`n## Dateien`n`n"
        foreach ($exampleFile in $exampleFiles) {
            $relativeExamplePath = Normalize-RelativePath (
                [System.IO.Path]::GetRelativePath((Resolve-Path -Path $SourceExamplesDir).Path, $exampleFile.FullName)
            )
            $examplesContent += "- [$relativeExamplePath]($(Get-RepositoryBlobUrl "docs/examples/$relativeExamplePath"))`n"
        }
    }

    $note = "<!-- Auto-synced from docs/examples/README.md in the main repository. Do not edit this page in the wiki repo. -->`n`n"
    Write-ManagedTextFile -RelativePath "Examples.md" -Content ($note + $examplesContent)
}

$sidebarEntries = [System.Collections.Generic.List[string]]::new()
$sidebarEntries.Add("# Navigation") | Out-Null
$sidebarEntries.Add("") | Out-Null
$sidebarEntries.Add("- [Home](Home)") | Out-Null

$topLevelPages = $script:ManagedFiles |
    Where-Object { $_ -like "*.md" -and $_ -notlike "_*.md" } |
    Sort-Object -Unique

foreach ($page in $topLevelPages) {
    if ($page -ieq "Home.md" -or $page -ieq "Examples.md") {
        continue
    }

    if ($page.Contains("/")) {
        continue
    }

    $pageName = [System.IO.Path]::GetFileNameWithoutExtension($page)
    $sidebarEntries.Add("- [$pageName]($pageName)") | Out-Null
}

if ($topLevelPages -contains "Examples.md") {
    $sidebarEntries.Add("- [Examples](Examples)") | Out-Null
}

Write-ManagedTextFile -RelativePath "_Sidebar.md" -Content (($sidebarEntries -join "`n").TrimEnd() + "`n")
Set-Content -Path $manifestPath -Value ($script:ManagedFiles | Sort-Object -Unique) -Encoding utf8

Write-Host "Managed wiki files:"
foreach ($managedFile in ($script:ManagedFiles | Sort-Object -Unique)) {
    Write-Host " - $managedFile"
}
