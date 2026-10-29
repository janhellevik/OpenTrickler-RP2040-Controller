# Dopecard additions to this fork

This fork (`janhellevik/OpenTrickler-RP2040-Controller`) adds what the Dopecard trickler bridge
needs:

- read-only slot reads
- a link from each profile slot to the Dopecard powder profile it was loaded from
- a RAM history of completed charges

The changes stay in this fork. They are not offered upstream.

Everything else behaves as in upstream `main`. Existing clients keep working: the web portal, the
display menu and the iOS app's `profile_config` reads.

## REST additions

All endpoints are `GET` on port 80, like the rest of the firmware's API. Responses are JSON with
`Content-Type: application/json`.

### `GET /rest/profile_read?pf=N`

Reads profile slot `N` (0 to 7) **without selecting it**. `profile_config?pf=N` makes `N` the
selected profile; `profile_read` never does.

Without `pf` it reads the selected slot. The response has the same fields as `profile_config`,
plus the Dopecard link:

```json
{"pf":2,"p0":0,"p1":0,"p2":"Varget,gr","p3":0.050,"p4":0.000,"p5":0.300,"p6":0.100,"p7":5.000,
 "p8":2.000,"p9":0.000,"p10":12.000,"p11":0.060,"p12":5.000,"d0":"65f1c0ffee0123456789abcd","d1":3}
```

| Field | Meaning |
|---|---|
| `pf` | slot index |
| `p0`, `p1` | profile rev and compatibility |
| `p2` | name, at most 15 characters |
| `p3` to `p7` | coarse trickler: kp, ki, kd, min flow, max flow (rps) |
| `p8` to `p12` | fine trickler: kp, ki, kd, min flow, max flow (rps) |
| `d0` | Dopecard profile id, `""` when the slot is not linked |
| `d1` | Dopecard profile revision (its `version`), `0` when not linked |

If `pf` is out of range, empty or not a number, the response is
`{"error":"InvalidProfileIndex"}` with the normal JSON headers.

Floats are written with three decimals and a dot. Quotes and backslashes in `p2` and `d0` are
escaped, so the response is always valid JSON.

### `GET /rest/profile_config`

This endpoint works as before, with these changes:

- It accepts `d0` (at most 24 characters, `d0=` clears the link) and `d1` (unsigned 32-bit).
  Every response echoes both.
- `p2` is always stored NUL-terminated and cut to 15 characters. Before, a 16-character name was
  stored without its terminator.
- An out-of-range `pf` now gets the JSON headers too. Before, the error body was sent without
  them.
- A parameter without a value (`?p3`) is ignored. Before, it crashed the handler.

The slot index still comes from the *first* query parameter (`values[0]`), so `pf` must go first.
Writing to a slot also selects it, as before.

### `GET /rest/charge_history?since=<seq>`

Returns the completed charges recorded after `since`, at most 16 per response, oldest first:

```json
{"boot":3735928559,"next":42,"items":[
 {"seq":41,"t_ms":1234567,"target":42.000,"weight":42.020,"elapsed_s":11.37,"profile_idx":2,"event":0},
 {"seq":42,"t_ms":1251012,"target":42.000,"weight":42.060,"elapsed_s":10.92,"profile_idx":2,"event":4}]}
```

**Response fields:**

| Field | Meaning |
|---|---|
| `boot` | A random 32-bit id, new at every power-up. When it changes, the trickler has rebooted, its history is gone and `seq` starts again from 1. |
| `next` | What to pass as `since` on the next call: the last returned `seq`, or the newest recorded `seq` when nothing is returned. |
| `items` | The charges, oldest first. |

**Item fields:**

| Field | Meaning |
|---|---|
| `seq` | Monotonic from 1 for each boot. |
| `t_ms` | Uptime in ms when the charge was recorded. |
| `target` | The target weight. |
| `weight` | The scale reading about 1 s after the trickle stopped. This is the reading the over/under evaluation uses. `"nan"` or `"inf"` (as strings) when the scale reports one. |
| `elapsed_s` | The trickle time, the same value as `charge_mode_state`'s `s5`. |
| `profile_idx` | The slot the charge started with. |
| `event` | `4` for over charge, `2` for under charge, `0` when within the fine stop threshold. These are the same bits as `charge_mode_state`'s `s3`. |

