# BeShare-over-MUSCLE protocol reference

Distilled from reading `HiShare/source/hishare/ShareNetClient.cpp`, `ShareFileTransfer.cpp`, and
MUSCLE's own `tools/chatclient.cpp` and `lang/python3/python_chat.py`. This is the wire contract
HiTuxShare must honour exactly; everything else about the app is an implementation detail.

See also `diagrams/protocol-flow.svg`.

---

## 1. Transport and framing

- TCP. Default MUSCLE server port **2960**. (BeShare's *peer-to-peer* transfer default listen port
  is 7000, with a scan range of 50.)
- Each message on the wire is an 8-byte header followed by the flattened `Message` body:

  | offset | size | field |
  |---|---|---|
  | 0 | 4 | body size, little-endian `uint32` |
  | 4 | 4 | encoding magic, little-endian `uint32` |

- Baseline encoding magic is `MUSCLE_MESSAGE_ENCODING_DEFAULT` = `1164862256` (`'Enc0'`).
  zlib encodings occupy the values above it and are negotiated with
  `PR_COMMAND_SETPARAMETERS { PR_NAME_REPLY_ENCODING = <level> }`.

## 2. The server-side node tree

`muscled` is a hierarchical database with live subscriptions. BeShare stores everything under a
`beshare` subtree, one branch per connected session:

```
/<hostname>/<sessionID>/beshare/name             { name, port, installid, version_name, … }
/<hostname>/<sessionID>/beshare/userstatus       { userstatus }
/<hostname>/<sessionID>/beshare/filecount        { filecount }
/<hostname>/<sessionID>/beshare/bandwidth        { label, bps }
/<hostname>/<sessionID>/beshare/uploadstats      { cur, max }
/<hostname>/<sessionID>/beshare/files/<filename> { beshare:File Size, … }   ← reachable peer
/<hostname>/<sessionID>/beshare/fires/<filename> { beshare:File Size, … }   ← firewalled peer
```

Path depths are meaningful and the client switches on them:

| depth | clause |
|---:|---|
| 0 | root |
| 1 | hostname |
| 2 | session ID |
| 3 | `beshare` |
| 4 | `name` / `userstatus` / `filecount` / `bandwidth` / `uploadstats` / `files` / `fires` |
| 5 | shared file name |

**`files` vs `fires` is the entire firewall signalling mechanism.** A peer that cannot accept
inbound connections publishes under `fires/`; downloaders seeing that path know they must ask the
peer to connect back to *them* instead. It is checked in HiShare by looking at a single character:
`GetPathClause(USER_NAME_DEPTH, nodepath)[2] == 'r'`.

## 3. Message `what` codes

BeShare's own codes (from `ShareNetClient.h`, and reproduced verbatim in MUSCLE's `chatclient.cpp`):

| value | name | meaning |
|---:|---|---|
| 0 | `NET_CLIENT_CONNECTED_TO_SERVER` | internal |
| 1 | `NET_CLIENT_DISCONNECTED_FROM_SERVER` | internal |
| 2 | `NET_CLIENT_NEW_CHAT_TEXT` | **chat message** |
| 3 | `NET_CLIENT_CONNECT_BACK_REQUEST` | "please connect to me, I'm firewalled" |
| 4 | `NET_CLIENT_CHECK_FILE_COUNT` | internal |
| 5 | `NET_CLIENT_PING` | client-to-client ping |
| 6 | `NET_CLIENT_PONG` | ping reply |
| 7 | `NET_CLIENT_SCAN_THREAD_REPORT` | internal |

Peer-to-peer transfer codes (`ShareFileTransfer.h`), based at `'tshr'`:

`TRANSFER_COMMAND_CONNECTED_TO_PEER`, `…_DISCONNECTED_FROM_PEER`, `…_FILE_LIST`, `…_FILE_HEADER`,
`…_FILE_DATA`, `…_DEPRECATED`, `…_NOTIFY_QUEUED`, `…_MD5_SEND_READ_DONE`, `…_MD5_RECV_READ_DONE`,
`…_PEER_ID`, `…_REJECTED`.

Server commands and results come from MUSCLE's `reflector/StorageReflectConstants.h`:
`PR_COMMAND_SETDATA`, `PR_COMMAND_SETPARAMETERS`, `PR_COMMAND_REMOVEDATA`,
`PR_COMMAND_REMOVEPARAMETERS`, `PR_COMMAND_GETPARAMETERS`, `PR_COMMAND_PING`, `PR_COMMAND_NOOP`,
`PR_COMMAND_JETTISONRESULTS`; `PR_RESULT_DATAITEMS`, `PR_RESULT_PARAMETERS`, `PR_RESULT_PONG`.

## 4. Login

```
PR_COMMAND_SETDATA {
   "beshare/name" = Message {
       String  "name"                     user's handle
       Int32   "port"                     our peer listen port (0 if we accept nothing)
       Int64   "installid"                stable per-installation ID
       String  "version_name"             e.g. "HiTuxShare"
       String  "version_num"              e.g. "0.1"
       Bool    "supports_partial_hashing" 64 KB partial-MD5 resume validation
       Bool    "supports_ranges"          honours "maxbytes" byte-range requests
       Bool    "supports_ssl"             optional; we leave this out for v1
   }
}
```

Then subscribe. `chatclient` and HiShare use the broad form; `python_chat` uses the narrow one:

```
PR_COMMAND_SETPARAMETERS { Bool "SUBSCRIBE:beshare/*" = true }
```

Add `PR_NAME_SUBSCRIBE_QUIETLY = true` to suppress the initial-state dump when you only want future
changes.

