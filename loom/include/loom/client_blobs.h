// loom/client_blobs.h — the bytes a client brings to the kernel or takes from it (P14 6). Media never travel inside JSON:
//
//   upload    a file in the data directory's upload folder (<data>/tmp/uploads: loom-server streams a multipart upload there, the native
//             shell writes there) is moved into the blob store by `put`; or, where the transport has no file (the Android WebView
//             bridge), `begin` / `append` / `commit` build that file from chunks - the only place bytes are base64, one bounded chunk at a time.
//   download  `path` gives the stored file to the server, which streams it (GET /api/client/blob/{hash}/raw); `read` gives a chunk
//             to a transport that cannot (offset, length).
//   result    actions.client_result carries only `blob_hash` for media; `parts` checks the hash against THIS user's blob store and records the
//             graph node and the artifact that say where the blob came from. A part with `data_base64` is refused.
//
// These actions are called through loom_invoke("actions", ...) and have no HTTP route of their own in the action table: the server's routes
// call them. Nothing here takes a path from a request except `put`, which accepts only a file inside the upload folder.
#pragma once

#include <string>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
class Runtime;
}

namespace loom::actions::client_blobs {

// client.blob_put {path, mime?} -> {hash, size, mime}. `path` must be a regular file directly inside <data>/tmp/uploads (a link or a
// file elsewhere is refused); it is stored in the blob store and the upload file is removed.
Result<Json> put(Runtime& rt, const Json& request);
// client.blob_path {hash} -> {hash, size, mime, path}: the file of a blob of this user's store (fetched into the local layout first when
// only another storage target holds it). For the server to stream; the path is not for clients.
Result<Json> path(Runtime& rt, const Json& request);
// client.blob_begin {} -> {upload_id}; client.blob_append {upload_id, data_base64} -> {upload_id, size};
// client.blob_commit {upload_id, mime?, abort?} -> {hash, size, mime} (abort: the upload is dropped).
Result<Json> begin(Runtime& rt, const Json& request);
Result<Json> append(Runtime& rt, const Json& request);
Result<Json> commit(Runtime& rt, const Json& request);
// client.blob_read {hash, offset?, length?} -> {hash, size, mime, offset, length, eof, data_base64}: one chunk (preset length 262144).
Result<Json> read(Runtime& rt, const Json& request);

// The parts a client brought back with actions.client_result, as typed parts of the run: media name a stored blob (`blob_hash`); text stays
// text. `origin` says which step of which run asked for them (kept on the graph node and the artifact).
Result<Json> parts(Runtime& rt, const Json& parts, const Json& origin);

}  // namespace loom::actions::client_blobs