**How it works:**

- A charge is recorded once its over/under evaluation is done, as the trickler shows "Remove Cup".
  A charge stopped with the reset button or `s2=0` before then is not recorded.
- The firmware keeps the last 64 charges in RAM. If more than 64 were recorded since `since`, the
  oldest ones are lost and the response starts at the oldest it still has.
- Call again with `next` until fewer than 16 items come back.
- Nothing is written to EEPROM. The bridge keeps the permanent copy.

## Request limits

The firmware's HTTP server limits every request:

- It reads at most **127 characters** of query string and drops the rest silently.
- It reads at most **16 parameters**.

A full profile write (`pf`, `p0` to `p12`, `d0`, `d1`, `ee`) doesn't fit in one request, so split
it:

- Send several `profile_config` requests, each starting with `pf=N`.
- Send `ee=true` only on the last one.
- Then read the slot back with `profile_read` and compare.

For example:

```
/rest/profile_config?pf=7&p2=Varget%2Cgr&p3=0.05&p4=0&p5=0.3&p6=0.1&p7=5
/rest/profile_config?pf=7&p8=2&p9=0&p10=12&p11=0.06&p12=5
/rest/profile_config?pf=7&d0=65f1c0ffee0123456789abcd&d1=3&ee=true
```

Each write selects slot 7. To leave the selection as it was, select the old slot again afterwards.

On this firmware and on stock firmware, an unknown path such as `/rest/profile_read` on stock gets
a truncated reply: 13 bytes (`HTTP/1.1 404 `) and then the connection closes. A client that probes
for these endpoints must therefore treat a parse error or a closed connection as "not supported".
A clean 404 never comes.

## EEPROM: profile data rev 2

Each `profile_t` has two new fields, `char dopecard_id[25]` and `uint32_t dopecard_rev`. The
profile block grows from 516 to 772 bytes (776 with its CRC32). It still fits in its 1 KB region
at 9K.

`EEPROM_PROFILE_DATA_REV` is now 2.

At boot, `profile_data_init` reads the block itself instead of calling `load_config`, which would
have replaced an older block with the default profiles. The decoding is in `src/profile_store.c`:

| Stored block | Result |
|---|---|
| rev 2 layout with a valid CRC | loaded as is |
| rev 1 layout with a valid CRC (any earlier firmware since 1.17) | migrated: every field copied, links empty, saved back as rev 2 |
| rev 1 layout tagged with the pre-1.17 rev number | migrated in the same way |
| anything else | the default profiles, as before |

Names are always NUL-terminated at 15 characters. An out-of-range selected slot becomes 0.

**Back up all 8 slots before flashing this firmware for the first time** (the bridge can do it).
The migration is covered by a host test, but the backup is what restores the profiles if anything
goes wrong. Going back to older firmware afterwards resets the profiles to the defaults, because
older firmware can't read the rev 2 block.

## Building

On macOS (Apple Silicon):

```sh
git submodule update --init --recursive
./setup_toolchain.sh            # downloads the ARM GNU toolchain into .toolchain/
source ./configure_env.sh
cmake -B build_pico2_w -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico2_w
cmake --build build_pico2_w     # -> build_pico2_w/app.uf2
cmake -B build_pico_w -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico_w
cmake --build build_pico_w      # -> build_pico_w/app.uf2
```

`.toolchain/` and `build*/` are ignored by git.

To run the migration test on the host (it needs only a C compiler):

```sh
./tests/run_host_tests.sh
```

## Flashing

Hold BOOTSEL while plugging in the Pico, then copy `app.uf2` to the drive that appears. After
the first boot on this firmware:

1. Read all 8 slots with `profile_read` and compare them to the backup.
2. Check that `profile_summary`'s `s1` is unchanged after the reads.
