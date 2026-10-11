# SPDX-License-Identifier: Apache-2.0
"""Windows-only bounded diagnostic replay; not a product qualification tool."""
import argparse
from contextlib import contextmanager
import ctypes
from ctypes import wintypes
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time

ENVIRONMENT_NAMES = (
    'SYSTEMROOT', 'WINDIR', 'PATH', 'PATHEXT', 'COMSPEC',
    'VCINSTALLDIR', 'VCTOOLSINSTALLDIR', 'VCTOOLSREDISTDIR', 'VSINSTALLDIR',
    'WINDOWSSDKDIR', 'WINDOWSSDKVERSION', 'VSCMD_ARG_TGT_ARCH', 'VSCMD_ARG_HOST_ARCH')
ASAN_OPTIONS = 'alloc_dealloc_mismatch=1'
EXIT_TIMEOUT = 124
EXIT_CONTROLLER = 125
INVENTORY_TIMEOUT_SECONDS = 15
CLEANUP_TIMEOUT_SECONDS = 5
MODULE_CAPACITY = 2048
MODULE_PATH_CAPACITY = 32768
THREAD_SNAPSHOT = 0x00000004
LIST_MODULES_ALL = 0x03
INVALID_HANDLE = ctypes.c_void_p(-1).value
SEM_FAILCRITICALERRORS = 0x0001
SEM_NOGPFAULTERRORBOX = 0x0002
OS_FAILURE_PRESENTATION_POLICY = 'inherit_failcritical_no_gp_error_box'
PRESERVE_PRESENTATION_POLICY = 'preserve_parent_error_mode'
DEADLINE_ERROR = 'diagnostic timeout must be finite and strictly positive'


def positive_deadline(value):
    try:
        timeout = float(value)
    except (TypeError, ValueError) as error:
        raise ValueError(DEADLINE_ERROR) from error
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError(DEADLINE_ERROR)
    return timeout


