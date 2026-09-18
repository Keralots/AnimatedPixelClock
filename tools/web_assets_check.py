"""PlatformIO pre-build step: refuse to build a stale src/web/web_assets.h.

The portal's CSS, JS and icon are gzipped into that header by
tools/web_assets_gen.py. Editing the sources in web_pages.h without rerunning
the generator would ship a page that disagrees with the repository, and nothing
would say so - which is why this runs before every build.

`pio run -t clean` is left alone: a tree that cannot build should still be able
to throw its build output away.
"""

import subprocess

Import("env")  # noqa: F821 - PlatformIO injects this

if not env.GetOption("clean"):  # noqa: F821
    result = subprocess.run(
        [env.subst("$PYTHONEXE"), "tools/web_assets_gen.py", "--check"],  # noqa: F821
        cwd=env["PROJECT_DIR"],  # noqa: F821
    )
    if result.returncode != 0:
        env.Exit(1)  # noqa: F821 - the generator has already said what is wrong
