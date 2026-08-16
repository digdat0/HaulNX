#ifndef RAR3_H
#define RAR3_H

#include "extract.h" /* extract_cb */

#ifdef __cplusplus
extern "C" {
#endif

/* Fallback RAR3 extractor for archives libarchive's RAR reader can't finish:
 * entries that invoke one of RAR3's "programmable filter" transforms
 * (x86 executable address delta, audio prediction, RGB delta, a generic
 * byte delta). libarchive has never implemented that filter's bytecode VM
 * and fails those entries outright. This ports the relevant slice of the
 * open-source unarr project's RAR3 decoder into source/rar3/: the base
 * LZSS+PPMd decompression (unavoidable regardless of the filter question --
 * every entry needs it to produce bytes at all) plus the five *known
 * standard* filter programs as small native C functions, matched by a
 * fingerprint of the raw filter bytecode. HaulNX deliberately does not carry
 * a general-purpose bytecode interpreter for arbitrary/custom filter
 * programs -- see source/rar3/filter-rar.c for the reasoning and the exact
 * trim from upstream. Provenance and license (LGPLv3, attributed further
 * upstream to XADMaster/TheUnarchiver) are recorded in
 * source/rar3/UPSTREAM_COMMIT.txt, AUTHORS, and THIRD-PARTY-NOTICES.md.
 *
 * Same contract as extract_archive() in extract.h: same return convention
 * (files extracted, or -1 if `src` isn't a RAR archive this build can read
 * at all, or a file couldn't be written -- extraction incomplete, do not
 * treat as success), same progress callback. Call this only after
 * extract_archive() has already failed on a `.rar` source, as the fallback
 * leg of every archive-drop path: queue.c's install_item() (downloads), the
 * embedded MTP responder (source/mtp/responder.cpp), and the Wi-Fi/companion
 * ROM-receive paths in MainApplication.cpp (RomRecvSaveOne, SortFileRow). An
 * entry using a genuinely custom/unrecognized filter program still fails --
 * same outcome as today, since libarchive can't handle it either -- and the
 * caller's existing "save the raw archive" fallback still applies. */
int rar3_extract(const char *src, const char *dest_dir, extract_cb cb,
                 void *userdata, int *out_overwrites);

#ifdef __cplusplus
}
#endif

#endif /* RAR3_H */