def deadline_argument(value):
    try:
        return positive_deadline(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError(DEADLINE_ERROR) from error


@contextmanager
def child_presentation_policy(enabled):
    """Single-threaded spawn only; restore this controller's process mode."""
    if threading.active_count() != 1:
        raise RuntimeError('presentation policy requires a single-threaded controller')
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetErrorMode.argtypes = []
    kernel.GetErrorMode.restype = wintypes.UINT
    kernel.SetErrorMode.argtypes = [wintypes.UINT]
    kernel.SetErrorMode.restype = wintypes.UINT
    before = kernel.GetErrorMode()
    inherited = before | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX if enabled else before
    policy = {'name': OS_FAILURE_PRESENTATION_POLICY if enabled else PRESERVE_PRESENTATION_POLICY,
              'parent_before': before, 'child_inherited': inherited}
    try:
        if enabled:
            kernel.SetErrorMode(inherited)
            if kernel.GetErrorMode() != inherited:
                raise RuntimeError('failed to set diagnostic child presentation policy')
        yield policy
    finally:
        if enabled:
            kernel.SetErrorMode(before)
        policy['parent_restored'] = kernel.GetErrorMode()
        if policy['parent_restored'] != before:
            raise RuntimeError('failed to restore controller presentation policy')


def digest(path):
    path = Path(path).resolve(strict=True)
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': result.hexdigest()}


def save(path, value):
    path.write_text(json.dumps(value, indent=2), encoding='utf-8')


def environment(output):
    inherited = {key.upper(): value for key, value in os.environ.items()}
    child = {key: inherited[key] for key in ENVIRONMENT_NAMES if key in inherited}
    temporary = output/'child-temp'
    temporary.mkdir()
    child.update(TEMP=str(temporary), TMP=str(temporary), ASAN_OPTIONS=ASAN_OPTIONS)
    # No HOME, user profile, GitHub credential, Python setting or full env dump.
    return child


class ThreadEntry(ctypes.Structure):
    _fields_ = [('dwSize', wintypes.DWORD), ('cntUsage', wintypes.DWORD),
                ('th32ThreadID', wintypes.DWORD), ('th32OwnerProcessID', wintypes.DWORD),
                ('tpBasePri', wintypes.LONG), ('tpDeltaPri', wintypes.LONG),
                ('dwFlags', wintypes.DWORD)]


def observe(process):
    """Noninvasive IDs/module metadata, not stacks, wait chains or minidumps."""
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel.Thread32First.argtypes = [wintypes.HANDLE, ctypes.POINTER(ThreadEntry)]
    kernel.Thread32Next.argtypes = [wintypes.HANDLE, ctypes.POINTER(ThreadEntry)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    result = {'pid': process.pid, 'threads': [], 'asan_modules': []}
    snapshot = kernel.CreateToolhelp32Snapshot(THREAD_SNAPSHOT, 0)
    if snapshot == INVALID_HANDLE:
        result['thread_error'] = ctypes.get_last_error()
    else:
        try:
            entry = ThreadEntry()
            entry.dwSize = ctypes.sizeof(entry)
            found = kernel.Thread32First(snapshot, ctypes.byref(entry))
            while found:
                if entry.th32OwnerProcessID == process.pid:
                    result['threads'].append({'tid': entry.th32ThreadID,
                                              'base_priority': entry.tpBasePri})
                found = kernel.Thread32Next(snapshot, ctypes.byref(entry))
        finally:
            kernel.CloseHandle(snapshot)
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    psapi.EnumProcessModulesEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.HMODULE),
                                         wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]
    psapi.EnumProcessModulesEx.restype = wintypes.BOOL
    psapi.GetModuleFileNameExW.argtypes = [wintypes.HANDLE, wintypes.HMODULE,
                                         wintypes.LPWSTR, wintypes.DWORD]
    psapi.GetModuleFileNameExW.restype = wintypes.DWORD
    modules = (wintypes.HMODULE * MODULE_CAPACITY)()
    needed = wintypes.DWORD()
    handle = wintypes.HANDLE(int(process._handle))  # retained own process handle
    if not psapi.EnumProcessModulesEx(handle, modules, ctypes.sizeof(modules),
                                     ctypes.byref(needed), LIST_MODULES_ALL):
        result['module_error'] = ctypes.get_last_error()
    elif needed.value > ctypes.sizeof(modules):
        result['module_error'] = 'module capacity exceeded; not a complete module snapshot'
    else:
        for module in modules[:needed.value // ctypes.sizeof(wintypes.HMODULE)]:
            name = ctypes.create_unicode_buffer(MODULE_PATH_CAPACITY)
            if psapi.GetModuleFileNameExW(handle, module, name, MODULE_PATH_CAPACITY):
                if 'asan' in Path(name.value).name.lower():
                    try:
                        result['asan_modules'].append(digest(name.value))
                    except OSError as error:
                        result['asan_modules'].append({'path': name.value, 'error': str(error)})
    return result


def replay(argv, cwd, output, timeout, files=(), purpose='actual executable replay',
           suppress_os_error_dialogs=False):
    if os.name != 'nt':
        raise RuntimeError('Windows only')
    timeout = positive_deadline(timeout)
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    child_env = environment(output)
    inputs = [digest(argv[0])] + [digest(path) for path in files]
    binary_snapshot = output/'binary-snapshot'
    binary_snapshot.mkdir()
    copied_inputs = []
    for ordinal, item in enumerate(inputs):
        destination = binary_snapshot/f'{ordinal}-{Path(item["path"]).name}'
        shutil.copyfile(item['path'], destination)
        copied = digest(destination)
        if copied['sha256'] != item['sha256']:
            raise RuntimeError('input changed during snapshot copy')
        copied_inputs.append(copied)
    receipt = {'purpose': purpose, 'argv': argv, 'cwd': str(Path(cwd).resolve()),
               'timeout_seconds': timeout, 'inputs': inputs, 'copied_inputs': copied_inputs,
               'environment_names': sorted(child_env),
               'environment_fingerprints': {key: hashlib.sha256(value.encode()).hexdigest()
                                            for key, value in child_env.items()},
               'asan_options': ASAN_OPTIONS, 'snapshots': [], 'state': 'prepared'}
    save(output/'receipt.json', receipt)
    process = None
    started = time.monotonic()
    with (output/'controller.jsonl').open('w', encoding='utf-8') as phases, \
            (output/'stdout.bin').open('wb') as stdout, \
            (output/'stderr.bin').open('wb') as stderr:
        def phase(name, **fields):
            phases.write(json.dumps({'phase': name, 'elapsed_seconds': time.monotonic()-started,
                                     **fields})+'\n')
            phases.flush()
        try:
            phase('before_spawn')
            with child_presentation_policy(suppress_os_error_dialogs) as policy:
                receipt['presentation_policy'] = policy
                process = subprocess.Popen(argv, cwd=cwd, env=child_env, stdin=subprocess.DEVNULL,
                                           stdout=stdout, stderr=stderr, close_fds=True)
            receipt.update(pid=process.pid, state='running')
            save(output/'receipt.json', receipt)
            phase('spawned', pid=process.pid)
            receipt['snapshots'].append({'phase': 'after_spawn', **observe(process)})
            try:
                process.wait(timeout=timeout)
                receipt.update(state='exited', exit_code=process.returncode)
                phase('exited', pid=process.pid, exit_code=process.returncode)
            except subprocess.TimeoutExpired:
                receipt.update(state='timed_out', exit_code=EXIT_TIMEOUT)
                phase('timeout', pid=process.pid)
                receipt['snapshots'].append({'phase': 'before_termination', **observe(process)})
                # Popen owns a live process HANDLE; no lookup/kill of a recycled PID.
                process.kill()
                phase('owned_child_terminated', pid=process.pid)
                process.wait(timeout=CLEANUP_TIMEOUT_SECONDS)
                receipt['child_exit_code'] = process.returncode
                phase('owned_child_reaped', pid=process.pid, exit_code=process.returncode)
        except BaseException as error:
            receipt.update(state='controller_failure', exit_code=EXIT_CONTROLLER,
                           exception_type=type(error).__name__, exception=str(error))
            phase('controller_failure', exception_type=type(error).__name__)
            if process is not None and process.poll() is None:
                process.kill()
                process.wait(timeout=CLEANUP_TIMEOUT_SECONDS)
                phase('owned_child_reaped_after_controller_failure', pid=process.pid)
            raise
        finally:
            receipt['elapsed_seconds'] = time.monotonic()-started
            receipt['child_alive'] = process is not None and process.poll() is None
            receipt['inputs_after'] = [digest(item['path']) for item in inputs]
            save(output/'receipt.json', receipt)
    receipt['output_files'] = [digest(output/'stdout.bin'), digest(output/'stderr.bin'),
                               digest(output/'controller.jsonl')]
    save(output/'receipt.json', receipt)
    return receipt


def ctest_command(ctest, build_dir, configuration, test_name, output):
    output.mkdir(parents=True, exist_ok=False)
    argv = [ctest, '--test-dir', str(build_dir), '-C', configuration, '--show-only=json-v1']
    save(output/'inventory-command.json', {'argv': argv, 'configuration': configuration})
    terminal = replay(argv, build_dir, output/'command', INVENTORY_TIMEOUT_SECONDS,
                      purpose='CTest inventory only; not a test execution')
    stdout = (output/'command/stdout.bin').read_bytes()
    (output/'ctest.json').write_bytes(stdout)
    (output/'ctest.stderr').write_bytes((output/'command/stderr.bin').read_bytes())
    save(output/'inventory-terminal.json', {'exit_code': terminal['exit_code'], 'pid': terminal['pid']})
    if terminal['exit_code']:
        raise RuntimeError('CTest inventory failed; its owned child receipt is retained')
    selected = [test for test in json.loads(stdout)['tests'] if test['name']==test_name]
    if len(selected)!=1 or not selected[0].get('command'):
        raise RuntimeError('one exact configured CTest command is required')
    test = selected[0]
    properties = {item['name']: item['value'] for item in test.get('properties', [])}
    if properties.get('ENVIRONMENT') or properties.get('ENVIRONMENT_MODIFICATION'):
        raise RuntimeError('CTest environment overrides require reviewed allowlist support')
    save(output/'selected-test.json', test)
    return test['command'], properties.get('WORKING_DIRECTORY', str(build_dir))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ctest', required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--configuration', required=True)
    parser.add_argument('--test-name', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--timeout-seconds', type=deadline_argument, required=True)
    parser.add_argument('--pdb', type=Path, required=True)
    parser.add_argument('--runtime-dll', type=Path, action='append', required=True)
    args = parser.parse_args()
    if os.name!='nt':
        parser.error('Windows only')
    command, cwd = ctest_command(args.ctest, args.build_dir.resolve(), args.configuration,
                                 args.test_name, args.output/'inventory')
    result = replay(command, cwd, args.output/'replay', args.timeout_seconds,
                    [args.pdb]+args.runtime_dll, suppress_os_error_dialogs=True)
    raise SystemExit(result['exit_code'])


if __name__ == '__main__':
    main()
