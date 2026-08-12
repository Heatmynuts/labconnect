param(
    [string[]]$Ports,
    [string]$Command = "SI",
    [int]$BaudRate = 9600,
    [int]$TimeoutMs = 1800,
    [switch]$Identity,
    [switch]$Watch,
    [int]$IntervalSeconds = 2,
    [string]$StatusUrl = "http://192.168.4.1/api/status"
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

function Get-LedState {
    param(
        [string]$Status,
        [string]$Response
    )

    if ($Status -eq "OK") {
        return "Vert bref, puis bleu fixe"
    }

    if ($Status -eq "Pas de reponse") {
        return "Bleu fixe probable, sans reponse balance"
    }

    if ($Response -match "access.*denied|acces.*refuse|refuse|utilise|already.*open|denied") {
        return "Deja ouvert par Optimu ou un autre logiciel"
    }

    return "Blanc clignotant, rouge clignotant, ou non couple"
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

function Get-AtomStatus {
    param([string]$StatusUrl)

    try {
        return Invoke-RestMethod -Uri $StatusUrl -Method Get -TimeoutSec 1
    } catch {
        return $null
    }
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
            "LED attendue" = Get-LedState -Status $status -Response $raw
            CDO = $cdo
            Balance = $balance
            Reponse = Convert-ControlChars -Text $raw
        }
    } catch {
        [pscustomobject]@{
            Port = $PortName
            Statut = "Erreur ouverture"
            "LED attendue" = Get-LedState -Status "Erreur ouverture" -Response $_.Exception.Message
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
if ($Watch) {
    Write-Host "Mode surveillance : Ctrl+C pour arreter"
    Write-Host "Etat LED ATOM lu via : $StatusUrl si le Wi-Fi diagnostic est actif"
}
Write-Host ""

do {
    $atomStatus = $null
    if ($Watch) {
        Clear-Host
        Write-Host "LabConnect CDO - surveillance des ports COM"
        Write-Host "Derniere mise a jour : $(Get-Date -Format 'HH:mm:ss')"
        $atomStatus = Get-AtomStatus -StatusUrl $StatusUrl
        if ($null -ne $atomStatus) {
            Write-Host "ATOM diagnostic : $($atomStatus.id) $($atomStatus.model)"
            Write-Host "LED ATOM       : $($atomStatus.led) - $($atomStatus.ledMeaning)"
            Write-Host "Bluetooth      : $(if ($atomStatus.btClient) { 'PC connecte' } elseif ($atomStatus.btStarted) { 'pret, PC deconnecte' } else { 'non demarre' })"
        } else {
            Write-Host "ATOM diagnostic : indisponible. Active le diagnostic par triple clic et connecte le PC au Wi-Fi de l'ATOM."
            Write-Host "LED attendue    : deduite ci-dessous depuis l'etat COM."
        }
        Write-Host ""
    }

    $results = foreach ($port in $Ports) {
        Test-CdoPort -PortName $port -Command $Command -BaudRate $BaudRate -TimeoutMs $TimeoutMs -Identity:$Identity
    }

    $results | Format-Table -AutoSize

    if ($Watch) {
        Write-Host ""
        Write-Host "Legende firmware : orange identification | rouge clignotant erreur | blanc clignotant PC deconnecte | bleu appairage/connecte | vert trafic"
        Start-Sleep -Seconds $IntervalSeconds
    }
} while ($Watch)
