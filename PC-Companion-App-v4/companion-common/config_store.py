"""Config file safety net shared by the Windows and Linux cores."""

import os
import shutil
import time


def quarantine_unreadable_config(path, err):
    """Copy a config that will not parse to a dated sidecar. Returns its path.

    Without this the next save silently overwrites the damaged file and the
    user's whole setup is gone with no copy anywhere.
    """
    stamp = time.strftime("%Y%m%d-%H%M%S")
    dst = "%s.corrupt-%s.json" % (os.path.splitext(path)[0], stamp)
    try:
        shutil.copy2(path, dst)
        kept = True
    except Exception:
        kept = False
    print("")
    print("!" * 60)
    print("  CONFIGURATION COULD NOT BE READ")
    print("!" * 60)
    print("  %s" % path)
    print("  %s" % err)
    if kept:
        print("  A copy was kept as:")
        print("    %s" % dst)
        print("  Starting with defaults. Your old settings are in that copy.")
    else:
        print("  The file could NOT be copied aside - do not save until you")
        print("  have backed it up yourself, or the settings will be lost.")
    print("!" * 60)
    print("")
    return dst if kept else ""
