param(
    [int]$Port = 9000,
    [string]$Namespace = 'demo-versus-rules'
)

# Only the loopback emulator is addressed. This script never contacts Firebase.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Net.Http
$script:client = New-Object System.Net.Http.HttpClient
$script:client.Timeout = [TimeSpan]::FromSeconds(10)
$script:passed = 0

function Encode-Base64Url([string]$Text) {
    return [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Text)).TrimEnd('=').Replace('+', '-').Replace('/', '_')
}
function New-Token([string]$Uid) {
    $seconds = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    $header = Encode-Base64Url '{"alg":"none","typ":"JWT"}'
    $payload = @{
        aud = $Namespace
        iss = "https://securetoken.google.com/$Namespace"
        sub = $Uid
        user_id = $Uid
        iat = $seconds
        exp = $seconds + 3600
        auth_time = $seconds
        firebase = @{ sign_in_provider = 'anonymous'; identities = @{} }
    } | ConvertTo-Json -Compress -Depth 8
    return $header + '.' + (Encode-Base64Url $payload) + '.'
}
function Send-Request {
    param([string]$Method, [string]$Path, $Body = $null, [string]$Uid = '', [switch]$Admin, [string]$Query = '', [string]$ETag = '', [switch]$WantETag)
    $uri = "http://127.0.0.1:$Port/$Path" + '.json?ns=' + [Uri]::EscapeDataString($Namespace)
    if ($Query) { $uri += '&' + $Query }
    if ($Uid) { $uri += '&auth=' + [Uri]::EscapeDataString((New-Token $Uid)) }
    $request = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::new($Method), $uri)
    if ($Admin) { $request.Headers.TryAddWithoutValidation('Authorization', 'Bearer owner') | Out-Null }
    if ($ETag) { $request.Headers.TryAddWithoutValidation('if-match', $ETag) | Out-Null }
    if ($WantETag) { $request.Headers.TryAddWithoutValidation('X-Firebase-ETag', 'true') | Out-Null }
    if ($Method -ne 'GET') {
        $json = if ($null -eq $Body) { 'null' } else { ConvertTo-Json -InputObject $Body -Compress -Depth 100 }
        $request.Content = New-Object System.Net.Http.StringContent($json, [Text.Encoding]::UTF8, 'application/json')
    }
    $response = $script:client.SendAsync($request).GetAwaiter().GetResult()
    $content = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
    $tag = ''
    if ($response.Headers.Contains('ETag')) { $tag = @($response.Headers.GetValues('ETag'))[0] }
    $data = $null
    if ($content) { try { $data = ConvertFrom-Json $content } catch {} }
    $result = @{ Status = [int]$response.StatusCode; Body = $content; Data = $data; ETag = $tag }
    $response.Dispose()
    $request.Dispose()
    return $result
}
function Assert-Status([string]$Name, $Response, [int[]]$Expected = @(200)) {
    if ($Expected -notcontains $Response.Status) {
        throw "$Name : expected $Expected, got $($Response.Status): $($Response.Body)"
    }
    $script:passed++
    Write-Output "PASS $Name"
}
function Assert-True([string]$Name, [bool]$Condition) {
    if (!$Condition) { throw $Name }
    $script:passed++
    Write-Output "PASS $Name"
}
function New-Profile([string]$Uid) {
    return @{ uid = $Uid; name = $Uid; accountId = 10; icon = 1; color1 = 32767; color2 = 16777215; winRate = 0; recentGames = 0 }
}
function Timestamp { return @{ '.sv' = 'timestamp' } }
function New-Room([string]$Uid, [bool]$Private = $false) {
    return @{ name = 'Test Room'; privateRoom = $Private; host = (New-Profile $Uid); hostSeen = (Timestamp); updatedAt = (Timestamp); started = $false; hostReady = $false; guestReady = $false; hostEmoteAt = 0; guestEmoteAt = 0; rules = @{ mode = 0; attempts = 3; targetPercent = 40; sequence = $false; practice = $false } }
}
function New-Launch {
    return @{ id = [Guid]::NewGuid().ToString('N'); requestedAt = (Timestamp); hostLoaded = $false; guestLoaded = $false; releasedAt = 0 }
}
function New-BattlePlayer([bool]$Playing = $true, [bool]$Spectator = $false) {
    return @{ attemptsUsed = 0; bestPercent = 0; currentPercent = 0; runNumber = $(if ($Playing) { 1 } else { 0 }); inAttempt = $Playing; cleared = $false; forfeited = $false; spectating = $Spectator; paused = $false; pausedAt = 0; x = 0; y = 0; cameraX = 0; cameraY = 0; updatedAt = 0 }
}
function New-Battle($Room) {
    $sequence = $Room.rules.sequence -and $Room.rules.mode -eq 0 -and !$Room.rules.practice
    return @{ id = $Room.launch.id; firstUid = $Room.host.uid; activeUid = $Room.host.uid; firstReachedUid = ''; opponentRunAtFirst = 0; winnerUid = ''; draw = $false; finishedAt = 0; host = (New-BattlePlayer); guest = (New-BattlePlayer (!$sequence) $sequence) }
}
function Set-Field($Object, [string]$Field, $Value) {
    if ($Object -is [Collections.IDictionary]) { $Object[$Field] = $Value }
    else { $Object | Add-Member -MemberType NoteProperty -Name $Field -Value $Value -Force }
}
function Remove-Field($Object, [string]$Field) {
    if ($Object -is [Collections.IDictionary]) { $Object.Remove($Field) }
    else { $Object.PSObject.Properties.Remove($Field) }
}
function Get-Room([string]$Id) { return (Send-Request GET "versus-v1/rooms/$Id" -Uid host).Data }
function Put-Room([string]$Id, [string]$Uid, $Body, [string]$ETag = '') {
    Set-Field $Body updatedAt (Timestamp)
    if ($Body.started -and $null -ne $Body.launch -and ($null -eq $Body.battle -or $Body.battle.id -ne $Body.launch.id)) { Set-Field $Body battle (New-Battle $Body) }
    if (!$Body.started) { Remove-Field $Body battle }
    return Send-Request PUT "versus-v1/rooms/$Id" $Body -Uid $Uid -ETag $ETag
}
function Ready-Host([string]$Id) {
    $room = Get-Room $Id
    $room.hostReady = $true
    Assert-Status "$Id host marks own Ready" (Put-Room $Id host $room)
}
function Create-Room([string]$Id, [string]$Pin = '') {
    $secret = @{ hostUid = 'host'; pin = $Pin }
    Assert-Status "$Id secret creation with null_etag" (Send-Request PUT "versus-v1/roomSecrets/$Id" $secret -Uid host -ETag 'null_etag')
    Assert-Status "$Id room creation with null_etag" (Put-Room $Id host (New-Room host ($Pin -ne '')) 'null_etag')
}
function Join-Room([string]$Id, [string]$Uid) {
    $room = Get-Room $Id
    Set-Field $room guest (New-Profile $Uid)
    Set-Field $room guestSeen (Timestamp)
    return Put-Room $Id $Uid $room
}

