# Fuction to get datadir name from $Env:DATADIR, default value "Zen" if env is unset

function Get-Datadir {
  if (-not [Environment]::GetEnvironmentVariable('DATADIR')) {
    return "Zen"
  }
  return "$Env:DATADIR"
}

# Function to download and check file

function Start-DownloadCheckFile ($url, $maxAttempts, $filepath, $hash) {
  $dlFilepath = "$filepath" + '.dl'
  if (!(Test-Path -Path "$filepath")) {
    Write-Host "$(Get-Date -Format 'o'): Downloading $url"
    $attemptCount = 0
    Do {
      $Failed = $false
      $attemptCount++
      Try {
        $wc.DownloadFile($url, $dlFilepath)
      } catch {
        $Failed = $true
      }
    } while ($Failed -and ($attemptCount -lt $maxAttempts))
    if (!(Test-Path -Path "$dlFilepath")) {
      throw "$(Get-Date -Format 'o'): Failed to download $url"
    }
    if ($(CertUtil -hashfile $dlFilepath SHA256)[1] -replace " ","" -eq $hash) {
      Rename-Item -Path "$dlFilepath" -NewName "$filepath"
      Write-Host  "$(Get-Date -Format 'o'): $filepath downloaded and checked"
    } else {
      throw "$(Get-Date -Format 'o'): $dlFilepath checksum verification failed"
    }
  } else {
    Write-Host "$(Get-Date -Format 'o'): File $filepath exists"
  }
}

# Define download URLs for file

$Source_sproutprovingkey = "https://downloads.horizen.global/file/TrustedSetup/sprout-proving.key"
$Source_sproutverifyingkey = "https://downloads.horizen.global/file/TrustedSetup/sprout-verifying.key"
$Source_saplingspendparams = "https://downloads.horizen.global/file/TrustedSetup/sapling-spend.params"
$Source_saplingoutputparams = "https://downloads.horizen.global/file/TrustedSetup/sapling-output.params"
$Source_sproutgroth16params = "https://downloads.horizen.global/file/TrustedSetup/sprout-groth16.params"

# Define SHA256 checksums for files

$Checksum_sproutprovingkey = "8bc20a7f013b2b58970cddd2e7ea028975c88ae7ceb9259a5344a16bc2c0eef7"
$Checksum_sproutverifyingkey = "4bd498dae0aacfd8e98dc306338d017d9c08dd0918ead18172bd0aec2fc5df82"
$Checksum_saplingspendparams = "8e48ffd23abb3a5fd9c5589204f32d9c31285a04b78096ba40a79b75677efc13"
$Checksum_saplingoutputparams = "2f0ebbcbb9bb0bcffe95a397e7eba89c29eb4dde6191c339db88570e3f3fb0e4"
$Checksum_sproutgroth16params = "b685d700c60328498fbde589c8c7c484c722b788b265b72af448a5bf0ee55b50"

# Define destination file paths:

$Destination = "$env:APPDATA\ZcashParams\"

$Dest_sproutprovingkey = "$Destination" + 'sprout-proving.key'
$Dest_sproutverifyingkey = "$Destination" + 'sprout-verifying.key'
$Dest_saplingspendparams = "$Destination" + 'sapling-spend.params'
$Dest_saplingoutputparams = "$Destination" + 'sapling-output.params'
$Dest_sproutgroth16params = "$Destination" + 'sprout-groth16.params'

# Initialize WebClient

$wc = New-Object System.Net.WebClient
$wc.Proxy = [System.Net.WebRequest]::DefaultWebProxy
$wc.Proxy.Credentials = [System.Net.CredentialCache]::DefaultNetworkCredentials
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# Set the maximum number of attempts in case download fails

$maxAttempts = 5

# Set datadir

$zenDatadir = Get-Datadir

Write-Host "$(Get-Date -Format 'o'): Download Script Started"

# Create directories and zen.conf

if (!(Test-Path -Path "$env:APPDATA\ZcashParams")) {
    New-Item -ItemType directory -Path "$env:APPDATA\ZcashParams"
}
if (!(Test-Path -Path "$env:APPDATA\$zenDatadir")) {
    New-Item -ItemType directory -Path "$env:APPDATA\$zenDatadir"
}
if (!(Test-Path -Path "$env:APPDATA\$zenDatadir\zen.conf")) {
    New-Item -Path "$env:APPDATA\$zenDatadir\" -Name "zen.conf" -ItemType "file"
    Add-Content -Path "$env:APPDATA\$zenDatadir\zen.conf" -Value rpcuser=zenrpc,rpcpassword=fortytwo
}

# Download files

Start-DownloadCheckFile $Source_sproutprovingkey $maxAttempts $Dest_sproutprovingkey $Checksum_sproutprovingkey
Start-DownloadCheckFile $Source_sproutverifyingkey $maxAttempts $Dest_sproutverifyingkey $Checksum_sproutverifyingkey
Start-DownloadCheckFile $Source_saplingspendparams $maxAttempts $Dest_saplingspendparams $Checksum_saplingspendparams
Start-DownloadCheckFile $Source_saplingoutputparams $maxAttempts $Dest_saplingoutputparams $Checksum_saplingoutputparams
Start-DownloadCheckFile $Source_sproutgroth16params $maxAttempts $Dest_sproutgroth16params $Checksum_sproutgroth16params
