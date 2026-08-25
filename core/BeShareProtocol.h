/*
 * Copyright 2026, Kevin Adams <kevinadams05@gmail.com>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef BESHARE_PROTOCOL_H
#define BESHARE_PROTOCOL_H


#include "util/String.h"


// Every constant in this header is dictated by the BeShare wire protocol as it has
// been spoken since 1999.  None of it may be changed to taste: a mismatch here means
// we silently fail to interoperate with the Haiku clients we exist to talk to.
//
// The authoritative references are BeShare's ShareNetClient.cpp, MUSCLE's own
// tools/chatclient.cpp, and lang/python3/python_chat.py.  See docs/PROTOCOL.md.

namespace hitux {


// The default port a muscled server listens on.
const uint16 kDefaultServerPort = 2960;

// Default port range BeShare peers listen on for incoming file transfers.
const uint16 kDefaultTransferPort = 7000;
const uint16 kTransferPortRange = 50;

// How many bytes of a partial download get hashed to validate a resume.
const uint32 kPartialHashByteCount = 64 * 1024;

// If we have sent the server nothing for this long, send a PR_COMMAND_NOOP so that
// neither side decides the connection has gone away.
const uint64 kServerKeepAliveMicroseconds = 5 * 60 * 1000000ULL;


// 'what' codes BeShare defines for its own messages.  These travel through the
// server untouched -- muscled just pattern-matches PR_NAME_KEYS and relays them.
enum
{
	NET_CLIENT_CONNECTED_TO_SERVER = 0,
	NET_CLIENT_DISCONNECTED_FROM_SERVER,
	NET_CLIENT_NEW_CHAT_TEXT,
	NET_CLIENT_CONNECT_BACK_REQUEST,
	NET_CLIENT_CHECK_FILE_COUNT,
	NET_CLIENT_PING,
	NET_CLIENT_PONG,
	NET_CLIENT_SCAN_THREAD_REPORT
};


// Depth of each clause in a server node path, e.g. for
// "/198.51.100.4/1308/beshare/files/holiday.jpg":
//
//   depth 0 = ""              (root)
//   depth 1 = "198.51.100.4"  (host name)
//   depth 2 = "1308"          (session ID)
//   depth 3 = "beshare"       (our subtree, so we do not collide with other apps)
//   depth 4 = "files"         (or name / userstatus / filecount / bandwidth / fires)
//   depth 5 = "holiday.jpg"   (shared file name)
enum
{
	ROOT_DEPTH = 0,
	HOST_NAME_DEPTH,
	SESSION_ID_DEPTH,
	BESHARE_HOME_DEPTH,
	USER_NAME_DEPTH,
	FILE_INFO_DEPTH
};


// Node paths we publish under our own session, relative to our session root.
#define BESHARE_NODE_NAME          "beshare/name"
#define BESHARE_NODE_USER_STATUS   "beshare/userstatus"
#define BESHARE_NODE_FILE_COUNT    "beshare/filecount"
#define BESHARE_NODE_BANDWIDTH     "beshare/bandwidth"
#define BESHARE_NODE_UPLOAD_STATS  "beshare/uploadstats"

// Shared files live under "files" when we can accept incoming connections and under
// "fires" when we cannot.  That single letter is the entire firewall signalling
// mechanism: a downloader that sees "fires" knows it must ask us to connect back.
#define BESHARE_NODE_FILES_PATH          "beshare/files/"
#define BESHARE_NODE_FIREWALLED_PATH     "beshare/fires/"

// The subscription that gets us every user's published state on the server.
#define BESHARE_SUBSCRIBE_ALL  "SUBSCRIBE:beshare/*"


// Field names inside the "beshare/name" node.
#define BESHARE_FIELD_NAME                "name"
#define BESHARE_FIELD_PORT                "port"
#define BESHARE_FIELD_INSTALL_ID          "installid"
#define BESHARE_FIELD_VERSION_NAME        "version_name"
#define BESHARE_FIELD_VERSION_NUMBER      "version_num"
#define BESHARE_FIELD_LEGACY_VERSION      "version"
#define BESHARE_FIELD_BOT                 "bot"
#define BESHARE_FIELD_SUPPORTS_PARTIAL_HASHING  "supports_partial_hashing"
#define BESHARE_FIELD_SUPPORTS_SSL        "supports_ssl"
#define BESHARE_FIELD_SUPPORTS_RANGES     "supports_ranges"

// Field names in the other published nodes.
#define BESHARE_FIELD_USER_STATUS         "userstatus"
#define BESHARE_FIELD_FILE_COUNT          "filecount"
#define BESHARE_FIELD_BANDWIDTH_LABEL     "label"
#define BESHARE_FIELD_BANDWIDTH_BITS      "bps"
#define BESHARE_FIELD_UPLOADS_CURRENT     "cur"
#define BESHARE_FIELD_UPLOADS_MAX         "max"

// Field names inside a shared-file node.  The space and capitalisation are part
// of the wire format, not a style choice -- they date to BeOS attribute names.
#define BESHARE_FIELD_FILE_NAME           "beshare:File Name"
#define BESHARE_FIELD_FILE_SIZE           "beshare:File Size"
#define BESHARE_FIELD_MODIFICATION_TIME   "beshare:Modification Time"
#define BESHARE_FIELD_PATH                "beshare:Path"
#define BESHARE_FIELD_KIND                "beshare:Kind"

// Peer-to-peer transfer message types.  Based at 'tshr' and ordered exactly as
// BeShare declares them -- the numbering is the wire contract, so nothing may be
// inserted into the middle of this list.
enum
{
	TRANSFER_COMMAND_CONNECTED_TO_PEER = 'tshr',
	TRANSFER_COMMAND_DISCONNECTED_FROM_PEER,
	TRANSFER_COMMAND_FILE_LIST,
	TRANSFER_COMMAND_FILE_HEADER,
	TRANSFER_COMMAND_FILE_DATA,
	TRANSFER_COMMAND_DEPRECATED,
	TRANSFER_COMMAND_NOTIFY_QUEUED,
	TRANSFER_COMMAND_MD5_SEND_READ_DONE,
	TRANSFER_COMMAND_MD5_RECV_READ_DONE,
	TRANSFER_COMMAND_PEER_ID,
	TRANSFER_COMMAND_REJECTED
};


// How file data may be disguised in transit, to get past middleboxes that filter
// on content. The requester states a preference; the sender stamps each chunk
// with what it actually did.
enum
{
	MUNGE_MODE_NONE = 0,
	MUNGE_MODE_XOR,

	NUM_MUNGE_MODES
};


// Fields in a transfer session's messages.
#define BESHARE_FIELD_FROM_SESSION        "beshare:FromSession"
#define BESHARE_FIELD_FROM_USER_NAME      "beshare:FromUserName"
#define BESHARE_FIELD_FILE_LIST_NAMES     "files"
#define BESHARE_FIELD_FILE_LIST_OFFSETS   "offsets"
#define BESHARE_FIELD_FILE_LIST_MD5       "md5"
#define BESHARE_FIELD_MUNGE_MODE          "mm"
#define BESHARE_FIELD_CHECKSUM            "chk"
#define BESHARE_FIELD_DATA                "data"
#define BESHARE_FIELD_START_OFFSET        "beshare:StartOffset"
#define BESHARE_FIELD_SEND_LENGTH         "beshare:SendLength"
#define BESHARE_FIELD_TIME_LEFT           "timeleft"


// Prefix marking a field that carries data but should not become a display
// column, e.g. "besharez:Vector Icon".
#define BESHARE_HIDDEN_FIELD_PREFIX       "besharez:"


// Field names in chat and ping messages.
#define BESHARE_FIELD_SESSION             "session"
#define BESHARE_FIELD_TEXT                "text"
#define BESHARE_FIELD_PRIVATE             "private"
#define BESHARE_FIELD_WHEN                "when"
#define BESHARE_FIELD_UPTIME              "uptime"
#define BESHARE_FIELD_ONLINE_TIME         "onlinetime"


// Prefix that marks an action ("/me waves") inside an ordinary chat message.  There
// is no separate 'what' code for actions -- the receiver looks for this prefix.
#define BESHARE_ACTION_PREFIX  "/me "


/** Builds the subscription path for a file query.
  *
  * (sessionExpression) and (fileExpression) are MUSCLE glob patterns, not
  * regular expressions.  A firewalled client subscribes only to "files" because
  * it cannot reach other firewalled peers anyway; everyone else uses "fi*" to
  * match both "files" and "fires".
  *
  * @param sessionExpression which users to search, "*" for everyone
  * @param fileExpression which file names to match
  * @param isFirewalled true iff we cannot accept incoming connections
  */
inline muscle::String
MakeQuerySubscriptionPath(const muscle::String& sessionExpression,
	const muscle::String& fileExpression, bool isFirewalled)
{
	muscle::String path("SUBSCRIBE:/*/");
	path += sessionExpression;
	path += "/beshare/";
	path += isFirewalled ? "files/" : "fi*/";
	path += fileExpression;
	return path;
}


/** Builds the PR_NAME_KEYS routing path that sends a message to one session, or to
  * every BeShare session on every host when (sessionId) is "*".
  *
  * The server does all the routing by pattern-matching this string against its node
  * tree, which is why chat has no channel concept: the pattern *is* the audience.
  *
  * @param sessionId the session ID to target, or "*" for everyone
  * @returns a path of the form "/&#42;/&lt;sessionId&gt;/beshare"
  */
inline muscle::String
MakeSessionTargetPath(const muscle::String& sessionId)
{
	muscle::String path("/*/");
	path += sessionId;
	path += "/beshare";
	return path;
}


}  // namespace hitux


#endif  // BESHARE_PROTOCOL_H
