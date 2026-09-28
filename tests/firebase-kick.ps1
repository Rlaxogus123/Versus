Create-Room kickcase
Assert-Status 'kick guest joins' (Join-Room kickcase guest)
$room=Get-Room kickcase
Set-Field $room lastKickUid guest
Remove-Field $room guest
Remove-Field $room guestSeen
Assert-Status 'stranger cannot kick' (Put-Room kickcase third $room) @(401,403)
Assert-Status 'guest cannot forge kick marker' (Put-Room kickcase guest $room) @(401,403)
Assert-Status 'host explicitly kicks live challenger' (Put-Room kickcase host $room)
Assert-Status 'new challenger can join after kick' (Join-Room kickcase third)
$room=Get-Room kickcase
Remove-Field $room lastKickUid
Assert-Status 'guest cannot delete removal notice' (Put-Room kickcase third $room) @(401,403)
$room=Get-Room kickcase
Set-Field $room.host membership $true
Assert-Status 'cannot change cosmetic edition after join' (Put-Room kickcase host $room) @(401,403)
Create-Room membercase
$room=Get-Room membercase
Set-Field $room guest (New-Profile guest)
Set-Field $room.guest membership $true
Set-Field $room guestSeen (Timestamp)
Assert-Status 'membership challenger joins' (Put-Room membercase guest $room)
$room=Get-Room membercase
Remove-Field $room.guest membership
Assert-Status 'membership flag cannot be deleted' (Put-Room membercase guest $room) @(401,403)
$room=Get-Room membercase
$room.guest.membership='yes'
Assert-Status 'membership flag rejects strings' (Put-Room membercase guest $room) @(401,403)