Our own session ID arrives in `PR_RESULT_PARAMETERS` under `PR_NAME_SESSION_ROOT`, formatted
`/<hostname>/<sessionID>` — split on the last `/`.

**Keepalive:** if nothing has been sent for 5 minutes, send `PR_COMMAND_NOOP`.

## 5. Chat

Outgoing:

```
what = 2 (NET_CLIENT_NEW_CHAT_TEXT)
   String PR_NAME_KEYS = "/*/<targetSessionID>/beshare"   ("*" for everyone)
   String "session"    = our session ID   (the server overwrites this)
   String "text"       = the message
   Bool   "private"    = true             (present only for private messages)
```

The routing is done entirely by `PR_NAME_KEYS`: the server pattern-matches it against its node tree
and relays the message to matching sessions. There is no chat "channel" concept.

Incoming chat is the same message with `"session"` set to the sender. `/me` actions are conveyed as
ordinary text beginning with `"/me "` — the receiver formats them; there is no separate code.

**Ping/pong** (`what=5`/`6`) carry `"session"`, `Int64 "when"`, and in the reply `"version"`,
`Int64 "uptime"` and `Int64 "onlinetime"`. Answering pings is expected client behaviour, not
optional.

## 6. Queries

```
PR_COMMAND_SETPARAMETERS {
   Bool "SUBSCRIBE:/*/<sessionExp>/beshare/<fi*|files>/<fileExp>" = true
}
PR_COMMAND_PING { Int32 "count" = n }
```

`fi*` matches both `files` and `fires`; a firewalled client subscribes only to `files` because it
cannot reach other firewalled peers anyway. The `PR_RESULT_PONG` with a matching `count` marks the
end of the *initial* result sweep — after that, results keep arriving live as other users share
things. Expressions are MUSCLE glob patterns (`regex/StringMatcher`), not regular expressions.

Cancel with `PR_COMMAND_REMOVEPARAMETERS { PR_NAME_KEYS = "SUBSCRIBE:*beshare/fi*" }` plus
`PR_COMMAND_JETTISONRESULTS { PR_NAME_KEYS = "beshare/fi*/*" }` to drop results already queued
server-side.

## 7. Shared-file node contents

```
beshare/files/<filename> = Message {
   Int64  "beshare:File Size"
   Int32  "beshare:Modification Time"      unix time
   String "beshare:Path"                   sub-path below the share root
   String "beshare:Kind"                   MIME type
   …arbitrary extra fields become display columns…
}
```

On Haiku the extra fields are BFS attributes pulled from the file's MIME type definition
(`Audio:Bitrate`, `MAIL:subject`, and so on), plus a `besharez:Vector Icon` HVIF blob. The
`besharez:` prefix means "don't create a display column for this".

Linux side: **display these when peers send them; publish only the four basics.** Linux xattrs are
capped around 4 KB and are effectively never populated, so there is nothing to harvest.

## 8. Peer-to-peer file transfer

Transfers never touch the server. The downloader opens a direct TCP connection to the host and port
the uploader advertised in its `beshare/name` node, and both sides speak flattened `Message`s over
the same `MessageIOGateway` framing.

```
downloader → uploader   TRANSFER_COMMAND_PEER_ID   { "beshare:FromSession", "beshare:FromUserName" }
downloader → uploader   TRANSFER_COMMAND_FILE_LIST { "beshare:File Name"[], "beshare:Path"[],
                                                     Int64 "offset"[], Int64 "maxbytes"[] }
uploader   → downloader TRANSFER_COMMAND_FILE_HEADER { "beshare:File Name", Int64 "beshare:File Size",
                                                     "beshare:FromSession", "beshare:Path",
                                                     "beshare:Kind",
                                                     Int64 "beshare:StartOffset"  (resume),
                                                     Int64 "beshare:SendLength"   (byte range) }
uploader   → downloader TRANSFER_COMMAND_FILE_DATA  { "data" (B_RAW_TYPE), Int32 "chk", Int32 "mm" } × N
```

- **`chk`** is a checksum over the chunk. TCP should make this unnecessary; BeShare added it because
  users kept hitting corrupted resumes anyway. Honour it — mismatch aborts the session.
- **`mm`** is the munge mode: `0` = plain, `1` = every byte XOR `0xFF`. It exists to defeat
  middleboxes that filter on content. Both directions must agree.
- **`TRANSFER_COMMAND_NOTIFY_QUEUED`** tells the downloader it is on the uploader's wait list.
- **`TRANSFER_COMMAND_REJECTED`** carries `Int64 "timeleft"` for a ban.
- Resume validity is confirmed by hashing the first `NUM_PARTIAL_HASH_BYTES` (64 KB) of the local
  partial file and comparing with the uploader — gated on the peer's `supports_partial_hashing`.

### Connect-back, for firewalled uploaders

```
what = 3 (NET_CLIENT_CONNECT_BACK_REQUEST)
   String PR_NAME_KEYS = "/*/<peerSessionID>/beshare"
   String "session"    = our session ID
   Int32  "port"       = the port we are now listening on
   Bool   "use_ssl"    = optional
```

Relayed by the server. The firewalled peer then makes the outbound TCP connection, and the roles are
inverted at the TCP level while staying the same at the protocol level.

**Two firewalled peers can never transfer to each other.** That is a protocol-level limitation and
the UI should say so plainly rather than appearing to hang.

## 9. Things that look like protocol but are not

- `installid` is used for per-machine upload bans and queue fairness, not identity or auth.
- There is **no authentication of any kind**. Session IDs are assigned by the server and any client
  can claim any name. Treat every field from a peer as untrusted input — especially file names and
  paths from `FILE_HEADER`, which must be sanitised before they touch the filesystem (`..`,
  absolute paths, embedded separators).
