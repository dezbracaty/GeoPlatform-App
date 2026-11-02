"""Real directory links without requiring Windows symbolic-link privilege."""
import os
import subprocess


def directory_link(link, target):
    if os.name == 'nt':
        subprocess.run(['cmd', '/c', 'mklink', '/J', str(link), str(target)],
                       check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if not link.is_junction():
            raise AssertionError('Fixture did not create a real NTFS junction')
    else:
        link.symlink_to(target, target_is_directory=True)
