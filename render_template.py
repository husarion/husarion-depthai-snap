#!/usr/bin/env python3

import argparse
import os
import sys
from datetime import datetime, timezone

import yaml
from jinja2 import Environment, FileSystemLoader

# jazzy only — humble support dropped (2026-08). Kept as a tuple so `check()` stays
# a loop, in case a future distro (e.g. "lyrical") gets added the same way.
ROS_DISTROS = ("jazzy",)

# snapd and craft-application enforce these, but only at `snap pack --check-skeleton`,
# i.e. ten minutes into a build. Verified by snapd's own rejection message
# ("description can have up to 4096 codepoints") and craft_application's summary limit.
FIELD_LIMITS = {
    "description": 4096,
    "summary": 78,
}


def render(template_path, ros_distro, build_date=None):
    env = Environment(loader=FileSystemLoader(os.path.dirname(template_path) or "."))
    template = env.get_template(os.path.basename(template_path))
    context = {
        "ros_distro": ros_distro,
        # BUILD_DATE is set once per CI run so the amd64 and arm64 jobs of one
        # release get the same snap version even if the build crosses midnight.
        "build_date": build_date or datetime.now(timezone.utc).strftime("%Y%m%d"),
    }
    return template.render(context)


def check(template_path):
    """Render for every distro and report what snapcraft would reject. Writes nothing."""
    problems = []
    for ros_distro in ROS_DISTROS:
        try:
            # Fixed date: this only validates description/summary length and YAML
            # shape, so a real BUILD_DATE would just make output non-reproducible.
            snapcraft = yaml.safe_load(render(template_path, ros_distro, build_date="00000000"))
        except Exception as e:  # jinja or yaml — either way the build cannot start
            problems.append(f"{ros_distro}: does not render to valid YAML: {e}")
            continue
        for field, limit in FIELD_LIMITS.items():
            length = len(snapcraft.get(field) or "")
            if length > limit:
                problems.append(
                    f"{ros_distro}: '{field}' is {length} codepoints, snapcraft allows "
                    f"{limit} — trim it in the template, the README is the place for "
                    f"the long form"
                )
    for problem in problems:
        print(f"{template_path}: {problem}", file=sys.stderr)
    return 1 if problems else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="validate only, write nothing")
    parser.add_argument("template")
    parser.add_argument("output", nargs="?")
    args = parser.parse_args()

    if args.check:
        return check(args.template)

    if not args.output:
        parser.error("an output path is required unless --check is given")

    ros_distro = os.getenv("ROS_DISTRO")
    rendered = render(args.template, ros_distro, build_date=os.getenv("BUILD_DATE"))

    snapcraft = yaml.safe_load(rendered)
    for field, limit in FIELD_LIMITS.items():
        length = len(snapcraft.get(field) or "")
        if length > limit:
            sys.exit(
                f"'{field}' is {length} codepoints, snapcraft allows {limit}. "
                f"Trim it in the template; the README is the place for the long form."
            )

    with open(args.output, "w") as f:
        f.write(rendered)
    return 0


if __name__ == "__main__":
    sys.exit(main())
