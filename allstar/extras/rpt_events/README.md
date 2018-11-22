Asterisk/app_rpt AMI events

rpt_events adds support to receive events via Asterisk Manager Interface (AMI) from app_rpt.

Supported variables events:
  - EventName: "RPT_RXKEYED",     EventValue: 0|1
  - EventName: "RPT_ETXKEYED",    EventValue: 0|1
  - EventName: "RPT_LINKS",       EventValue: NUMLINKS,MODE NODEMUMx[,MODE NODEMUM...]
  - EventName: "RPT_NUMLINKS",    EventValue: NUMLINKS
  - EventName: "RPT_TXKEYED",     EventValue: 0|1
  - EventName: "RPT_ETXKEYED",    EventValue: 0|1
  - EventName: "RPT_ALINKS",      EventValue: NUMALINKS
  - EventName: "RPT_NUMALINKS",   EventValue: NUMALINKS,NODEMUM MODE RXKEYED[,NODEMUM MODE RXKEYED...]
  - EventName: "RPT_AUTOPATCHUP", EventValue: 0|1

Supported decoders events:
  - EventName: "MDC-1200", EventValue: UnitID
  - EventName: "DTMF"    , EventValue: DTMF character

This code was added to app_rpt.c on 01/12/2018
