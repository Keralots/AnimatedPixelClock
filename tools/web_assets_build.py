"""PlatformIO pre-build step: generate src/web/web_assets.h from web/.

The header is not committed, so every build makes it from the portal sources
with tools/web_assets_gen.py. The generator also refuses a page the firmware
cannot fill, and that fails the build.

`pio run -t clean` is left alone: a tree that cannot build should still be able
to throw its build output away.
"""

import subprocess

Import("env")  # noqa: F821 - PlatformIO injects this

if not env.GetOption("clean"):  # noqa: F821
    result = subprocess.run(
        [env.subst("$PYTHONEXE"), "tools/web_assets_gen.py"],  # noqa: F821
        cwd=env["PROJECT_DIR"],  # noqa: F821
    )
    if result.returncode != 0:
        env.Exit(1)  # noqa: F821 - the generator has already said what is wrong
