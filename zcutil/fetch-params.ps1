Get-Date
"Download Script Started"

# Create directories and zen.conf

if(!(Test-Path -Path "$env:APPDATA\ZcashParams")){
    New-Item -ItemType directory -Path "$env:APPDATA\ZcashParams"
}
if(!(Test-Path -Path "$env:APPDATA\sphereDatadir")){
    New-Item -ItemType directory -Path "$env:APPDATA\sphereDatadir"
}
if(!(Test-Path -Path "$env:APPDATA\sphereDatadir\zen.conf")){
    New-Item -Path "$env:APPDATA\sphereDatadir\" -Name "zen.conf" -ItemType "file"
    Add-Content -Path "$env:APPDATA\sphereDatadir\zen.conf" -Value rpcuser=zenrpc,rpcpassword=fortytwo
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

$wc = New-Object System.Net.WebClient
$wc.Proxy = [System.Net.WebRequest]::DefaultWebProxy
$wc.Proxy.Credentials = [System.Net.CredentialCache]::DefaultNetworkCredentials
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# Set the maximum number of attempts in case download fails

$maxAttempts = 5

# Define destination file paths:

$Destination = "$env:APPDATA\ZcashParams\"

$Dest_sproutprovingkey = "$Destination" + 'sprout-proving.key'
$Dest_sproutverifyingkey = "$Destination" + 'sprout-verifying.key'
$Dest_saplingspendparams = "$Destination" + 'sapling-spend.params'
$Dest_saplingoutputparams = "$Destination" + 'sapling-output.params'
$Dest_sproutgroth16params = "$Destination" + 'sprout-groth16.params'

$DestDl_sproutprovingkey = "$Destination" + 'sprout-proving.key.dl'
$DestDl_sproutverifyingkey = "$Destination" + 'sprout-verifying.key.dl'
$DestDl_saplingspendparams = "$Destination" + 'sapling-spend.params.dl'
$DestDl_saplingoutputparams = "$Destination" + 'sapling-output.params.dl'
$DestDl_sproutgroth16params = "$Destination" + 'sprout-groth16.params.dl'

# Download files

if (!(Test-Path -Path "$Dest_sproutprovingkey")) {
    $attemptCount = 0
Do {
    $attemptCount++
    $wc.DownloadFile($Source_sproutprovingkey, $DestDl_sproutprovingkey)
} while (($(CertUtil -hashfile $DestDl_sproutprovingkey SHA256)[1] -replace " ","" -ne ($Checksum_sproutprovingkey)) -and ($attemptCount -le $maxAttempts))
Rename-Item -Path "$DestDl_sproutprovingkey" -NewName "sprout-proving.key"
}

Get-Date
"sprout-proving.key checked or downloaded"

if (!(Test-Path -Path "$Dest_sproutverifyingkey")) {
    $attemptCount = 0
Do {
    $attemptCount++
    $wc.DownloadFile($Source_sproutverifyingkey, $DestDl_sproutverifyingkey)
} while (($(CertUtil -hashfile $DestDl_sproutverifyingkey SHA256)[1] -replace " ","" -ne ($Checksum_sproutverifyingkey)) -and ($attemptCount -le $maxAttempts))
Rename-Item -Path "$DestDl_sproutverifyingkey" -NewName "sprout-verifying.key"
}

Get-Date
"sprout-verifying.key checked or downloaded"

if (!(Test-Path -Path "$Dest_saplingspendparams")) {
    $attemptCount = 0
Do {
    $attemptCount++
    $wc.DownloadFile($Source_saplingspendparams, $DestDl_saplingspendparams)
} while (($(CertUtil -hashfile $DestDl_saplingspendparams SHA256)[1] -replace " ","" -ne ($Checksum_saplingspendparams)) -and ($attemptCount -le $maxAttempts))
Rename-Item -Path "$DestDl_saplingspendparams" -NewName "sapling-spend.params"
}

Get-Date
"sapling-spend.params checked or downloaded"

if (!(Test-Path -Path "$Dest_saplingoutputparams")) {
    $attemptCount = 0
Do {
    $attemptCount++
    $wc.DownloadFile($Source_saplingoutputparams, $DestDl_saplingoutputparams)
} while (($(CertUtil -hashfile $DestDl_saplingoutputparams SHA256)[1] -replace " ","" -ne ($Checksum_saplingoutputparams)) -and ($attemptCount -le $maxAttempts))
Rename-Item -Path "$DestDl_saplingoutputparams" -NewName "sapling-output.params"
}

Get-Date
"sapling-output.params checked or downloaded"

if (!(Test-Path -Path "$Dest_sproutgroth16params")) {
    $attemptCount = 0
Do {
    $attemptCount++
    $wc.DownloadFile($Source_sproutgroth16params, $DestDl_sproutgroth16params)
} while (($(CertUtil -hashfile $DestDl_sproutgroth16params SHA256)[1] -replace " ","" -ne ($Checksum_sproutgroth16params)) -and ($attemptCount -le $maxAttempts))
Rename-Item -Path "$DestDl_sproutgroth16params" -NewName "sprout-groth16.params"
}

Get-Date
"sprout-groth16.params checked or downloaded"
