# Invoked inside firebase-rules.ps1's isolated loopback emulator session.
Create-Room multimask
Assert-Status 'multi guest joins' (Join-Room multimask guest)
$room = Get-Room multimask
Set-Field $room mapSelection @{ random=$true; mask=2304; platformer=$false }
Assert-Status 'guest cannot change mask' (Put-Room multimask guest $room) @(401,403)
Assert-Status 'host selects easy OR insane demon' (Put-Room multimask host $room)
foreach ($bad in @(0, -1, 8192, 1.5)) {
    $room = Get-Room multimask; $room.mapSelection.mask = $bad
    Assert-Status "invalid random mask $bad rejected" (Put-Room multimask host $room) @(401,403)
}
$room = Get-Room multimask
Remove-Field $room.mapSelection mask; Set-Field $room.mapSelection difficulty 0
Assert-Status 'mask cannot downgrade back to legacy filter' (Put-Room multimask host $room) @(401,403)
Ready-Host multimask
$room = Get-Room multimask; $room.guestReady=$true
Assert-Status 'multi mask pre-draw ready' (Put-Room multimask guest $room)
$room = Get-Room multimask; $room.mapSelection.mask=8191
Assert-Status 'filter edit requires ready reset' (Put-Room multimask host $room) @(401,403)
$room.hostReady=$false; $room.guestReady=$false
Assert-Status 'mask edit resets both ready states' (Put-Room multimask host $room)

# Cross-check rules bit tests with the client bit layout, including precise
# star deselection and arbitrary OR combinations, for Classic and Platformer.
$masks = @(1,2,4,8,16,32,64,128,256,512,1024,2048,4096,2304,20,8191)
foreach ($mask in $masks) {
    foreach ($platformer in @($false,$true)) {
        $id = "mask-$mask-$platformer"
        $base = New-Room host
        $base.guest=New-Profile guest; $base.guestSeen=Timestamp
        $base.hostReady=$true; $base.guestReady=$true
        $base.mapSelection=@{random=$true;mask=$mask;platformer=$platformer}
        Assert-Status "$id seed ready room" (Send-Request PUT "versus-v1/rooms/$id" $base -Admin)
        for ($bit=0; $bit -lt 13; $bit++) {
            $sample=@{id=4001;name='Filter test';creator='Creator';difficulty=$(if($bit -lt 8){3}else{@(7,8,6,9,10)[$bit-8]});stars=$(if($bit -lt 8){$bit+2}else{10});demon=($bit -ge 8);autoLevel=$false;platformer=$platformer}
            $second=$sample.Clone(); $second.id=4002
            $room=Get-Room $id; $room.hostReady=$false; $room.guestReady=$false
            Set-Field $room mapDraw @{id=[Guid]::NewGuid().ToString('N');at=(Timestamp);selected='0';settled=$false;levels=@($sample,$second)}
            $expected=if(($mask -band (1 -shl $bit)) -ne 0){@(200)}else{@(401,403)}
            Assert-Status "$id bit $bit exact eligibility" (Put-Room $id host $room) $expected
            if($expected[0] -eq 200){
                Assert-Status "$id restore pre-draw fixture" (Send-Request PUT "versus-v1/rooms/$id" $base -Admin)
            }
        }
    }
}

