# Scoped grants, preparation snapshots, feature decorations and mode guards.
# Loaded by firebase-rules.ps1 in its isolated local emulator namespace.
Create-Room preparation
Assert-Status 'preparation guest joins' (Join-Room preparation guest)
$room=Get-Room preparation
Set-Field $room level @{id=7201;name='Prepare';creator='Builder';difficulty=3;stars=5;demon=$false;autoLevel=$false;platformer=$false;featureState=4}
Assert-Status 'host selects Mythic level' (Put-Room preparation host $room)
foreach($feature in @(-1,5,1.5)) {
    $room=Get-Room preparation; $room.level.featureState=$feature
    Assert-Status "invalid feature $feature denied" (Put-Room preparation host $room) @(401,403)
}
$room=Get-Room preparation; $room.level.featureState=1
Assert-Status 'guest cannot change feature' (Put-Room preparation guest $room) @(401,403)
$room=Get-Room preparation; Remove-Field $room.level featureState
Assert-Status 'level feature cannot be deleted' (Put-Room preparation host $room) @(401,403)
function New-Download([string]$Uid) {
    return @{uid=$Uid;levelId=7201;percent=50;mapReady=$true;songsDone=0;songsTotal=1;soundsDone=0;soundsTotal=0;updatedAt=(Timestamp)}
}
foreach($uid in @('host','guest')) {
    $room=Get-Room preparation
    if($null -eq $room.downloads){Set-Field $room downloads @{}}
    Set-Field $room.downloads $uid (New-Download $uid)
    $other=if($uid -eq 'host'){'guest'}else{'host'}
    Assert-Status "$other cannot invent $uid progress" (Put-Room preparation $other $room) @(401,403)
    Assert-Status "$uid publishes own progress" (Put-Room preparation $uid $room)
    $room=Get-Room preparation
    $room.downloads.$uid.percent=60; $room.downloads.$uid.updatedAt=Timestamp
    Assert-Status "$other cannot edit $uid progress" (Put-Room preparation $other $room) @(401,403)
    Assert-Status "$uid advances own progress" (Put-Room preparation $uid $room)
    $room=Get-Room preparation; Remove-Field $room.downloads $uid
    Assert-Status "$other cannot delete $uid progress" (Put-Room preparation $other $room) @(401,403)
    Assert-Status "$uid cannot delete snapshot" (Put-Room preparation $uid $room) @(401,403)
}
$room=Get-Room preparation; Remove-Field $room downloads
Assert-Status 'cannot erase download parent' (Put-Room preparation host $room) @(401,403)
foreach($invalid in @(
    @{field='percent';value=100}, @{field='percent';value=101}, @{field='percent';value=1.5},
    @{field='levelId';value=7202}, @{field='uid';value='guest'},
    @{field='songsDone';value=2}, @{field='soundsDone';value=-1},
    @{field='songsTotal';value=100001}, @{field='mapReady';value=$false},
    @{field='updatedAt';value=1}
)) {
    $room=Get-Room preparation
    $room.downloads.host.updatedAt=Timestamp
    Set-Field $room.downloads.host $invalid.field $invalid.value
    Assert-Status "malformed preparation $($invalid.field) $($invalid.value) denied" (Put-Room preparation host $room) @(401,403)
}
$room=Get-Room preparation; Remove-Field $room.downloads.host songsDone
Assert-Status 'required progress field deletion denied' (Put-Room preparation host $room) @(401,403)
$room=Get-Room preparation; Set-Field $room.downloads outsider (New-Download third)
Assert-Status 'third progress seat denied' (Put-Room preparation host $room) @(401,403)
$room=Get-Room preparation
$room.downloads.host.songsDone=1; $room.downloads.host.percent=100; $room.downloads.host.updatedAt=Timestamp
Assert-Status 'complete preparation accepted' (Put-Room preparation host $room)
$room=Get-Room preparation; $room.level.id=7202
Assert-Status 'map change preserves old tagged snapshots' (Put-Room preparation host $room)
$room=Get-Room preparation; $room.downloads.host=New-Download host; $room.downloads.host.levelId=7202
Assert-Status 'new map resets own progress' (Put-Room preparation host $room)
$room=Get-Room preparation; $room.level.id=7201
Assert-Status 'restore shared download map' (Put-Room preparation host $room)
$room=Get-Room preparation
$room.started=$true; $room.hostReady=$true; $room.guestReady=$true
Set-Field $room launch (New-Launch); Set-Field $room battle (New-Battle $room)
Assert-Status 'seed synchronized loading fixture' (Send-Request PUT 'versus-v1/rooms/preparation' $room -Admin)
$room=Get-Room preparation; $room.downloads.host=New-Download host
Assert-Status 'shared progress continues before launch release' (Put-Room preparation host $room)
$room=Get-Room preparation
$room.launch.hostLoaded=$true; $room.launch.guestLoaded=$true
$room.launch.releasedAt=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()-5000
Assert-Status 'seed released loading fixture' (Send-Request PUT 'versus-v1/rooms/preparation' $room -Admin)
$room=Get-Room preparation
$room.downloads.host.percent=70; $room.downloads.host.updatedAt=Timestamp
Assert-Status 'preparation writes stop after match release' (Put-Room preparation host $room) @(401,403)
$room=Get-Room preparation; $room.hostSeen=Timestamp
Assert-Status 'battle heartbeat preserves preparation snapshots' (Put-Room preparation host $room)
$room=Get-Room preparation; $room.started=$false; $room.hostReady=$false; $room.guestReady=$false
Remove-Field $room launch; Remove-Field $room battle
Assert-Status 'restore preparation lobby fixture' (Send-Request PUT 'versus-v1/rooms/preparation' $room -Admin)

