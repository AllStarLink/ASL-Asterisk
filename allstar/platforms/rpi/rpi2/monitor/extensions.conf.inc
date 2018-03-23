[console]
exten => ${NODE},1,Ringing
exten => ${NODE},n,Wait(3)
exten => ${NODE},n,Answer
exten => ${NODE},n,Playback(rpt/node)
exten => ${NODE},n,SayDigits(${EXTEN})
exten => ${NODE},n,rpt(${NODE}|P)
exten => ${NODE},n,Hangup


