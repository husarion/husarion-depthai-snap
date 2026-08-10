#!/usr/bin/env python3

import sys
import os
from datetime import datetime, timezone
from jinja2 import Environment, FileSystemLoader

def render_template(template_path, output_path, context):
    env = Environment(loader=FileSystemLoader(os.path.dirname(template_path)))
    template = env.get_template(os.path.basename(template_path))

    with open(output_path, 'w') as f:
        f.write(template.render(context))

if __name__ == "__main__":
    template_path = sys.argv[1]
    output_path = sys.argv[2]
    context = {
        'ros_distro': os.getenv('ROS_DISTRO'),
        # BUILD_DATE is set once per CI run so the amd64 and arm64 jobs of one
        # release get the same snap version even if the build crosses midnight.
        'build_date': os.getenv('BUILD_DATE') or datetime.now(timezone.utc).strftime('%Y%m%d'),
        # 'core_version': os.getenv('CORE_VERSION')
    }

    render_template(template_path, output_path, context)
