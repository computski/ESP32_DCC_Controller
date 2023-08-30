#pragma once

//https://www.esp32.com/viewtopic.php?t=9703

//seeing heap error
//but also panic errors
//resolved most of these with criticalENTER/EXIT this is required when using ISRs, else you will see panic cache errors
//however, there still seems to be a crash if you access the web pages from within ED (Chromium) which might be a webserver or websockets thing


/*
Service mode read, returns --- and this does not timeout to ???
Is this linked to the 1mS flag?
I think -- is written locally in js, so perhaps the websocket response never comes?
unlike POM,  which sends a websocket 'ok' response, we don't send one for SM

ED points.  response seems slow.  actually you need to tap the button really quickly
if you hold it, then it does not change.  not sure if this is an ED setting, or whether
its sending a different message or the return message is ignored if your finger is still down.
(does it do this on ESP12 ver?)






*/