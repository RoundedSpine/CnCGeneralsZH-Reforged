#!/usr/bin/env python3
"""Publish a bounded build report to a separate, generated Git branch."""
import json
import fcntl
from collections import deque
import os
import re
from pathlib import Path
import subprocess


def git(*args, data=None, check=True):
    return subprocess.run(['git', *args], input=data, text=True, capture_output=True, check=check)


def collect_log(source, progress=False):
    tail = deque(maxlen=300 if progress else 6000)
    context = deque(maxlen=3000)
    previous = deque(maxlen=3)
    remaining = 0
    with source.open(errors='replace') as stream:
        for raw in stream:
            line = raw.rstrip('\n')
            tail.append(line)
            if not progress:
                if re.search(r'FAILED:|\b(?:fatal )?error:|CMake Error|ninja: .*error', line):
                    if not remaining: context.extend(previous)
                    remaining = 16
                if remaining:
                    context.append(line)
                    remaining -= 1
                previous.append(line)
    return '\n'.join(tail) + '\n', '\n'.join(context)[-500000:] + '\n'


def publish():
    git('config', 'user.name', 'github-actions[bot]')
    git('config', 'user.email', '41898282+github-actions[bot]@users.noreply.github.com')
    branch = 'ci/ios-port-logs'
    fetched = git('fetch', '--no-tags', 'origin', branch, check=False)
    parent = git('rev-parse', 'FETCH_HEAD').stdout.strip() if fetched.returncode == 0 else None
    build = Path('build/ios')
    files = {
        'status.json': json.dumps({
            'revision': os.environ['GITHUB_SHA'], 'run_id': os.environ['GITHUB_RUN_ID'],
            'attempt': os.environ['GITHUB_RUN_ATTEMPT'], 'status': os.environ['IOS_JOB_STATUS'],
            'url': f"https://github.com/{os.environ['GITHUB_REPOSITORY']}/actions/runs/{os.environ['GITHUB_RUN_ID']}",
        }, indent=2) + '\n'
    }
    progress = os.environ['IOS_JOB_STATUS'] == 'in_progress'
    for name, source in (
        ('build.log', build / 'build.log'),
        ('ffmpeg-configure.log', build / 'ffmpeg-work/build/configure.log'),
        ('ffmpeg-config.log', build / 'ffmpeg-work/build/ffbuild/config.log'),
        ('cmake-configure.log', build / 'CMakeFiles/CMakeConfigureLog.yaml'),
    ):
        if source.is_file() and (not progress or name == 'build.log'):
            tail, errors = collect_log(source, progress)
            files[name] = tail
            if name == 'build.log' and not progress:
                files['errors.log'] = errors
    entries = []
    for name, content in files.items():
        blob = git('hash-object', '-w', '--stdin', data=content).stdout.strip()
        entries.append(f'100644 blob {blob}\t{name}\n')
    tree = git('mktree', data=''.join(entries)).stdout.strip()
    args = ['commit-tree', tree]
    if parent:
        args += ['-p', parent]
    commit = git(*args, data=f"build: report iOS run {os.environ['GITHUB_RUN_ID']}\n").stdout.strip()
    result = git('push', 'origin', f'{commit}:refs/heads/{branch}', check=False)
    print(result.stderr)
    result.check_returncode()


def main():
    folder = Path('build/ios')
    folder.mkdir(parents=True, exist_ok=True)
    # Progress and final publishers must never race to update the report branch.
    with (folder / 'publish.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        publish()


if __name__ == '__main__':
    main()
