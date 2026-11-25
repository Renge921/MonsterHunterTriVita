#pragma once

// One diagnostic line that survives a release run on the device.
//
// On the Vita the line is appended to ux0:/data/mh3-wii/mh3.log with the file
// API and the card is synced, so it is on the card while the game still runs.
// stderr is the same file, but its writes sit in the driver's cache until the
// process closes the stream: a log pulled mid-run stops after the first
// cluster, which hides every line written once the game has started.
// Elsewhere the line goes to stderr.
#ifdef MH3_DEVELOPMENT
void MH3DiagLog(const char* format, ...) __attribute__((format(printf, 1, 2)));
#else
inline void MH3DiagLog(const char*, ...) {}
#endif