# Mixed demon candidates and immutable creator metadata round-trip.
$room=Get-Room multimask; $room.mapSelection.mask=2304
Assert-Status 'select mixed demons for creator test' (Put-Room multimask host $room)
Ready-Host multimask
$room=Get-Room multimask; $room.guestReady=$true
Assert-Status 'guest ready for mixed demon draw' (Put-Room multimask guest $room)
$room=Get-Room multimask; $room.hostReady=$false; $room.guestReady=$false
$a=@{id=5001;name='Easy map';creator='Maker One';difficulty=7;stars=10;demon=$true;autoLevel=$false;platformer=$false}
$b=@{id=5002;name='Insane map';creator='Maker Two';difficulty=9;stars=10;demon=$true;autoLevel=$false;platformer=$false}
Set-Field $room mapDraw @{id=[Guid]::NewGuid().ToString('N');at=(Timestamp);selected='1';settled=$false;levels=@($a,$b)}
Assert-Status 'different selected demon tiers coexist in roulette' (Put-Room multimask host $room)
$room=Get-Room multimask; $room.mapDraw.levels[0].creator='Forged'
Assert-Status 'roulette creator immutable' (Put-Room multimask host $room) @(401,403)
$room=Get-Room multimask; Remove-Field $room.mapDraw.levels[0] creator
Assert-Status 'roulette creator cannot be deleted' (Put-Room multimask host $room) @(401,403)
$room=Get-Room multimask; $room.mapDraw.at=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()-10000
Assert-Status 'advance creator draw clock fixture' (Send-Request PUT 'versus-v1/rooms/multimask' $room -Admin)
$room=Get-Room multimask; $room.mapDraw.settled=$true
Set-Field $room level ($b.Clone()); $room.level.creator='Wrong'
Assert-Status 'selected creator must match winning card' (Put-Room multimask host $room) @(401,403)
$room.level.creator='Maker Two'
Assert-Status 'settled map keeps winning creator' (Put-Room multimask host $room)

