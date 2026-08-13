# 호스트 PC의 실제 LAN IP 감지 (Docker/WSL/VirtualBox 가상 NIC 제외)
$ip = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
  Where-Object {
    $_.IPAddress -notmatch '^127\.' -and
    $_.IPAddress -notmatch '^169\.254\.' -and
    $_.IPAddress -notmatch '^172\.(1[6-9]|2[0-9]|3[0-1])\.' -and
    $_.IPAddress -notmatch '^192\.168\.56\.' -and
    $_.InterfaceAlias -notmatch 'VirtualBox|VMware|WSL|Hyper-V|Loopback|Bluetooth|Docker|vEthernet|TAP|TUN|ZeroTier|Tailscale'
  } |
  Sort-Object @{
    Expression = {
      if ($_.IPAddress -like '192.168.*') { 0 }
      elseif ($_.IPAddress -like '10.*') { 1 }
      else { 2 }
    }
  }, @{
    Expression = {
      if ($_.InterfaceAlias -match 'Wi-Fi|WiFi|WLAN|Wireless|무선') { 0 }
      elseif ($_.InterfaceAlias -match 'Ethernet|이더넷|LAN') { 1 }
      else { 2 }
    }
  } |
  Select-Object -First 1 -ExpandProperty IPAddress

if (-not $ip) {
  Write-Host "LAN IP 감지 실패 — .env 에 SERVER_LAN_IP=192.168.0.xxx 를 직접 입력하세요." -ForegroundColor Yellow
  exit 1
}

$envPath = Join-Path $PSScriptRoot '.env'
"SERVER_LAN_IP=$ip" | Set-Content -Path $envPath -Encoding ASCII
Write-Host "호스트 LAN IP 감지: $ip -> $envPath" -ForegroundColor Green
Write-Host $ip
