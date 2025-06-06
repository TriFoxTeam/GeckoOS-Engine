# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os


def toolchain_task_definitions():
    # triggers override of the `graph_config_schema` noqa
    import gecko_taskgraph  # noqa
    from taskgraph.generator import load_tasks_for_kind

    # Don't import globally to allow this module being imported without
    # the taskgraph module being available (e.g. standalone js)
    params = {"level": os.environ.get("MOZ_SCM_LEVEL", "3"), "files_changed": []}
    root_dir = os.path.join(os.path.dirname(__file__), "..", "..", "..", "taskcluster")
    toolchains = load_tasks_for_kind(params, "toolchain", root_dir=root_dir)
    aliased = {}
    for t in toolchains.values():
        aliases = t.attributes.get("toolchain-alias")
        if not aliases:
            aliases = []
        if isinstance(aliases, str):
            aliases = [aliases]
        for alias in aliases:
            aliased[f"toolchain-{alias}"] = t
    toolchains.update(aliased)

    return toolchains