try {
    $rulesPath = Join-Path $PSScriptRoot '../firebase-rules.json'
    $rules = Get-Content -LiteralPath $rulesPath -Raw | ConvertFrom-Json
    Assert-Status 'rules compile' (Send-Request PUT '.settings/rules' $rules -Admin)
    Assert-Status 'reset isolated emulator namespace' (Send-Request DELETE '' -Admin)

    Assert-Status 'unauthenticated room list denied' (Send-Request GET 'versus-v1/rooms' -Query 'orderBy=%22updatedAt%22&limitToLast=200') @(401, 403)
    Assert-Status 'authenticated unbounded list denied' (Send-Request GET 'versus-v1/rooms' -Uid guest) @(401, 403)
    Assert-Status 'room list over 200 denied' (Send-Request GET 'versus-v1/rooms' -Uid guest -Query 'orderBy=%22updatedAt%22&limitToLast=201') @(401, 403)
    Assert-Status 'authenticated bounded list allowed' (Send-Request GET 'versus-v1/rooms' -Uid guest -Query 'orderBy=%22updatedAt%22&limitToLast=200')
    Assert-Status 'creation without secret reservation denied' (Put-Room unreserved host (New-Room host)) @(401, 403)
    Assert-Status 'invalid password length denied' (Send-Request PUT 'versus-v1/roomSecrets/badpin' @{ hostUid = 'host'; pin = '123' } -Uid host) @(401, 403)

    Assert-Status 'reserve malformed payload test room' (Send-Request PUT 'versus-v1/roomSecrets/invalid' @{ hostUid = 'host'; pin = '' } -Uid host)
    $invalid = New-Room host
    $invalid.host.icon = -1
    Assert-Status 'negative cube icon denied' (Put-Room invalid host $invalid) @(401, 403)
    $invalid = New-Room host
    $invalid.host.icon = 1.5
    Assert-Status 'fractional cube icon denied' (Put-Room invalid host $invalid) @(401, 403)
    $invalid = New-Room host
    $invalid.host.color1 = 16777216
    Assert-Status 'invalid RGB value denied' (Put-Room invalid host $invalid) @(401, 403)
    $invalid = New-Room host
    $invalid.host.winRate = 101
    Assert-Status 'invalid percentage denied' (Put-Room invalid host $invalid) @(401, 403)
    $invalid = New-Room host
    $invalid.host.recentGames = 11
    Assert-Status 'recent history count above ten denied' (Put-Room invalid host $invalid) @(401, 403)
    $invalid = New-Room host
    $invalid.name = '   '
    Assert-Status 'blank room name denied' (Put-Room invalid host $invalid) @(401, 403)
    $invalid = New-Room host
    $invalid.name = 'x' * 49
    Assert-Status 'oversized room name denied' (Put-Room invalid host $invalid) @(401, 403)
    $invalid = New-Room host
    $invalid.host.name = 'x' * 33
    Assert-Status 'oversized player name denied' (Put-Room invalid host $invalid) @(401, 403)
    $invalid = New-Room host
    $invalid.host.accountId = 1.5
    Assert-Status 'fractional account id denied' (Put-Room invalid host $invalid) @(401, 403)

    Create-Room public
    Assert-Status 'anonymous JWT owner room read allowed' (Send-Request GET 'versus-v1/rooms/public' -Uid host)
    Assert-Status 'unauthenticated room read denied' (Send-Request GET 'versus-v1/rooms/public') @(401, 403)
    Assert-Status 'cross-user secret read denied' (Send-Request GET 'versus-v1/roomSecrets/public' -Uid guest) @(401, 403)
    Assert-Status 'secret collection read denied' (Send-Request GET 'versus-v1/roomSecrets' -Uid host) @(401, 403)
    Assert-Status 'aggregate root read denied' (Send-Request GET 'versus-v1' -Uid host) @(401, 403)
    Assert-Status 'stranger cannot delete room' (Send-Request DELETE 'versus-v1/rooms/public' -Uid third) @(401, 403)
    Assert-Status 'host cannot delete secret of open room' (Send-Request DELETE 'versus-v1/roomSecrets/public' -Uid host) @(401, 403)

    $room = Get-Room public
    Set-Field $room guest (New-Profile guest)
    Set-Field $room guestSeen (Timestamp)
    Assert-Status 'host cannot invent a guest' (Put-Room public host $room) @(401, 403)
    Assert-Status 'second seat admitted' (Join-Room public guest)
    Assert-Status 'third seat cannot replace guest' (Join-Room public third) @(401, 403)
    $room = Get-Room public
    $room.guestReady = $true
    Assert-Status 'guest cannot ready without selected map' (Put-Room public guest $room) @(401, 403)

    $guestRetry = Send-Request GET 'versus-v1/rooms/public' -Uid guest -WantETag
    $guestRetry.Data.guestSeen = Timestamp
    Assert-Status 'guest retry refreshes own seat idempotently' (Put-Room public guest $guestRetry.Data $guestRetry.ETag)
    $hostHeartbeat = Send-Request GET 'versus-v1/rooms/public' -Uid host -WantETag
    $hostHeartbeat.Data.hostSeen = Timestamp
    Assert-Status 'host heartbeat preserves guest seat' (Put-Room public host $hostHeartbeat.Data $hostHeartbeat.ETag)

    $room = Get-Room public
    $room.host.name = 'forged'
    Assert-Status 'guest cannot alter host profile' (Put-Room public guest $room) @(401, 403)
    $room = Get-Room public
    $room.host.uid = 'guest'
    Assert-Status 'guest cannot become host' (Put-Room public guest $room) @(401, 403)
    $room = Get-Room public
    $room.hostSeen = Timestamp
    Assert-Status 'guest cannot keep absent host alive' (Put-Room public guest $room) @(401, 403)
    $room = Get-Room public
    $room.privateRoom = $true
    Assert-Status 'guest cannot alter privacy' (Put-Room public guest $room) @(401, 403)
    $room = Get-Room public
    Set-Field $room extraGuest (New-Profile third)
    Assert-Status 'unknown third seat field denied' (Put-Room public guest $room) @(401, 403)
    $room = Get-Room public
    Remove-Field $room guest
    Remove-Field $room guestSeen
    Assert-Status 'host cannot remove fresh guest' (Put-Room public host $room) @(401, 403)

    $map = @{ id = 128; name = 'Test Level'; difficulty = -1; stars = 0; demon = $false; autoLevel = $false }
    $room = Get-Room public
    Set-Field $room level $map
    Assert-Status 'guest cannot select map' (Put-Room public guest $room) @(401, 403)
    Assert-Status 'host can select unrated map' (Put-Room public host $room)
    $room = Get-Room public
    $room.level.difficulty = 10
    $room.level.stars = 10
    $room.level.demon = $true
    Assert-Status 'host can select extreme demon' (Put-Room public host $room)
    $room = Get-Room public
    Remove-Field $room level
    Assert-Status 'guest cannot delete selected map' (Put-Room public guest $room) @(401, 403)
    $room = Get-Room public
    $room.level.difficulty = 11
    Assert-Status 'invalid difficulty denied' (Put-Room public host $room) @(401, 403)
    Ready-Host public
    $room = Get-Room public
    $room.started = $true
    Set-Field $room launch (New-Launch)
    Assert-Status 'host cannot start before guest Ready' (Put-Room public host $room) @(401, 403)
    $room = Get-Room public
    $room.guestReady = $true
    Assert-Status 'host cannot forge guest Ready' (Put-Room public host $room) @(401, 403)
    Assert-Status 'guest can mark Ready with selected map' (Put-Room public guest $room)
    $room = Get-Room public
    $room.guestReady = $false
    Assert-Status 'host cannot silently reset Ready without map change' (Put-Room public host $room) @(401, 403)
    Assert-Status 'guest may unready before start' (Put-Room public guest $room)
    $room.guestReady = $true
    Assert-Status 'guest may ready again' (Put-Room public guest $room)
    Ready-Host public
    $room = Get-Room public
    $room.started = $true
    Set-Field $room launch (New-Launch)
    Assert-Status 'ready guest still cannot start match' (Put-Room public guest $room) @(401, 403)
    Assert-Status 'host can start ready match' (Put-Room public host $room)
    $guestRetry = Send-Request GET 'versus-v1/rooms/public' -Uid guest -WantETag
    $guestRetry.Data.guestSeen = Timestamp
    Assert-Status 'already seated guest retries after start' (Put-Room public guest $guestRetry.Data $guestRetry.ETag)
    $room = Get-Room public
    $room.level.id = 999
    Assert-Status 'map frozen once started' (Put-Room public host $room) @(401, 403)
    $room = Get-Room public
    $room.started = $false
    $room.hostReady = $false
    Assert-Status 'partial cancellation retaining launch denied' (Put-Room public guest $room) @(401, 403)
    Remove-Field $room guest
    Remove-Field $room guestSeen
    Remove-Field $room launch
    $room.guestReady = $false
    Assert-Status 'guest can leave started room' (Put-Room public guest $room)
    Assert-True 'leaving frees second seat' ($null -eq (Get-Room public).guest)
    Assert-Status 'new guest joins freed seat' (Join-Room public third)

    $room = Get-Room public
    $room.guestReady = $true
    Assert-Status 'replacement guest may ready' (Put-Room public third $room)

    $room = Get-Room public
    $room.guestSeen = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - 60000
    Assert-Status 'seed stale guest' (Send-Request PUT 'versus-v1/rooms/public' $room -Admin)
    Ready-Host public
    $room = Get-Room public
    $room.started = $true
    Set-Field $room launch (New-Launch)
    Assert-Status 'host cannot start with stale guest' (Put-Room public host $room) @(401, 403)
    $room = Get-Room public
    Remove-Field $room guest
    Remove-Field $room guestSeen
    $room.guestReady = $false
    Remove-Field $room launch
    $room.hostSeen = Timestamp
    Assert-Status 'host can remove expired guest' (Put-Room public host $room)

    Create-Room launch
    Assert-Status 'launch guest joins' (Join-Room launch guest)
    $room = Get-Room launch
    Set-Field $room level @{ id = 128; name = 'Launch Level'; difficulty = 2; stars = 3; demon = $false; autoLevel = $false }
    Assert-Status 'launch map selected' (Put-Room launch host $room)
    $room = Get-Room launch
    $room.guestReady = $true
    Assert-Status 'launch guest ready' (Put-Room launch guest $room)

    $room = Get-Room launch
    $room.level.id = 129
    Assert-Status 'map change must clear guest readiness' (Put-Room launch host $room) @(401, 403)
    $room.guestReady = $false
    Assert-Status 'host map change resets guest readiness' (Put-Room launch host $room)
    $room = Get-Room launch
    $room.guestReady = $true
    Assert-Status 'guest ready after map change' (Put-Room launch guest $room)

    Ready-Host launch
    $room = Get-Room launch
    $room.started = $true
    Set-Field $room launch (New-Launch)
    $room.level.id = 130
    Assert-Status 'host cannot change map and start atomically' (Put-Room launch host $room) @(401, 403)
    $room.level.id = 129
    $room.launch.hostLoaded = $true
    Assert-Status 'launch must begin unloaded' (Put-Room launch host $room) @(401, 403)
    $room.launch.hostLoaded = $false
    $room.launch.id = 'not-a-launch-id'
    Assert-Status 'launch identifier format validated' (Put-Room launch host $room) @(401, 403)
    $room.launch.id = [Guid]::NewGuid().ToString('N')
    $room.launch.requestedAt = 1
    Assert-Status 'launch requires current server timestamp' (Put-Room launch host $room) @(401, 403)
    $room.launch.requestedAt = Timestamp
    Assert-Status 'host opens synchronized launch' (Put-Room launch host $room)

    $room = Get-Room launch
    $room.launch.releasedAt = Timestamp
    Assert-Status 'release denied before either player loaded' (Put-Room launch host $room) @(401, 403)
    $room = Get-Room launch
    $room.launch.guestLoaded = $true
    Assert-Status 'host cannot forge guest loaded flag' (Put-Room launch host $room) @(401, 403)
    $room = Get-Room launch
    $room.launch.hostLoaded = $true
    Assert-Status 'guest cannot forge host loaded flag' (Put-Room launch guest $room) @(401, 403)
    $room = Get-Room launch
    $room.launch.id = [Guid]::NewGuid().ToString('N')
    Assert-Status 'active launch identifier immutable' (Put-Room launch host $room) @(401, 403)
    $room = Get-Room launch
    $room.launch.requestedAt = Timestamp
    Assert-Status 'active launch request time immutable' (Put-Room launch host $room) @(401, 403)
    $room = Get-Room launch
    $room.guestReady = $false
    Assert-Status 'guest cannot unready without cancelling active launch' (Put-Room launch guest $room) @(401, 403)

    $hostLoad = Send-Request GET 'versus-v1/rooms/launch' -Uid host -WantETag
    $guestLoad = Send-Request GET 'versus-v1/rooms/launch' -Uid guest -WantETag
    Assert-True 'both load acknowledgments begin with same ETag' ($hostLoad.ETag -eq $guestLoad.ETag)
    $hostLoad.Data.launch.hostLoaded = $true
    $hostLoad.Data.hostSeen = Timestamp
    Assert-Status 'host acknowledges own loaded state' (Put-Room launch host $hostLoad.Data $hostLoad.ETag)
    $guestLoad.Data.launch.guestLoaded = $true
    $guestLoad.Data.guestSeen = Timestamp
    Assert-Status 'simultaneous guest ack retries after host CAS' (Put-Room launch guest $guestLoad.Data $guestLoad.ETag) @(412)
    $room = Get-Room launch
    $room.launch.hostLoaded = $false
    Assert-Status 'loaded acknowledgment cannot be reset' (Put-Room launch host $room) @(401, 403)
    $room = Get-Room launch
    $room.launch.releasedAt = Timestamp
    Assert-Status 'one loaded player cannot release countdown' (Put-Room launch host $room) @(401, 403)

    $guestLoad = Send-Request GET 'versus-v1/rooms/launch' -Uid guest -WantETag
    $guestLoad.Data.launch.guestLoaded = $true
    $guestLoad.Data.launch.releasedAt = Timestamp
    $guestLoad.Data.guestSeen = Timestamp
    Assert-Status 'second loaded ack releases shared countdown' (Put-Room launch guest $guestLoad.Data $guestLoad.ETag)
    $released = Get-Room launch
    Assert-True 'server records positive common release time' ($released.launch.releasedAt -gt 0 -and $released.launch.hostLoaded -and $released.launch.guestLoaded)
    $room = Get-Room launch
    $room.launch.releasedAt = Timestamp
    Assert-Status 'shared release time cannot be moved' (Put-Room launch host $room) @(401, 403)
    $room = Get-Room launch
    $room.launch.releasedAt = 0
    Assert-Status 'shared release time cannot be reset' (Put-Room launch guest $room) @(401, 403)
    $room = Get-Room launch
    Assert-Status 'already acknowledged player may retry idempotently' (Put-Room launch host $room)

    $room = Get-Room launch
    $room.started = $false
    $room.hostReady = $false
    $room.guestReady = $false
    Remove-Field $room launch
    Assert-Status 'guest quit may cancel after gameplay release' (Put-Room launch guest $room)
    $cancelled = Get-Room launch
    Assert-True 'cancellation clears launch and readiness together' (!$cancelled.started -and !$cancelled.guestReady -and $null -eq $cancelled.launch)
    Ready-Host launch
    $room = Get-Room launch
    $room.started = $true
    Set-Field $room launch (New-Launch)
    Assert-Status 'cancelled session cannot restart without Ready' (Put-Room launch host $room) @(401, 403)
    $room = Get-Room launch
    $room.guestReady = $true
    Assert-Status 'guest can ready for next launch' (Put-Room launch guest $room)
    Ready-Host launch
    $room = Get-Room launch
    $room.started = $true
    Set-Field $room launch (New-Launch)
    Assert-Status 'host starts distinct next launch' (Put-Room launch host $room)
    $currentLaunch = Get-Room launch
    Assert-True 'subsequent launch uses different identity' ($currentLaunch.launch.id -ne $released.launch.id)
    Assert-Status 'old launch snapshot cannot acknowledge new session' (Put-Room launch guest $released) @(401, 403)

    $room = Get-Room launch
    $room.launch.requestedAt = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - 61000
    Assert-Status 'seed launch exceeding sixty second timeout' (Send-Request PUT 'versus-v1/rooms/launch' $room -Admin)
    $room = Get-Room launch
    $room.launch.hostLoaded = $true
    Assert-Status 'host load acknowledgment after timeout denied' (Put-Room launch host $room) @(401, 403)
    $room = Get-Room launch
    $room.launch.guestLoaded = $true
    Assert-Status 'guest load acknowledgment after timeout denied' (Put-Room launch guest $room) @(401, 403)
    $room = Get-Room launch
    $room.launch.hostLoaded = $true
    $room.launch.guestLoaded = $true
    Assert-Status 'seed both loaded after elapsed launch window' (Send-Request PUT 'versus-v1/rooms/launch' $room -Admin)
    $room = Get-Room launch
    $room.launch.releasedAt = Timestamp
    Assert-Status 'countdown release after timeout denied' (Put-Room launch host $room) @(401, 403)
    $room = Get-Room launch
    $room.started = $false
    $room.hostReady = $false
    $room.guestReady = $false
    Remove-Field $room launch
    Assert-Status 'host can cancel timed-out launch' (Put-Room launch host $room)
    $room = Get-Room launch
    $room.guestReady = $true
    Assert-Status 'guest can ready following timeout cancellation' (Put-Room launch guest $room)
    Ready-Host launch
    $room = Get-Room launch
    $room.started = $true
    Set-Field $room launch (New-Launch)
    Assert-Status 'host restarts after timeout cancellation' (Put-Room launch host $room)
    $room = Get-Room launch
    $room.started = $false
    $room.hostReady = $false
    $room.guestReady = $false
    Remove-Field $room launch
    Assert-Status 'guest can cancel loading failure before release' (Put-Room launch guest $room)

    $room = Get-Room launch
    $room.guestReady = $true
    Assert-Status 'guest ready for guest-first loading order' (Put-Room launch guest $room)
    Ready-Host launch
    $room = Get-Room launch
    $room.started = $true
    Set-Field $room launch (New-Launch)
    Assert-Status 'host opens guest-first launch' (Put-Room launch host $room)
    $room = Get-Room launch
    $room.launch.guestLoaded = $true
    Assert-Status 'guest may finish loading first' (Put-Room launch guest $room)
    $room = Get-Room launch
    $room.launch.hostLoaded = $true
    $room.launch.releasedAt = Timestamp
    Assert-Status 'host may acknowledge second and release' (Put-Room launch host $room)
    $room = Get-Room launch
    $room.started = $false
    $room.hostReady = $false
    $room.guestReady = $false
    Remove-Field $room launch
    Assert-Status 'host quit may cancel after gameplay release' (Put-Room launch host $room)

    $room = Get-Room launch
    $room.guestReady = $true
    Assert-Status 'guest ready before stale cleanup test' (Put-Room launch guest $room)
    Ready-Host launch
    $room = Get-Room launch
    $room.started = $true
    Set-Field $room launch (New-Launch)
    Assert-Status 'start before stale cleanup test' (Put-Room launch host $room)
    $room = Get-Room launch
    $room.guestSeen = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - 60000
    Assert-Status 'seed guest disconnection during loading' (Send-Request PUT 'versus-v1/rooms/launch' $room -Admin)
    $room = Get-Room launch
    $room.started = $false
    $room.hostReady = $false
    $room.guestReady = $false
    Remove-Field $room launch
    Remove-Field $room guest
    Remove-Field $room guestSeen
    $room.hostSeen = Timestamp
    Assert-Status 'host stale cleanup clears seat Ready and launch' (Put-Room launch host $room)

    Create-Room private '0042'
    Assert-Status 'private room entry denied without proof' (Join-Room private guest) @(401, 403)
    Assert-Status 'wrong PIN proof denied' (Send-Request PUT 'versus-v1/admissions/private/guest' @{ pin = '4242' } -Uid guest) @(401, 403)
    Assert-Status 'proof cannot be forged for another user' (Send-Request PUT 'versus-v1/admissions/private/third' @{ pin = '0042' } -Uid guest) @(401, 403)
    Assert-Status 'correct four-digit PIN proof accepted' (Send-Request PUT 'versus-v1/admissions/private/guest' @{ pin = '0042' } -Uid guest)
    Assert-Status 'admission proof cannot be read even by submitter' (Send-Request GET 'versus-v1/admissions/private/guest' -Uid guest) @(401, 403)
    Assert-Status 'private room entry allowed after proof' (Join-Room private guest)
    $list = Send-Request GET 'versus-v1/rooms' -Uid guest -Query 'orderBy=%22updatedAt%22&limitToLast=200'
    Assert-Status 'public listing contains private room' $list
    Assert-True 'PIN absent from public listing' (!$list.Body.Contains('0042') -and !$list.Body.Contains('"pin"'))
    Assert-Status 'secret cannot be overwritten' (Send-Request PUT 'versus-v1/roomSecrets/private' @{ hostUid = 'host'; pin = '9999' } -Uid host) @(401, 403)

    Create-Room race
    $a = Send-Request GET 'versus-v1/rooms/race' -Uid guest -WantETag
    $b = Send-Request GET 'versus-v1/rooms/race' -Uid third -WantETag
    Assert-True 'concurrent reads share ETag' ($a.ETag -and $a.ETag -eq $b.ETag)
    Set-Field $a.Data guest (New-Profile guest)
    Set-Field $a.Data guestSeen (Timestamp)
    Assert-Status 'first compare-and-swap claims seat' (Put-Room race guest $a.Data $a.ETag)
    Set-Field $b.Data guest (New-Profile third)
    Set-Field $b.Data guestSeen (Timestamp)
    Assert-Status 'stale concurrent claim cannot replace occupant' (Put-Room race third $b.Data $b.ETag) @(412)
    $stale = Send-Request GET 'versus-v1/rooms/race' -Uid guest -WantETag
    Assert-Status 'host room deletion allowed' (Send-Request DELETE 'versus-v1/rooms/race' -Uid host)
    $stale.Data.guestSeen = Timestamp
    Assert-Status 'stale guest heartbeat cannot resurrect deleted room' (Put-Room race guest $stale.Data $stale.ETag) @(412)
    Assert-True 'guest observes deleted room as null' ($null -eq (Send-Request GET 'versus-v1/rooms/race' -Uid guest).Data)

    Create-Room stalehost
    $room = Get-Room stalehost
    $room.hostSeen = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - 60000
    Assert-Status 'seed stale host' (Send-Request PUT 'versus-v1/rooms/stalehost' $room -Admin)
    Assert-Status 'join stale host denied server-side' (Join-Room stalehost guest) @(401, 403)

    # New room rules, emotes, battle progress, spectator streaming, and derived history.
    Create-Room battlecase
    Assert-Status 'battle guest joins' (Join-Room battlecase guest)
    $room = Get-Room battlecase
    Set-Field $room level @{ id = 12345; name = 'Battle Map'; difficulty = 4; stars = 7; demon = $false; autoLevel = $false }
    $room.rules.attempts = 1
    Assert-Status 'host configures attempt rule' (Put-Room battlecase host $room)
    $room = Get-Room battlecase
    $room.rules.targetPercent = 75
    Assert-Status 'guest cannot change game rules' (Put-Room battlecase guest $room) @(401, 403)

    $room = Get-Room battlecase
    Set-Field $room emote @{ uid = 'host'; kind = 'like'; at = (Timestamp); nonce = ([Guid]::NewGuid().ToString('N')) }
    $room.hostEmoteAt = Timestamp
    Assert-Status 'host sends allowed room emote' (Put-Room battlecase host $room)
    $room = Get-Room battlecase
    Set-Field $room emote @{ uid = 'host'; kind = 'fire'; at = (Timestamp); nonce = ([Guid]::NewGuid().ToString('N')) }
    $room.hostEmoteAt = Timestamp
    Assert-Status 'one-second emote flood is denied' (Put-Room battlecase host $room) @(401, 403)
    $room = Get-Room battlecase
    Set-Field $room emote @{ uid = 'host'; kind = 'angry'; at = (Timestamp); nonce = ([Guid]::NewGuid().ToString('N')) }
    $room.guestEmoteAt = Timestamp
    Assert-Status 'guest cannot spoof host emote' (Put-Room battlecase guest $room) @(401, 403)

    Ready-Host battlecase
    $room = Get-Room battlecase
    $room.guestReady = $true
    Assert-Status 'battle guest marks own Ready' (Put-Room battlecase guest $room)
    $room = Get-Room battlecase
    Set-Field $room launch (New-Launch)
    $room.started = $true
    Set-Field $room battle (New-Battle $room)
    Assert-Status 'host creates validated battle state' (Put-Room battlecase host $room)
    $room = Get-Room battlecase
    $room.rules.attempts = 2
    Assert-Status 'game rules are frozen after start' (Put-Room battlecase host $room) @(401, 403)

    $room = Get-Room battlecase
    $room.launch.hostLoaded = $true
    $room.launch.guestLoaded = $true
    $room.launch.releasedAt = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - 5000
    Assert-Status 'seed released battle for progress checks' (Send-Request PUT 'versus-v1/rooms/battlecase' $room -Admin)

    $room = Get-Room battlecase
    $room.battle.guest.x = 12
    $room.battle.guest.updatedAt = Timestamp
    $room.guestSeen = Timestamp
    Assert-Status 'position traffic denied before anyone spectates' (Put-Room battlecase guest $room) @(401, 403)
    $room = Get-Room battlecase
    $room.battle.host.attemptsUsed = 1
    $room.battle.host.bestPercent = 60
    $room.battle.host.currentPercent = 60
    $room.battle.host.inAttempt = $false
    $room.battle.host.spectating = $true
    $room.battle.host.updatedAt = Timestamp
    $room.hostSeen = Timestamp
    Assert-Status 'exhausted host enters spectator state' (Put-Room battlecase host $room)
    $room = Get-Room battlecase
    Set-Field $room.battle hostReturned $true
    Assert-Status 'cannot return acknowledgement while opponent still playing' (Put-Room battlecase host $room) @(401,403)
    $room = Get-Room battlecase
    $room.battle.guest.x = 120
    $room.battle.guest.y = 45
    $room.battle.guest.cameraX = 90
    $room.battle.guest.cameraY = 20
    $room.battle.guest.updatedAt = Timestamp
    $room.guestSeen = Timestamp
    Assert-Status 'active player streams position only to spectator' (Put-Room battlecase guest $room)
    $room = Get-Room battlecase
    $room.battle.host.bestPercent = 99
    $room.battle.guest.updatedAt = Timestamp
    $room.guestSeen = Timestamp
    Assert-Status 'guest cannot alter host battle progress' (Put-Room battlecase guest $room) @(401, 403)
    $room = Get-Room battlecase
    $room.battle.guest.attemptsUsed = 1
    $room.battle.guest.bestPercent = 50
    $room.battle.guest.currentPercent = 50
    $room.battle.guest.inAttempt = $false
    $room.battle.guest.spectating = $true
    $room.battle.guest.updatedAt = Timestamp
    $room.guestSeen = Timestamp
    $room.battle.winnerUid = 'host'
    $room.battle.finishedAt = Timestamp
    Assert-Status 'derived farther-progress winner accepted' (Put-Room battlecase guest $room)
    $matchID = (Get-Room battlecase).battle.id
    $badHistory = @{ roomId = 'battlecase'; opponentName = 'guest'; levelName = 'Battle Map'; winnerName = 'guest'; result = 'loss'; playedAt = (Timestamp) }
    Assert-Status 'forged battle history result denied' (Send-Request PUT "versus-v1/history/host/$matchID" $badHistory -Uid host) @(401, 403)
    $goodHistory = @{ roomId = 'battlecase'; opponentName = 'guest'; levelName = 'Battle Map'; winnerName = 'host'; result = 'win'; playedAt = (Timestamp) }
    Assert-Status 'participant records server-derived history' (Send-Request PUT "versus-v1/history/host/$matchID" $goodHistory -Uid host)

    Create-Room earlycase
    Assert-Status 'early attempt guest joins' (Join-Room earlycase guest)
    $room = Get-Room earlycase
    Set-Field $room level @{ id = 22456; name = 'Early Map'; difficulty = 5; stars = 8; demon = $false; autoLevel = $false }
    Assert-Status 'early attempt host selects map' (Put-Room earlycase host $room)
    Ready-Host earlycase
    $room = Get-Room earlycase
    $room.guestReady = $true
    Assert-Status 'early attempt guest ready' (Put-Room earlycase guest $room)
    $room = Get-Room earlycase
    Set-Field $room launch (New-Launch)
    $room.started = $true
    Set-Field $room battle (New-Battle $room)
    Assert-Status 'early attempt battle starts' (Put-Room earlycase host $room)
    $room = Get-Room earlycase
    $room.launch.hostLoaded = $true
    $room.launch.guestLoaded = $true
    $room.launch.releasedAt = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - 5000
    Assert-Status 'seed early attempt release' (Send-Request PUT 'versus-v1/rooms/earlycase' $room -Admin)
    $room = Get-Room earlycase
    $room.battle.guest.attemptsUsed = 2
    $room.battle.guest.runNumber = 2
    $room.battle.guest.bestPercent = 45
    $room.battle.guest.currentPercent = 45
    $room.battle.guest.inAttempt = $false
    $room.battle.guest.updatedAt = Timestamp
    $room.guestSeen = Timestamp
    Assert-Status 'guest has one remaining attempt' (Put-Room earlycase guest $room)
    $room = Get-Room earlycase
    $room.battle.host.attemptsUsed = 1
    $room.battle.host.runNumber = 2
    $room.battle.host.bestPercent = 60
    $room.battle.host.currentPercent = 60
    $room.battle.host.updatedAt = Timestamp
    $room.hostSeen = Timestamp
    $room.battle.winnerUid = 'host'
    $room.battle.finishedAt = Timestamp
    Assert-Status 'early win denied with opponent attempt remaining' (Put-Room earlycase host $room) @(401, 403)
    $room = Get-Room earlycase
    $room.battle.guest.attemptsUsed = 3
    $room.battle.guest.runNumber = 3
    $room.battle.guest.bestPercent = 45
    $room.battle.guest.currentPercent = 45
    $room.battle.guest.inAttempt = $false
    $room.battle.guest.spectating = $true
    $room.battle.guest.updatedAt = Timestamp
    $room.guestSeen = Timestamp
    Assert-Status 'guest exhausts attempts' (Put-Room earlycase guest $room)
    $room = Get-Room earlycase
    $room.battle.host.attemptsUsed = 1
    $room.battle.host.runNumber = 2
    $room.battle.host.bestPercent = 60
    $room.battle.host.currentPercent = 60
    $room.battle.host.updatedAt = Timestamp
    $room.hostSeen = Timestamp
    $room.battle.winnerUid = 'host'
    $room.battle.finishedAt = Timestamp
    Assert-Status 'higher progress with fewer attempts wins early' (Put-Room earlycase host $room)

    Create-Room percentcase
    Assert-Status 'percent guest joins' (Join-Room percentcase guest)
    $room = Get-Room percentcase
    Set-Field $room level @{ id = 23456; name = 'Percent Map'; difficulty = 5; stars = 8; demon = $false; autoLevel = $false }
    $room.rules.mode = 1
    $room.rules.targetPercent = 40
    Assert-Status 'host configures percent rule' (Put-Room percentcase host $room)
    $room = Get-Room percentcase
    $room.rules.sequence = $true
    Assert-Status 'sequence cannot be enabled in percent mode' (Put-Room percentcase host $room) @(401, 403)
    Ready-Host percentcase
    $room = Get-Room percentcase
    $room.guestReady = $true
    Assert-Status 'percent guest marks Ready' (Put-Room percentcase guest $room)
    $room = Get-Room percentcase
    Set-Field $room launch (New-Launch)
    $room.started = $true
    Set-Field $room battle (New-Battle $room)
    Assert-Status 'host creates percent battle' (Put-Room percentcase host $room)
    $room = Get-Room percentcase
    $room.launch.hostLoaded = $true
    $room.launch.guestLoaded = $true
    $room.launch.releasedAt = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - 5000
    Assert-Status 'seed released percent battle' (Send-Request PUT 'versus-v1/rooms/percentcase' $room -Admin)
    $room = Get-Room percentcase
    $room.battle.host.bestPercent = 40
    $room.battle.host.currentPercent = 40
    $room.battle.host.inAttempt = $false
    $room.battle.host.spectating = $true
    $room.battle.host.updatedAt = Timestamp
    $room.battle.firstReachedUid = 'host'
    $room.battle.opponentRunAtFirst = 1
    $room.hostSeen = Timestamp
    Assert-Status 'first percent target waits for opponent current run' (Put-Room percentcase host $room)
    $room = Get-Room percentcase
    $room.battle.guest.x = 20
    $room.battle.guest.updatedAt = Timestamp
    $room.guestSeen = Timestamp
    Assert-Status 'percent mode never permits position streaming' (Put-Room percentcase guest $room) @(401, 403)
    $room = Get-Room percentcase
    $room.battle.winnerUid = 'host'
    $room.battle.finishedAt = Timestamp
    $room.guestSeen = Timestamp
    Assert-Status 'percent winner denied while tie attempt is active' (Put-Room percentcase guest $room) @(401, 403)
    $room = Get-Room percentcase
    $room.battle.guest.bestPercent = 40
    $room.battle.guest.currentPercent = 40
    $room.battle.guest.inAttempt = $false
    $room.battle.guest.spectating = $true
    $room.battle.guest.updatedAt = Timestamp
    $room.battle.draw = $true
    $room.battle.finishedAt = Timestamp
    $room.guestSeen = Timestamp
    Assert-Status 'same-run percent target produces draw' (Put-Room percentcase guest $room)

    Create-Room sequencecase
    Assert-Status 'legacy sequence room guest joins' (Join-Room sequencecase guest)
    $room = Get-Room sequencecase
    $room.rules.sequence = $true
    Assert-Status 'sequence mode cannot be enabled' (Put-Room sequencecase host $room) @(401, 403)
    Assert-Status 'seed preexisting sequence room' (Send-Request PUT 'versus-v1/rooms/sequencecase' $room -Admin)
    $room = Get-Room sequencecase
    $room.rules.sequence = $false
    Assert-Status 'guest cannot disable legacy sequence rule' (Put-Room sequencecase guest $room) @(401, 403)
    $room = Get-Room sequencecase
    $room.rules.sequence = $false
    Assert-Status 'host can disable legacy sequence rule' (Put-Room sequencecase host $room)
    $room = Get-Room sequencecase
    Set-Field $room level @{ id = 34567; name = 'Normal Map'; difficulty = 6; stars = 9; demon = $false; autoLevel = $false }
    Assert-Status 'normal match map selected' (Put-Room sequencecase host $room)
    Ready-Host sequencecase
    $room = Get-Room sequencecase
    $room.guestReady = $true
    Assert-Status 'normal match guest marks Ready' (Put-Room sequencecase guest $room)
    $room = Get-Room sequencecase
    Set-Field $room launch (New-Launch)
    $room.started = $true
    Set-Field $room battle (New-Battle $room)
    Assert-Status 'host creates simultaneous battle' (Put-Room sequencecase host $room)

    Create-Room practicecase
    Assert-Status 'practice guest joins' (Join-Room practicecase guest)
    $room = Get-Room practicecase
    Set-Field $room level @{ id = 45678; name = 'Practice Map'; difficulty = 7; stars = 10; demon = $false; autoLevel = $false }
    $room.rules.practice = $true
    Assert-Status 'host configures practice rule' (Put-Room practicecase host $room)
    $room = Get-Room practicecase
    $room.rules.sequence = $true
    Assert-Status 'sequence cannot be enabled with practice' (Put-Room practicecase host $room) @(401, 403)
    Ready-Host practicecase
    $room = Get-Room practicecase
    $room.guestReady = $true
    Assert-Status 'practice guest marks Ready' (Put-Room practicecase guest $room)
    $room = Get-Room practicecase
    Set-Field $room launch (New-Launch)
    $room.started = $true
    Set-Field $room battle (New-Battle $room)
    Assert-Status 'host creates practice battle' (Put-Room practicecase host $room)
    $room = Get-Room practicecase
    $room.launch.hostLoaded = $true
    $room.launch.guestLoaded = $true
    $room.launch.releasedAt = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - 5000
    Assert-Status 'seed released practice battle' (Send-Request PUT 'versus-v1/rooms/practicecase' $room -Admin)
    $room = Get-Room practicecase
    $room.battle.host.attemptsUsed = 1
    $room.battle.host.runNumber = 2
    $room.battle.host.bestPercent = 100
    $room.battle.host.currentPercent = 100
    $room.battle.host.inAttempt = $false
    $room.battle.host.cleared = $true
    $room.battle.host.spectating = $true
    $room.battle.host.updatedAt = Timestamp
    $room.hostSeen = Timestamp
    Assert-Status 'practice clear records used attempts' (Put-Room practicecase host $room)
    $room = Get-Room practicecase
    $room.battle.guest.bestPercent = 100
    $room.battle.guest.currentPercent = 100
    $room.battle.guest.inAttempt = $false
    $room.battle.guest.cleared = $true
    $room.battle.guest.spectating = $true
    $room.battle.guest.updatedAt = Timestamp
    $room.battle.winnerUid = 'guest'
    $room.battle.finishedAt = Timestamp
    $room.guestSeen = Timestamp
    Assert-Status 'practice winner uses fewer attempts' (Put-Room practicecase guest $room)

    Create-Room pausecase
    Assert-Status 'pause guest joins' (Join-Room pausecase guest)
    $room = Get-Room pausecase
    Set-Field $room level @{ id = 56789; name = 'Pause Map'; difficulty = 3; stars = 5; demon = $false; autoLevel = $false }
    Assert-Status 'pause map selected' (Put-Room pausecase host $room)
    Ready-Host pausecase
    $room = Get-Room pausecase
    $room.guestReady = $true
    Assert-Status 'pause guest marks Ready' (Put-Room pausecase guest $room)
    $room = Get-Room pausecase
    Set-Field $room launch (New-Launch)
    $room.started = $true
    Set-Field $room battle (New-Battle $room)
    Assert-Status 'host creates pause battle' (Put-Room pausecase host $room)
    $room = Get-Room pausecase
    $room.launch.hostLoaded = $true
    $room.launch.guestLoaded = $true
    $room.launch.releasedAt = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - 5000
    Assert-Status 'seed released pause battle' (Send-Request PUT 'versus-v1/rooms/pausecase' $room -Admin)
    $room = Get-Room pausecase
    $room.battle.guest.paused = $true
    $room.battle.guest.pausedAt = Timestamp
    $room.battle.guest.updatedAt = Timestamp
    $room.guestSeen = Timestamp
    Assert-Status 'guest reports pause state' (Put-Room pausecase guest $room)
    $room = Get-Room pausecase
    $room.battle.guest.pausedAt = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - 31000
    Assert-Status 'seed elapsed pause timeout' (Send-Request PUT 'versus-v1/rooms/pausecase' $room -Admin)
    $room = Get-Room pausecase
    $room.battle.host.updatedAt = Timestamp
    $room.battle.winnerUid = 'host'
    $room.battle.finishedAt = Timestamp
    $room.hostSeen = Timestamp
    Assert-Status 'opponent may finalize 30-second pause loss' (Put-Room pausecase host $room)


    # Detailed history snapshots, return acknowledgements, and private attempt logs.
    $room = Get-Room practicecase
    $match = $room.battle.id
    Set-Field $room.battle hostReturned $true
    Assert-Status 'finished player may acknowledge before history saved' (Put-Room practicecase host $room)
    $room = Get-Room practicecase
    foreach ($uid in @('host','guest')) {
        $isHost = $uid -eq 'host'
        $ownProfile = if ($isHost) { $room.host } else { $room.guest }
        $otherProfile = if ($isHost) { $room.guest } else { $room.host }
        $ownStats = if ($isHost) { $room.battle.host } else { $room.battle.guest }
        $otherStats = if ($isHost) { $room.battle.guest } else { $room.battle.host }
        $detail = @{ roomId='practicecase'; opponentName=$otherProfile.name; levelName=$room.level.name; winnerName='guest'; result=$(if($isHost){'loss'}else{'win'}); playedAt=(Timestamp); levelId=$room.level.id; self=$ownProfile; opponent=$otherProfile; selfStats=$ownStats; opponentStats=$otherStats; rules=$room.rules }
        $forged = $detail | ConvertTo-Json -Depth 100 | ConvertFrom-Json
        $forged.opponent.icon = 999
        Assert-Status "$uid cannot forge opponent icon" (Send-Request PUT "versus-v1/history/$uid/$match" $forged -Uid $uid) @(401,403)
        $forged = $detail | ConvertTo-Json -Depth 100 | ConvertFrom-Json
        $forged.selfStats.bestPercent = 99
        Assert-Status "$uid cannot forge final percent" (Send-Request PUT "versus-v1/history/$uid/$match" $forged -Uid $uid) @(401,403)
        Assert-Status "$uid archives detailed match" (Send-Request PUT "versus-v1/history/$uid/$match" $detail -Uid $uid)
        Assert-Status "$uid retries archive with null etag" (Send-Request PUT "versus-v1/history/$uid/$match" $detail -Uid $uid -ETag 'null_etag') @(412)

    }
    Assert-Status 'own archive receipt readable after lost response' (Send-Request GET "versus-v1/history/host/$match" -Uid host)
    Assert-Status 'opponent archive receipt private' (Send-Request GET "versus-v1/history/host/$match" -Uid guest) @(401,403)
    $attempt = @{ roomId='practicecase'; run=1; percent=30; endedAt=(Timestamp) }
    Assert-Status 'host records first attempt' (Send-Request PUT "versus-v1/matchAttempts/$match/host/r1" $attempt -Uid host)
    $attempt.percent = 29
    Assert-Status 'recorded attempt immutable' (Send-Request PUT "versus-v1/matchAttempts/$match/host/r1" $attempt -Uid host) @(401,403)
    $attempt.percent = 100; $attempt.run = 2
    Assert-Status 'host records clear attempt' (Send-Request PUT "versus-v1/matchAttempts/$match/host/r2" $attempt -Uid host)
    Assert-Status 'guest cannot write host attempt' (Send-Request PUT "versus-v1/matchAttempts/$match/host/r2" $attempt -Uid guest) @(401,403)
    $attempt.run=3
    Assert-Status 'cannot invent unused attempt' (Send-Request PUT "versus-v1/matchAttempts/$match/host/r3" $attempt -Uid host) @(401,403)
    $attempt.run=1
    Assert-Status 'guest records own attempt' (Send-Request PUT "versus-v1/matchAttempts/$match/guest/r1" $attempt -Uid guest)
    Assert-Status 'attempt key matches run' (Send-Request PUT "versus-v1/matchAttempts/$match/guest/r2" $attempt -Uid guest) @(401,403)
    $query='orderBy=%22run%22&startAt=1&endAt=10&limitToFirst=10'
    Assert-Status 'own attempt page' (Send-Request GET "versus-v1/matchAttempts/$match/host" -Uid host -Query $query)
    Assert-Status 'opponent attempt page' (Send-Request GET "versus-v1/matchAttempts/$match/host" -Uid guest -Query $query)
    Assert-Status 'outsider cannot read attempt page' (Send-Request GET "versus-v1/matchAttempts/$match/host" -Uid third -Query $query) @(401,403)
    Assert-Status 'unbounded attempts denied' (Send-Request GET "versus-v1/matchAttempts/$match/host" -Uid host) @(401,403)
    $room = Get-Room practicecase
    Set-Field $room.battle guestReturned $true
    Assert-Status 'host cannot acknowledge opponent result' (Put-Room practicecase host $room) @(401,403)
    $room = Get-Room practicecase
    Set-Field $room.battle hostReturned $true
    Assert-Status 'host returns after archive saved' (Put-Room practicecase host $room)
    $room = Get-Room practicecase
    Set-Field $room.battle guestReturned $true
    Assert-Status 'guest returns after archive saved' (Put-Room practicecase guest $room)
    $room = Get-Room practicecase
    Remove-Field $room.battle hostReturned
    Assert-Status 'acknowledgement cannot be removed' (Put-Room practicecase guest $room) @(401,403)

    $room = Get-Room practicecase
    $room.started=$false; $room.hostReady=$false; $room.guestReady=$false
    Remove-Field $room launch
    Assert-Status 'same room resets for rematch' (Put-Room practicecase host $room)
    $room = Get-Room practicecase
    Assert-True 'both players and map retained after result' ($room.host.uid -eq 'host' -and $room.guest.uid -eq 'guest' -and $room.level.id -gt 0 -and !$room.started)
    Assert-Status 'history still authorizes opponent attempts after reset' (Send-Request GET "versus-v1/matchAttempts/$match/host" -Uid guest -Query $query)
    Assert-Status 'delayed own attempt remains writable after reset' (Send-Request PUT "versus-v1/matchAttempts/$match/guest/r1" $attempt -Uid guest)

    # History denial/unavailability cannot hold a finished match hostage.
    $fallback = Get-Room battlecase
    $fallback.launch.id = [Guid]::NewGuid().ToString('N')
    $fallback.battle.id = $fallback.launch.id
    Remove-Field $fallback.battle hostReturned
    Remove-Field $fallback.battle guestReturned
    Assert-Status 'seed finished match with no archival receipts' (Send-Request PUT 'versus-v1/rooms/returnfallback' $fallback -Admin)
    $fallback = Get-Room returnfallback
    Set-Field $fallback.battle hostReturned $true
    Assert-Status 'host return independent of archival receipt' (Put-Room returnfallback host $fallback)
    $fallback = Get-Room returnfallback
    Set-Field $fallback.battle guestReturned $true
    Assert-Status 'guest return independent of archival receipt' (Put-Room returnfallback guest $fallback)
    $fallback = Get-Room returnfallback
    $fallback.started=$false; $fallback.hostReady=$false; $fallback.guestReady=$false
    Remove-Field $fallback launch
    Assert-Status 'either returning player can reset finished match' (Put-Room returnfallback guest $fallback)
    $fallback = Get-Room returnfallback
    Assert-True 'archive failure still retains both players and selected map' (!$fallback.started -and $null -eq $fallback.battle -and $fallback.host.uid -eq 'host' -and $fallback.guest.uid -eq 'guest' -and $fallback.level.id -gt 0)
    $fallback.hostReady = $true
    Assert-Status 'host may ready next round immediately after return' (Put-Room returnfallback host $fallback)
    $fallback = Get-Room returnfallback
    $fallback.guestReady = $true
    Assert-Status 'guest may ready next round immediately after return' (Put-Room returnfallback guest $fallback)

    $history = @{}
    for ($i = 1; $i -le 12; $i++) {
        $history["match$i"] = @{ playedAt = $i; opponentName = 'guest'; levelName = 'Test'; winnerName = 'host'; result = 'win' }
    }
    Assert-Status 'trusted server may seed history' (Send-Request PUT 'versus-v1/history/host' $history -Admin)
    $recent = Send-Request GET 'versus-v1/history/host' -Uid host -Query 'orderBy=%22playedAt%22&limitToLast=10'
    Assert-Status 'owner reads last ten history records' $recent
    Assert-True 'history contains only latest ten of twelve' (@($recent.Data.PSObject.Properties).Count -eq 10 -and $null -eq $recent.Data.match1 -and $null -eq $recent.Data.match2 -and $null -ne $recent.Data.match12)
    Assert-Status 'other user cannot read private history' (Send-Request GET 'versus-v1/history/host' -Uid guest -Query 'orderBy=%22playedAt%22&limitToLast=10') @(401, 403)
    Assert-Status 'unbounded history denied' (Send-Request GET 'versus-v1/history/host' -Uid host) @(401, 403)
    Assert-Status 'client cannot invent match results' (Send-Request PUT 'versus-v1/history/host' $history -Uid host) @(401, 403)
    Assert-Status 'client cannot delete history' (Send-Request DELETE 'versus-v1/history/host' -Uid host) @(401, 403)

    Assert-Status 'host can close private room' (Send-Request DELETE 'versus-v1/rooms/private' -Uid host)
    Assert-Status 'host can remove private admission proofs' (Send-Request DELETE 'versus-v1/admissions/private' -Uid host)
    Assert-Status 'host can remove closed room secret' (Send-Request DELETE 'versus-v1/roomSecrets/private' -Uid host)

    $largeList = @{}
    $currentTime = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    for ($i = 0; $i -lt 205; $i++) {
        $room = New-Room host
        $room.updatedAt = $currentTime - 205 + $i
        $room.hostSeen = $currentTime
        $largeList["volume$i"] = $room
    }
    Assert-Status 'seed 205 active rooms for bounded listing' (Send-Request PUT 'versus-v1/rooms' $largeList -Admin)
    $list = Send-Request GET 'versus-v1/rooms' -Uid guest -Query ('orderBy=%22updatedAt%22&limitToLast=200&startAt=' + ($currentTime - 45000))
    Assert-Status 'service list query accepted with 205 rooms' $list
    Assert-True 'large listing returns 200 newest rooms' (@($list.Data.PSObject.Properties).Count -eq 200 -and $null -eq $list.Data.volume0 -and $null -ne $list.Data.volume204)

    Write-Output "All $script:passed Firebase emulator checks passed."
}
finally {
    $script:client.Dispose()
}
