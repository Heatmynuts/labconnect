param(
    [string[]]$Ports,
    [string]$Command = "SI",
    [int]$BaudRate = 9600,
    [int]$TimeoutMs = 1800,
    [switch]$Identity
)

$ErrorActionPreference = "Stop"

$expected = @{
    "CDO02" = "A&D MC-30K"
    "CDO03" = "A&D MC-6100"
    "CDO04" = "Mettler XP504"
    "CDO05" = "A&D BA-225"
    "CDO06" = "Mettler XP56"
}

function Convert-ControlChars {
    param([string]$Text)

    if ($null -eq $Text) {
        return ""
    }

    return $Text.Replace("`r", "\r").Replace("`n", "\n")
}

function Get-CdoFromResponse {
    param([string]$Text)

    if ([string]::IsNullOrWhiteSpace($Text)) {
        return ""
    }

    if ($Text -match "CDO\s*0?([2-6])") {
        return "CDO0$($Matches[1])"
    }

    if ($Text -match "ID\s*,\s*([0-9]+)") {
        $digits = $Matches[1]
        if ($digits -match "0*([2-6])$") {
            return "CDO0$($Matches[1])"
        }
    }

    return ""
}

function Read-PortResponse {
    param(
        [System.IO.Ports.SerialPort]$SerialPort,
        [int]$TimeoutMs
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    $chunks = New-Object System.Collections.Generic.List[string]

    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $chunk = $SerialPort.ReadExisting()
            if ($chunk.Length -gt 0) {
                $chunks.Add($chunk)
                if (($chunks -join "") -match "`n") {
                    break
                }
            }
        } catch [TimeoutException] {
        }

        Start-Sleep -Milliseconds 40
    }

    return ($chunks -join "")
}

function Test-CdoPort {
    param(
        [string]$PortName,
        [string]$Command,
        [int]$BaudRate,
        [int]$TimeoutMs,
        [switch]$Identity
    )

    $serial = $null

    try {
        $serial = New-Object System.IO.Ports.SerialPort $PortName, $BaudRate, "None", 8, "One"
        $serial.Handshake = "None"
        $serial.DtrEnable = $false
        $serial.RtsEnable = $false
        $serial.ReadTimeout = 200
        $serial.WriteTimeout = 800
        $serial.NewLine = "`r`n"
        $serial.Open()
        Start-Sleep -Milliseconds 150
        $serial.DiscardInBuffer()
        $serial.DiscardOutBuffer()

        $responses = New-Object System.Collections.Generic.List[string]

        if ($Identity) {
            foreach ($probe in @("I10", "?ID")) {
                $serial.Write("$probe`r`n")
                $responses.Add((Read-PortResponse -SerialPort $serial -TimeoutMs $TimeoutMs))
            }
        } else {
            $serial.Write("$Command`r`n")
            $responses.Add((Read-PortResponse -SerialPort $serial -TimeoutMs $TimeoutMs))
        }

        $raw = ($responses -join "")
        $cdo = Get-CdoFromResponse -Text $raw
        $status = if ($raw.Length -gt 0) { "OK" } else { "Pas de reponse" }
        $balance = if ($expected.ContainsKey($cdo)) { $expected[$cdo] } else { "" }

        [pscustomobject]@{
            Port = $PortName
            Statut = $status
            CDO = $cdo
            Balance = $balance
            Reponse = Convert-ControlChars -Text $raw
        }
    } catch {
        [pscustomobject]@{
            Port = $PortName
            Statut = "Erreur ouverture"
            CDO = ""
            Balance = ""
            Reponse = $_.Exception.Message
        }
    } finally {
        if ($null -ne $serial) {
            if ($serial.IsOpen) {
                $serial.Close()
            }
            $serial.Dispose()
        }
    }
}

if (-not $Ports -or $Ports.Count -eq 0) {
    $Ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object {
        if ($_ -match "^COM([0-9]+)$") { [int]$Matches[1] } else { 9999 }
    }
}

if (-not $Ports -or $Ports.Count -eq 0) {
    Write-Host "Aucun port COM detecte."
    exit 1
}

Write-Host "LabConnect CDO - controle des ports COM"
Write-Host "Parametres serie : $BaudRate bauds, 8N1, sans controle de flux, terminaison CRLF"
if ($Identity) {
    Write-Host "Commandes envoyees : I10 puis ?ID"
} else {
    Write-Host "Commande envoyee : $Command"
}
Write-Host ""

$results = foreach ($port in $Ports) {
    Test-CdoPort -PortName $port -Command $Command -BaudRate $BaudRate -TimeoutMs $TimeoutMs -Identity:$Identity
}

$results | Format-Table -AutoSize