# Selected Platformer and a random Platformer filter both prohibit Percent.
$room=Get-Room preparation; $room.level.platformer=$true
Assert-Status 'platformer supported in Attempts' (Put-Room preparation host $room)
$room=Get-Room preparation; $room.rules.mode=1
Assert-Status 'Percent with selected platformer denied' (Put-Room preparation host $room) @(401,403)
$room=Get-Room preparation; $room.level.platformer=$false; $room.rules.mode=1
Assert-Status 'classic supports Percent' (Put-Room preparation host $room)
$room=Get-Room preparation; $room.level.platformer=$true
Assert-Status 'platformer selection during Percent denied' (Put-Room preparation host $room) @(401,403)
$room=Get-Room preparation; Remove-Field $room level
Set-Field $room mapSelection @{random=$true;mask=1;platformer=$true}
Assert-Status 'random platformer during Percent denied' (Put-Room preparation host $room) @(401,403)
$room.rules.mode=0
Assert-Status 'random platformer supports Attempts' (Put-Room preparation host $room)
$room=Get-Room preparation; $room.rules.mode=1
Assert-Status 'Percent with random platformer denied' (Put-Room preparation host $room) @(401,403)

# Roulette feature data remains immutable, including an omitted legacy field.
Create-Room featuredraw
Assert-Status 'feature draw guest joins' (Join-Room featuredraw guest)
$room=Get-Room featuredraw
Set-Field $room mapSelection @{random=$true;mask=1;platformer=$false}
Assert-Status 'feature draw filter' (Put-Room featuredraw host $room)
Ready-Host featuredraw
$room=Get-Room featuredraw; $room.guestReady=$true
Assert-Status 'feature draw guest ready' (Put-Room featuredraw guest $room)
$room=Get-Room featuredraw; $room.hostReady=$false; $room.guestReady=$false
$cards=@(
    @{id=7301;name='Legacy';difficulty=1;stars=2;demon=$false;autoLevel=$false;platformer=$false},
    @{id=7302;name='Legendary';difficulty=1;stars=2;demon=$false;autoLevel=$false;platformer=$false;featureState=3}
)
Set-Field $room mapDraw @{id=[Guid]::NewGuid().ToString('N');at=(Timestamp);selected='1';settled=$false;levels=$cards}
Assert-Status 'optional and Legendary roulette metadata accepted' (Put-Room featuredraw host $room)
$room=Get-Room featuredraw; $room.mapDraw.levels[1].featureState=4
Assert-Status 'roulette feature immutable' (Put-Room featuredraw host $room) @(401,403)
$room=Get-Room featuredraw; Remove-Field $room.mapDraw.levels[1] featureState
Assert-Status 'roulette feature deletion denied' (Put-Room featuredraw host $room) @(401,403)
$room=Get-Room featuredraw; Set-Field $room.mapDraw.levels[0] featureState 4
Assert-Status 'legacy roulette feature cannot gain decoration' (Put-Room featuredraw host $room) @(401,403)
$room=Get-Room featuredraw; $room.mapDraw.at=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()-10000
Assert-Status 'advance feature draw fixture' (Send-Request PUT 'versus-v1/rooms/featuredraw' $room -Admin)
$room=Get-Room featuredraw; $room.mapDraw.settled=$true
Set-Field $room level ($cards[1].Clone()); $room.level.featureState=4
Assert-Status 'winner feature must match' (Put-Room featuredraw host $room) @(401,403)
$room.level.featureState=3
Assert-Status 'winner feature round trip' (Put-Room featuredraw host $room)
$room=Get-Room featuredraw; $room.mapDraw.selected='0'; $room.mapDraw.settled=$false
Remove-Field $room level
Assert-Status 'seed legacy winning card fixture' (Send-Request PUT 'versus-v1/rooms/featuredraw' $room -Admin)
$room=Get-Room featuredraw; $room.mapDraw.settled=$true
Set-Field $room level ($cards[0].Clone()); Set-Field $room.level featureState 0
Assert-Status 'legacy missing feature settles as undecorated zero' (Put-Room featuredraw host $room)

