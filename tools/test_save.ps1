# test_save.ps1 - GTi v4.8.0 standalone save-path bench test (no Amiga needed)
# ASCII only - PowerShell 5.1 chokes on UTF-8 dashes without a BOM.
#
# Pokes bytes INSIDE the DISK.ADF file on the GTi's USB disk, exactly like
# FlashFloppy does when a game saves. (A NEW file would land in free clusters
# OUTSIDE the image window and prove nothing.)
#
# Usage:
#   1. GTi in STANDALONE mode, SAVES=COPY on the card. INSERT any game.
#   2. Windows mounts the GTi as a removable drive with DISK.ADF on it.
#   3. powershell -ExecutionPolicy Bypass -File test_save.ps1 -Drive E
#      (replace E with the GTi's drive letter)
#   4. Watch the GTi: ~3s after Windows flushes, the status bar flashes green
#      "SAVED: <game>". Check the SD: GameName.sav.adf beside the master.
#
param([Parameter(Mandatory=$true)][string]$Drive)

$path = "${Drive}:\DISK.ADF"
if (-not (Test-Path $path)) { Write-Error "No DISK.ADF on drive $Drive - INSERT a game first."; exit 1 }

$fs = [System.IO.File]::Open($path, 'Open', 'ReadWrite')
try {
    $marker = [System.Text.Encoding]::ASCII.GetBytes("OMEGAWARE SAVE TEST - THE FLOPPY FLINGER THINGER WOZ ERE")
    # Poke three spots well inside the image: ~100 KB, ~400 KB, ~700 KB
    foreach ($off in 102400, 409600, 716800) {
        $fs.Seek($off, 'Begin') | Out-Null
        $fs.Write($marker, 0, $marker.Length)
        Write-Host ("Poked {0} bytes at offset {1}" -f $marker.Length, $off)
    }
    $fs.Flush()
} finally { $fs.Close() }

Write-Host ""
Write-Host "Done. Windows may cache writes - now EJECT the drive from Explorer"
Write-Host "(or wait ~10s), then watch the GTi for the green SAVED toast."
Write-Host "Verify: GameName.sav.adf on the SD, bytes differ at 102400/409600/716800."
