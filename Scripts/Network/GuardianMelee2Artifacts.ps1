# This helper deletes only a caller-owned child of an explicit, ordinary root.
# Directory links are unlinked without enumerating their targets.
function Assert-GuardianArtifactPath([string]$Path, [string]$AllowedRoot) {
 $root=[IO.Path]::GetFullPath($AllowedRoot).TrimEnd([IO.Path]::DirectorySeparatorChar)
 $full=[IO.Path]::GetFullPath($Path)
 if(!$full.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){
  throw "Artifact path must be strictly inside $root : $full"
 }
 $parent=[IO.Path]::GetDirectoryName($full)
 while($parent){
  $item=Get-Item -LiteralPath $parent -Force -ErrorAction Stop
  if(!$item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){
   throw "Artifact parent is not an ordinary directory: $parent"
  }
  $parent=[IO.Path]::GetDirectoryName($parent)
 }
 return $full
}

function Remove-GuardianProbeArtifact([string]$Path, [string]$AllowedRoot) {
 $full=Assert-GuardianArtifactPath $Path $AllowedRoot
 $stats=@{deletedBytes=0L;deletedFiles=0;unlinkedEntries=0;deletedDirectories=0}
 function Remove-OwnedEntry([string]$EntryPath) {
  $null=Assert-GuardianArtifactPath $EntryPath $AllowedRoot
  $entry=Get-Item -LiteralPath $EntryPath -Force -ErrorAction Stop
  if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){
   if($entry.PSIsContainer){[IO.Directory]::Delete($entry.FullName)}
   else{Remove-Item -LiteralPath $entry.FullName -Force -ErrorAction Stop}
   $stats.unlinkedEntries++
  }elseif($entry.PSIsContainer){
   foreach($child in $entry.GetFileSystemInfos()){Remove-OwnedEntry $child.FullName}
   if($entry.Attributes -band [IO.FileAttributes]::ReadOnly){
    $entry.Attributes=$entry.Attributes -band (-bnot [IO.FileAttributes]::ReadOnly)
   }
   [IO.Directory]::Delete($entry.FullName)
   $stats.deletedDirectories++
  }else{
   $bytes=$entry.Length
   Remove-Item -LiteralPath $entry.FullName -Force -ErrorAction Stop
   $stats.deletedBytes+=$bytes
   $stats.deletedFiles++
  }
 }
 if(Test-Path -LiteralPath $full){Remove-OwnedEntry $full}
 [pscustomobject]$stats
}