# Grants never authorize a stranger, an unbounded query, or a departed seat.
$grant=@{roomId='preparation';targetUid='host'}
Assert-Status 'stranger cannot request player history' (Send-Request PUT 'versus-v1/historyAccess/third' $grant -Uid third) @(401,403)
Assert-Status 'guest cannot write another viewer grant' (Send-Request PUT 'versus-v1/historyAccess/host' $grant -Uid guest) @(401,403)
Assert-Status 'room guest requests host recent matches' (Send-Request PUT 'versus-v1/historyAccess/guest' $grant -Uid guest)
$query='orderBy=%22playedAt%22&limitToLast=10'
Assert-Status 'current room guest reads bounded host history' (Send-Request GET 'versus-v1/history/host' -Uid guest -Query $query)
Assert-Status 'grant does not permit unbounded history' (Send-Request GET 'versus-v1/history/host' -Uid guest) @(401,403)
Assert-Status 'grant does not permit eleven history records' (Send-Request GET 'versus-v1/history/host' -Uid guest -Query 'orderBy=%22playedAt%22&limitToLast=11') @(401,403)
Assert-Status 'grant does not permit other target' (Send-Request GET 'versus-v1/history/third' -Uid guest -Query $query) @(401,403)
Assert-Status 'unauthenticated history denied' (Send-Request GET 'versus-v1/history/host' -Query $query) @(401,403)
$room=Get-Room preparation; Remove-Field $room guest; Remove-Field $room guestSeen
Assert-Status 'guest leaves while old progress retained' (Put-Room preparation guest $room)
Assert-Status 'leaving immediately revokes history grant' (Send-Request GET 'versus-v1/history/host' -Uid guest -Query $query) @(401,403)
Assert-Status 'replacement guest joins old progress room' (Join-Room preparation third)
Assert-Status 'replaced guest cannot reuse history grant' (Send-Request GET 'versus-v1/history/host' -Uid guest -Query $query) @(401,403)

$room=Get-Room featuredraw
Set-Field $room emote @{uid='host';kind='money';at=(Timestamp);nonce=[Guid]::NewGuid().ToString('N')}
$room.hostEmoteAt=Timestamp
Assert-Status 'membership money emote allowed on protocol' (Put-Room featuredraw host $room)
