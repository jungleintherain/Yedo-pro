#ifndef STORY_UPDATER_H
#define STORY_UPDATER_H

namespace ygo {
// Self update for the story mod (Windows).
// If ./story/_update/manifest.json exists, the parts listed in it are joined into a new
// executable, the running exe is renamed to *.old, the new one takes its name and the game restarts.
// Returns true when the caller must exit because a new process was started.
bool ApplyStoryUpdate();
}

#endif
