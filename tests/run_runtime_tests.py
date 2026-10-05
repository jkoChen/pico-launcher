"""Build real runtime/adapters on GitHub; replace only DS hardware/UI dependencies.

The staged production sources are copied byte-for-byte. No C++ source rewriting
is used. Run the same tests on the shipped pixel-font revision to demonstrate
that they detect the regressions, then on the fixed working tree under ASan/UBSan.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASELINE = '1ddeb677a5a36b7c3869379a2fdc011899bcc967'
SOURCES = [
    'core/RefCount.h', 'core/SharedPtr.h', 'core/SharedPtr.cpp',
    'core/WeakPtr.h', 'core/WeakPtr.cpp', 'core/EnableSharedFromThis.h',
    'core/LinkedList.h', 'core/LinkedListLink.h', 'core/BitVector.h',
    'core/task/Task.h', 'core/task/Task.cpp', 'core/task/TaskState.h',
    'core/task/TaskResult.h', 'core/task/TaskQueue.h', 'core/task/TaskQueue.cpp',
    'gui/views/RecyclerAdapter.h',
    'romBrowser/FileRecyclerAdapter.h', 'romBrowser/FileRecyclerAdapter.cpp',
    'romBrowser/viewModels/RomBrowserItemViewModel.h',
    'romBrowser/viewModels/RomBrowserItemViewModel.cpp',
    'romBrowser/DisplayMode/BannerListFileRecyclerAdapter.h',
    'romBrowser/DisplayMode/BannerListFileRecyclerAdapter.cpp',
]
STUB_HEADERS = [
    'romBrowser/FileInfoManager.h', 'romBrowser/IRomBrowserController.h',
    'romBrowser/views/BannerListItemView.h', 'romBrowser/Theme/IRomBrowserViewFactory.h',
    'romBrowser/viewModels/RomBrowserViewModel.h', 'romBrowser/FileType/Nds/NdsFileType.h',
]
CASES = ['weak-reset', 'pending-queue', 'completion-ownership', 'row-reuse', 'immediate-identity']


def build(destination, baseline=False):
    shutil.copytree(ROOT / 'tests/runtime_stubs', destination)
    for name in SOURCES:
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if baseline:
            data = subprocess.check_output(['git', 'show', f'{BASELINE}:arm9/source/{name}'], cwd=ROOT)
        else:
            data = (ROOT / 'arm9/source' / name).read_bytes()
        target.write_bytes(data)
    for name in STUB_HEADERS:
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('#pragma once\n#include "browser.h"\n')
    binary = destination / 'runtime_tests'
    subprocess.run([
        'g++', '-m32', '-std=c++23', '-g', '-O1', '-pthread', '-no-pie',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-I', str(destination), '-include', str(destination / 'common.h'),
        str(ROOT / 'tests/runtime_tests.cpp'),
        *(str(destination / name) for name in SOURCES if name.endswith('.cpp')),
        '-o', str(binary),
    ], check=True)
    return binary


def run(binary, case, baseline=False):
    environment = os.environ | {'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1',
                                'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'}
    try:
        result = subprocess.run([str(binary), case], capture_output=True, text=True,
                                timeout=10 if baseline else 30, env=environment)
    except subprocess.TimeoutExpired:
        if baseline and case == 'weak-reset':
            print('CONFIRMED baseline failure: weak-reset does not return', flush=True)
            return
        raise
    if baseline:
        assert result.returncode != 0, f'Baseline unexpectedly passed {case}'
        output = result.stderr
        assert any(marker in output for marker in ('CHECK FAILED:', 'AddressSanitizer', 'runtime error:')), output
        print(f'CONFIRMED baseline failure: {case}\n{output[:2400]}', flush=True)
    else:
        print(result.stdout, end='', flush=True)
        if result.returncode:
            print(result.stderr, flush=True)
            raise RuntimeError(f'{case} failed: {result.returncode}')


with tempfile.TemporaryDirectory(prefix='pico-runtime-') as directory:
    directory = Path(directory)
    baseline = build(directory / 'baseline', baseline=True)
    for case in CASES:
        run(baseline, case, baseline=True)
    fixed = build(directory / 'fixed')
    for case in CASES:
        run(fixed, case)
