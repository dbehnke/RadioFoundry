#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 David Behnke
"""Build and package all registry images with the private kf8sfork label. Other branches refuse this wrapper."""
import argparse
from datetime import datetime
import fcntl
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'rfoundry'))
from rfoundry import boards  # noqa: E402


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()


def require_branch(branch):
    if branch != 'kf8sfork':
        raise ValueError('private version labels require branch kf8sfork')


def next_sequence(path, day):
    """Reserve once per complete build attempt, shared across worktrees and protected against concurrent builders."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.with_suffix('.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        saved = json.loads(path.read_text()) if path.exists() else {}
        sequence = saved.get('sequence', 0) + 1 if saved.get('day') == day else 1
        descriptor, temporary = tempfile.mkstemp(prefix=path.name, dir=path.parent)
        try:
            with os.fdopen(descriptor, 'w') as stream:
                json.dump({'day': day, 'sequence': sequence}, stream)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, path)
        finally:
            Path(temporary).unlink(missing_ok=True)
        return sequence



def run_logged(args, log):
    with log.open('w') as stream:
        subprocess.run(args, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, check=True)
    if re.search(r'\bwarning:', log.read_text(), re.I):
        raise ValueError('build warnings: ' + str(log))


def self_test():
    require_branch('kf8sfork')
    for branch in ('main', 'feat/p25-contact-lookup', 'feat/audio-rx-agc', 'HEAD'):
        try:
            require_branch(branch)
        except ValueError:
            pass
        else:
            raise AssertionError(branch)
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'sequence.json'
        assert next_sequence(path, '261123') == 1
        assert next_sequence(path, '261123') == 2
        assert next_sequence(path, '261124') == 1
        from unittest.mock import patch
        with patch('os.replace', side_effect=OSError('interrupted update')):
            try:
                next_sequence(path, '261124')
            except OSError:
                pass
            else:
                raise AssertionError('interruption did not reach the reservation')
        assert next_sequence(path, '261124') == 2
        path.write_text('')
        try:
            next_sequence(path, '261124')
        except ValueError:
            pass
        else:
            raise AssertionError('an empty counter must fail closed')
    print('kf8s build: branch guards and daily sequence passed')


def require_release_ready():
    # Both legacy branches used layout 1 / 412 bytes for different fields. Do not publish this integration snapshot.
    raise ValueError('kf8sfork packaging is blocked pending legacy AGC/AES settings migration; '
                     'see docs/spec/settings-store.md, Receive audio preferences')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--jobs', type=int, default=8)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    require_branch(git('branch', '--show-current'))
    require_release_ready()
    if git('status', '--porcelain', '--untracked-files=normal'):
        raise ValueError('commit or preserve pending changes before building')
    subprocess.run(['git', 'fetch', 'origin', 'main'], cwd=ROOT, check=True)
    subprocess.run(['git', 'merge-base', '--is-ancestor', 'origin/main', 'HEAD'], cwd=ROOT, check=True)
    upstream_full = git('rev-parse', 'origin/main')
    commit_full = git('rev-parse', 'HEAD')
    upstream, commit = upstream_full[:8], commit_full[:8]
    day = datetime.now(ZoneInfo('America/Detroit')).strftime('%y%m%d')
    common = Path(git('rev-parse', '--git-common-dir'))
    if not common.is_absolute():
        common = ROOT / common
    sequence = next_sequence(common / 'kf8s-build-sequence.json', day)
    version = f'kf8s-{upstream}-{commit}-{day}-{sequence}'
    out = ROOT / 'build' / 'kf8s' / version
    package = out / 'package'
    package.mkdir(parents=True)
    print(version, flush=True)
    images = []
    for board in boards.BOARDS.values():
        choices = [[(option['name'], value['id']) for value in option['values']] for option in board.build_options]
        for values in itertools.product(*choices):
            options = dict(values)
            name = board.release_id(options)
            build = out / name
            flags = [f'BOARD={board.id}', f'BUILD={build}', f'RFOUNDRY_RELEASE={version}',
                     'BUILD_SOURCE=fork', 'BUILD_SOURCE_REPO=dbehnke/RadioFoundry']
            flags += [f'{key}={value}' for key, value in values]
            print('building ' + name, flush=True)
            run_logged(['make', f'-j{args.jobs}', *flags, 'all', 'stack-check'], out / (name + '.log'))
            image = build / Path(board.build_image(options)).name
            release = [sys.executable, 'tools/release.py', '--board', board.id, '--version', version,
                       '--bin', str(image), '--out', str(package)]
            for key, value in values:
                release += ['--build-option', f'{key}={value}']
            subprocess.run(release, cwd=ROOT, check=True)
            images.append({'board': board.id, 'options': options, 'image': image.name})
    require_branch(git('branch', '--show-current'))
    if (git('rev-parse', 'HEAD') != commit_full or git('status', '--porcelain') or
            git('rev-parse', 'origin/main') != upstream_full):
        raise ValueError('source changed during the build; refusing to package')
    metadata = {'version': version, 'upstream': upstream_full,
                'commit': commit_full, 'images': images}
    (package / 'build.json').write_text(json.dumps(metadata, indent=2) + '\n')
    (package / 'README.txt').write_text(
        f'{version}\nPrivate KF8S test build based on RadioFoundry. Contact: dbehnke (KF8S).\n'
        'Includes receive audio AGC and optional P25 caller lookup.\n'
        'Enable P25 contact lookup under Settings > Talk & listen (default Off).\n'
        'Match the image and its band/power option to your hardware; see the board reference.\n'
        'Use each image with its JSON manifest in the RadioFoundry web flasher.\n'
        'Images contain no AMBE codec. Follow the flasher instructions to preserve or supply your codec.\n'
        'P25 remains unavailable where upstream has not implemented it; see the feature matrix.\n'
        'Keep LICENSE, AUTHORS, CONTRIBUTORS and third-party notices with redistributed downloads.\n')
    for name in ('LICENSE', 'AUTHORS', 'CONTRIBUTORS'):
        shutil.copy2(ROOT / name, package / name)
    shutil.copytree(ROOT / 'LICENSES', package / 'LICENSES')
    archive = out / f'radiofoundry-{version}.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as zipped:
        for path in sorted(package.rglob('*')):
            if path.is_file():
                zipped.write(path, path.relative_to(package))
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix('.zip.sha256').write_text(f'{digest}  {archive.name}\n')
    print(f'ZIP: {archive}\nSHA256: {digest}')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