# Pair-local score is derived from the immutable finished battle, never a
# freely writable counter. Test either peer winning and resetting the room.
Create-Room scorecase
$room=Get-Room scorecase
Set-Field $room score @{host=0;guest=0;lastMatch=''}
Set-Field $room mapSelection @{random=$false;mask=0;platformer=$false}
Set-Field $room level @{id=6001;name='Score map';creator='Builder';difficulty=3;stars=5;demon=$false;autoLevel=$false;platformer=$false}
Assert-Status 'initialize score and manual mode with empty mask' (Put-Room scorecase host $room)
Assert-Status 'join zeroed score room' (Join-Room scorecase guest)
function Seed-ScoreResult([string]$Winner,[bool]$Draw=$false) {
    $room=Get-Room scorecase
    $room.started=$true; $room.hostReady=$true; $room.guestReady=$true
    Set-Field $room launch (New-Launch)
    Set-Field $room battle (New-Battle $room)
    $room.launch.releasedAt=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()-10000
    $room.battle.finishedAt=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()-1000
    $room.battle.winnerUid=$Winner; $room.battle.draw=$Draw
    $room.battle.host.attemptsUsed=3; $room.battle.guest.attemptsUsed=3
    $room.battle.host.inAttempt=$false; $room.battle.guest.inAttempt=$false
    $room.battle.host.bestPercent=$(if($Winner -eq 'host' -or $Draw){80}else{70})
    $room.battle.guest.bestPercent=$(if($Winner -eq 'guest' -or $Draw){80}else{70})
    Assert-Status "seed finished score result $Winner $Draw" (Send-Request PUT 'versus-v1/rooms/scorecase' $room -Admin)
}
function Score-ResetBody {
    $room=Get-Room scorecase
    $room.score.lastMatch=$room.battle.id
    if(!$room.battle.draw){
        if($room.battle.winnerUid -eq $room.host.uid){$room.score.host++}else{$room.score.guest++}
    }
    $room.started=$false; $room.hostReady=$false; $room.guestReady=$false
    Remove-Field $room launch
    return $room
}
Seed-ScoreResult host
$skipScore=@{started=$false;hostReady=$false;guestReady=$false;launch=$null;battle=$null;updatedAt=(Timestamp)}
Assert-Status 'partial room reset cannot bypass score settlement' (Send-Request PATCH 'versus-v1/rooms/scorecase' $skipScore -Uid guest) @(401,403)
$room=Score-ResetBody; $room.score.host=0
Assert-Status 'finished reset cannot lose winner point' (Put-Room scorecase host $room) @(401,403)
$room=Score-ResetBody; $room.score.host=2
Assert-Status 'finished reset cannot award two points' (Put-Room scorecase host $room) @(401,403)
$room=Score-ResetBody; $room.score.guest=1
Assert-Status 'loser cannot claim a win' (Put-Room scorecase guest $room) @(401,403)
$room=Score-ResetBody; Remove-Field $room score
Assert-Status 'score cannot be erased during reset' (Put-Room scorecase host $room) @(401,403)
Assert-Status 'guest commits host win on return' (Put-Room scorecase guest (Score-ResetBody))
$room=Get-Room scorecase; $room.score.host++
Assert-Status 'same completed result cannot be counted twice' (Put-Room scorecase host $room) @(401,403)
$room=Get-Room scorecase
Assert-Status 'retry unchanged reset remains idempotent' (Put-Room scorecase guest $room)
Seed-ScoreResult guest
Assert-Status 'host commits guest win on return' (Put-Room scorecase host (Score-ResetBody))
Seed-ScoreResult host
Assert-Status 'second host win persists' (Put-Room scorecase host (Score-ResetBody))
$room=Get-Room scorecase
Assert-True 'three rounds show host two guest one' ($room.score.host -eq 2 -and $room.score.guest -eq 1)
Seed-ScoreResult '' $true
Assert-Status 'draw does not increment either win count' (Put-Room scorecase guest (Score-ResetBody))
$room=Get-Room scorecase
Assert-True 'draw keeps score two to one' ($room.score.host -eq 2 -and $room.score.guest -eq 1)
$room.score.host=0; $room.score.guest=0; $room.score.lastMatch=''
Assert-Status 'host cannot reset score against same opponent' (Put-Room scorecase host $room) @(401,403)
$room=Get-Room scorecase; $room.level.name='Another map'; $room.level.creator='Another maker'
Assert-Status 'manual map change preserves pair score' (Put-Room scorecase host $room)
$room=Get-Room scorecase; $room.rules.attempts=4
Assert-Status 'rule changes preserve pair score' (Put-Room scorecase host $room)
$room=Get-Room scorecase; $room.level.creator='x'*33
Assert-Status 'oversized creator rejected' (Put-Room scorecase host $room) @(401,403)
$room=Get-Room scorecase; $room.level.creator='Guest forged creator'
Assert-Status 'guest cannot modify creator' (Put-Room scorecase guest $room) @(401,403)
$room=Get-Room scorecase; Remove-Field $room guest; Remove-Field $room guestSeen
Assert-Status 'departure must reset both scores' (Put-Room scorecase guest $room) @(401,403)
Assert-Status 'partial guest departure must also reset scores' (Send-Request PATCH 'versus-v1/rooms/scorecase' @{guest=$null;guestSeen=$null;updatedAt=(Timestamp)} -Uid guest) @(401,403)
$room.score.host=0; $room.score.guest=0; $room.score.lastMatch=''
Assert-Status 'guest departure clears pair score' (Put-Room scorecase guest $room)
Assert-Status 'new challenger starts from zero' (Join-Room scorecase third)
$room=Get-Room scorecase
Assert-True 'new pair has zero wins' ($room.score.host -eq 0 -and $room.score.guest -eq 0 -and $room.score.lastMatch -eq '')
$room.score.host=7; $room.score.guest=4; $room.guestSeen=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()-46000
Assert-Status 'seed timed-out challenger with prior wins' (Send-Request PUT 'versus-v1/rooms/scorecase' $room -Admin)
$room=Get-Room scorecase; Remove-Field $room guest; Remove-Field $room guestSeen
Assert-Status 'timeout cannot retain stale wins' (Put-Room scorecase host $room) @(401,403)
$room.score.host=0; $room.score.guest=0; $room.score.lastMatch=''
Assert-Status 'host timeout cleanup resets both scores' (Put-Room scorecase host $room)
