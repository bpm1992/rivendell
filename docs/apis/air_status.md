# Air Status Through ripcd

Fork addition (4.4.1int4a). RDAirPlay and rdvairplayd send the status of their log
machines to ripcd(8), which holds the latest state and passes it on to clients that
subscribe. Commands go the other way as ordinary RML, with the line-ID commands and
command IDs described in `rml.xml` (Line IDs and Command IDs).

## Protocol

ripcd's own protocol on TCP port 5006: two-letter commands, arguments separated by
spaces, each command ending in `!`. A client authenticates first with `PW <password>!`
(`[Identity] Password` in `rd.conf`), and ripcd answers `PW +!`.

| Command | From | Does |
|---|---|---|
| `ST <payload>!` | RDAirPlay, rdvairplayd | One status message. ripcd updates what it holds and passes it to subscribers |
| `SS 1!` / `SS 0!` | Any client | Subscribe to status messages, or stop. On `SS 1` ripcd sends at once everything it holds |
| `SQ!` | Any client | Everything ripcd holds, now; and ripcd sends `SQ!` to every status source, which answers with a fresh full state |

**Versions:** ripcd and the players come from the same package. A 4.4.1int4a player
talking to an older ripcd would have its connection dropped by the first status message
longer than 256 characters (the old limit), and would reconnect every heartbeat; upgrade
the whole package.

Only subscribed connections get `ST` messages. RDLibrary, rdcatchd, panels and other
clients see nothing new. A status message may be longer than ripcd's usual 256-character
command limit (up to 8 MB); a full state of a 1,000-line log is about 30 KB.

## Messages

```
ST <v> <seq> <time> <type> <fields>!
```

- `v`: 1.
- `seq`: counts from 1 in each sending process (ripcd's own `source` messages have their
  own count).
- `time`: the station's wall clock, milliseconds since 1970.
- Every field is a number or a code word: no names or titles travel. Look them up in the
  station's database: titles and artists in `CART` by cart number, cut descriptions in
  `CUTS`, the loaded log's name in `LOG_MACHINES` by machine.
- `machine`: the RML machine number, 1 to 3 for RDAirPlay, 101 to 120 for rdvairplayd.
- `line_id`: `LOG_LINES.LINE_ID` as RDAirPlay holds it; `-1` for none.
- Times of day (`start`, `predicted`, `nextstop`) are milliseconds since local midnight,
  `-1` when unknown. Positions and lengths are milliseconds.

| `type` | When | Fields |
|---|---|---|
| `hello` | Connecting to ripcd, and before each full state | `app pid started caps` |
| `beat` | Every 10 s, and at once on a change of running, mode, caed connection or the On Air flag | `onair cae skipped`, then per machine `machine:mode:running:playing:next_line_id:next_trans` |
| `state` | After `hello`; on `SQ`; after skipped messages; after a log loads | per machine `M machine version mode running next_line_id`, then its decks `D line_id:cart:cut:position:length`, then every line `L line_id:cart:cut:status:trans:timetype:predicted:length` |
| `played` | A line starts | `machine line_id cart cut length start` |
| `paused` | A line pauses | `machine line_id position` |
| `stopped` | A line stops | `machine line_id position` |
| `pos` | Once a second per playing line | `machine line_id position` |
| `channel` | An audio channel starts or stops | `machine mport card port on` |
| `segue` | The active event changes | `machine line_id trans` |
| `next` | The next line changes | `machine line_id` |
| `nextstop` | The next stop time changes | `machine time` |
| `running` | The machine starts or stops running | `machine running` |
| `mode` | The machine's mode changes | `machine mode` |
| `loaded` | A log is loaded or renamed (a `state` follows) | `machine version` |
| `insert` | A line is inserted | `machine version line_id after_id cart cut trans` |
| `remove` | Lines are removed | `machine version line_id...` |
| `move` | A line is moved | `machine version line_id after_id` |
| `modify` | A line changes (its cut, its status) | `machine version line_id cut status` |
| `cmd` | After an RML command carrying a command ID | `id ok result` |
| `source` | ripcd: a source's connection state changed | `host app pid status` |

Example: `ST 1 4821 1795034412337 played 1 1187 65003 3 10658 61612300!`

**`hello`:** `app` is `rdairplay` or `rdvairplayd`; `pid` its process ID; `started` when
it started (ms since 1970); `caps` a comma list, `status,cmdid,lineid,pause,edit` in
4.4.1int4a. A client sends command IDs only when `caps` lists `cmdid`: a stock
Rivendell would take `#id` as an ordinary argument.

**`beat`:** `onair` is the On Air flag (0/1); `cae` whether the program is connected to
caed (0/1, see below); `skipped` how many status messages the program has skipped
because ripcd wasn't reading.

**`version`:** each machine counts every change to its copy of the log (load, insert,
remove, move, modify). Messages are never lost on TCP, so a gap in `version` only follows
a reconnection or skipped messages; send `SQ` then.

**`cmd`:** `ok` is 1 or 0. `result` for the line-ID commands is `-` when done, otherwise
one of `noline` (no such line), `played` (the line has played or is playing), `guard`
(inside `EditGuardSeconds`), `running` (`LP` while the machine runs or is paused),
`nocart`, `disabled` (`PA` with Pause Enabled off, or on rdvairplayd), `idle` (nothing
playing or paused), `failed`, `args`. For the older commands that take a command ID it is
`ran`: they report no result of their own.

**`source`:** `status` is `ok`, `stale` (no message for 30 s while the connection stays
open: the program's event loop is stuck) or `disconnected` (the connection closed: the
program exited or crashed). ripcd keeps the mark, and sends it with everything it holds,
until the program says `hello` again.

### Codes

| Field | Values |
|---|---|
| `mode` | 0 Previous, 1 LiveAssist, 2 Auto, 3 Manual (`RDAirPlayConf::OpMode`) |
| `trans`, `next_trans` | 0 Play, 1 Segue, 2 Stop, 255 none (`RDLogLine::TransType`); `-1` no next line |
| `status` | 1 Scheduled, 2 Playing, 3 Auditioning, 4 Finished, 5 Finishing, 6 Paused, 7 Stopping (`RDLogLine::Status`) |
| `timetype` | 0 Relative, 1 Hard, 255 none (`RDLogLine::TimeType`) |

## What ripcd holds

Per status source (one RDAirPlay and one rdvairplayd per host): its last `hello`, its
last `state` and every change since it (except `pos`, `channel` and `cmd`), its last
`beat`, and its `source` mark. After 5,000 changes ripcd drops them and asks the source
for a fresh state. A subscriber with more than 8 MB unsent (it stopped reading) loses its
subscription and must send `SS 1` again. A ripcd restart forgets everything; every source sends a full state
when its connection comes back.

## Never a backlog

A program writes to ripcd without blocking. While more than 256 KB waits unsent (ripcd
isn't reading), status messages are skipped and counted in `beat`; RML, replies and
notifications never are. When the socket drains below 64 KB, a full state replaces what
was skipped.

## caed connection

When caed closes its connection (it exited or restarted), the program logs it, reports
`cae 0` in the next `beat`, and reconnects every 2 seconds. What was loaded in the old
caed is gone: those plays fail, and the log carries on with fresh loads. Before this
fork, RDAirPlay kept polling the dead socket and never played again.

## Trying it

`tests/air_status_client.py` is a subscriber for testing: `watch` prints every message,
`state` asks for and summarizes a full state, `send 'LN 1 1187'` sends RML with a command
ID and prints its `cmd` result, `selftest 1` checks refusals and command IDs on machine 1
without changing what plays.
