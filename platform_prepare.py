#!/usr/bin/env python3
# coding=utf-8

import sys
import os
import json
import subprocess

from script.util import (
    rm_rf, copy_file,
    do_subprocess_argv, run_shell_script,
    get_system_name
)


def get_deps(root):
    dep_file = os.path.join(root, "platform_libsdepend")
    if not os.path.exists(dep_file):
        print("Dependency file not found.")
        return []
    with open(dep_file, "r", encoding="utf-8") as f:
        try:
            deps = json.load(f)
        except Exception as e:
            print(f"Failed to parse dependency file: {e}")
            return []
    return deps


def _is_git_repo(path):
    git_dir = os.path.join(path, ".git")
    return os.path.exists(git_dir)


def _run_git_capture(path, args):
    cmd = ["git", "-C", path] + args
    ret = subprocess.run(
        cmd,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if ret.returncode != 0:
        return None
    return ret.stdout.strip()


def _git_head_matches_version(path, version):
    """
    Validate current SDK checkout against expected version.
    Supports both commit-hash and tag/branch-like refs.
    """
    if not _is_git_repo(path):
        return False

    head = _run_git_capture(path, ["rev-parse", "HEAD"])
    if not head:
        return False

    # Commit hash mode
    hex_chars = set("0123456789abcdefABCDEF")
    if 7 <= len(version) <= 40 and all(c in hex_chars for c in version):
        return head.startswith(version.lower())

    # Tag/ref mode
    wanted = _run_git_capture(path, ["rev-parse", f"{version}^{{commit}}"])
    if not wanted:
        return False

    return head == wanted


def _normalize_slce_line_endings(dest_path):
    """
    Some external SDK .slce files contain CRLF and later scripts parse `id:`
    lines with shell tools. Normalize to LF to avoid IDs like `wiseconnect3_sdk\\r`.
    """
    if not os.path.isdir(dest_path):
        return

    for name in os.listdir(dest_path):
        if not name.endswith(".slce"):
            continue
        file_path = os.path.join(dest_path, name)
        try:
            with open(file_path, "rb") as f:
                content = f.read()
            normalized = content.replace(b"\r\n", b"\n")
            if normalized != content:
                with open(file_path, "wb") as f:
                    f.write(normalized)
                print(f"Normalized line endings: {file_path}")
        except Exception as e:
            print(f"Warning: Failed to normalize {file_path}: {e}")


def need_prepare(root, prepare_file, target):
    result = False
    libs_need_prepare = []
    deps = get_deps(root)
    for dep in deps:
        name = dep.get("name", "")
        path = dep.get("path", "")
        version = dep.get("version", "")
        if not name or not path or not version:
            print(f"Invalid dependency config: {dep}")
            result = True
            continue

        dest_path = os.path.join(root, path)
        if not os.path.exists(dest_path):
            print(f"{name} not exists, need prepare.")
            libs_need_prepare.append(name)
            result = True
            continue

        if not _git_head_matches_version(dest_path, version):
            print(f"{name} version mismatch, need prepare.")
            libs_need_prepare.append(name)
            result = True

    if not _toolchain_installed(root):
        print("ARM toolchain not found, need prepare.")
        result = True
    if not _slc_installed(root):
        print("SLC CLI not found, need prepare.")
        result = True
    return result, libs_need_prepare


def record_prepare(prepare_file, target):
    with open(prepare_file, "a", encoding='utf-8') as f:
        f.write(target + "\n")
    return True


def _slc_installed(root):
    """
    Return True if an slc/slc-cli executable is available on PATH or under tools/slc.
    """
    import shutil
    if shutil.which("slc") or shutil.which("slc-cli"):
        return True
    slc_dir = os.path.join(root, "tools", "slc")
    if not os.path.isdir(slc_dir):
        return False
    for dirpath, _, filenames in os.walk(slc_dir):
        for name in filenames:
            if name.startswith("slc"):
                path = os.path.join(dirpath, name)
                if os.path.isfile(path) and os.access(path, os.X_OK):
                    return True
    return False


def _java_available(root):
    """
    Return True if slc can find a JVM: either the bundled tools/jre or a java
    on PATH.

    slc_cli is a Java application -- `slc` is a shell wrapper whose second line
    execs java -- so without one it dies with "java: command not found" and
    takes the whole generate step with it. script/slc_cli prepends
    tools/jre/bin to PATH when that java is executable, which is the layout
    install_jre produces on all three hosts (it descends into Contents/Home for
    the macOS bundle so the path does not vary).

    Both spellings of the binary are probed because this runs in the native
    interpreter, where "java" simply does not exist on Windows. The bash side
    tests -x on the extension-less name and gets away with it under MSYS;
    Python does not, and would otherwise re-run the installer on every build
    for a JRE that is already there.
    """
    import shutil
    jre_bin = os.path.join(root, "tools", "jre", "bin")
    for name in ("java", "java.exe"):
        if os.access(os.path.join(jre_bin, name), os.X_OK):
            return True
    return shutil.which("java") is not None


def _toolchain_installed(root):
    """
    Return True if an extracted bare-metal ARM toolchain sits in the shared
    platform/tools directory.

    Looks for the compiler rather than the directory: platform/tools is shared
    with every other platform and CI pre-seeds it with tarballs, so the
    directory is there long before anything has been unpacked into it.
    """
    import glob
    shared = os.path.join(root, os.pardir, "tools")
    for pattern in ("gcc-arm-none-eabi-*", "arm-gnu-toolchain-*-arm-none-eabi"):
        for path in glob.glob(os.path.join(shared, pattern)):
            for exe in ("arm-none-eabi-gcc", "arm-none-eabi-gcc.exe"):
                if os.path.isfile(os.path.join(path, "bin", exe)):
                    return True
    return False


def download_tools(root, prepare_file):
    print("Downloading Tools...")
    need_toolchain = not _toolchain_installed(root)
    need_slc = not _slc_installed(root)

    if need_toolchain:
        print("Initializing toolchain + Silicon Labs tools ...")
        if run_shell_script("./script/bootstrap") != 0:
            return False
    elif need_slc:
        # Toolchain may already exist from a partial bootstrap; still need SLC CLI.
        #
        # The JRE has to be asked for separately here. Only the full
        # ./script/bootstrap runs install_jre, and this branch skips it because
        # the toolchain is already present -- which is exactly the CI shape: the
        # shared platform/tools is pre-seeded with the compiler, so a freshly
        # cloned platform takes this path, installs slc, and then slc cannot
        # start. That surfaced as "java: command not found" followed by
        # "slc_generate.py failed (1)", with the toolchain sitting right there.
        if not _java_available(root):
            print("No JRE for slc, installing one ...")
            if run_shell_script("./script/bootstrap", "jre") != 0:
                print("Failed to install a JRE. Run: ./script/bootstrap jre")
                return False
        print("SLC CLI not found, installing Silicon Labs tools ...")
        if run_shell_script("./script/bootstrap_silabs") != 0:
            print("Failed to install SLC CLI. Run: ./script/bootstrap silabs")
            return False

    if not _slc_installed(root):
        print("ERROR: SLC CLI still missing after bootstrap.")
        print("Install manually: cd platform/SiWx917 && ./script/bootstrap silabs")
        return False

    return True


def download_sdk(root, prepare_file, prepare_libs):
    deps = get_deps(root)
    if not deps:
        return False
    for dep in deps:
        name = dep.get("name", "")
        url = dep.get("url", "")
        version = dep.get("version", "")
        path = dep.get("path", "")
        init_submodule = dep.get("init_submodule", False)
        patch = dep.get("patch", None)
        if name not in prepare_libs:
            continue
        if not url or not version or not path:
            print(f"Invalid dependency config: {dep}")
            continue
        # Clone
        dest_path = os.path.join(root, path)
        if not os.path.exists(dest_path):
            print(f"Cloning {name}: {url} ({version}) to {dest_path}")
            cmds = [
                "git",
                "clone",
                "--recursive",
                url,
                "-b",
                version,
                "--depth=1",
                dest_path,
            ]
            if do_subprocess_argv(cmds) != 0:
                print(f"Failed to clone {name}")
                return False
        else:
            print(f"Dependency already exists: {dest_path}")
            cmds = [
                "git",
                "reset",
                "--hard",
                version
            ]
            if do_subprocess_argv(cmds, cwd=dest_path) != 0:
                print(f"Failed to checkout {version} for {name}")
                return False
        # Init submodule
        if init_submodule:
            cmds = [
                "git",
                "submodule",
                "update",
                "--init",
                "--recursive",
            ]
            if do_subprocess_argv(cmds, cwd=dest_path) != 0:
                print(f"Failed to update submodules for {name}")
                return False
        # Apply Patch
        if patch is not None:
            patch_file = os.path.join(root, patch)
            if os.path.exists(patch_file):
                cmds = [
                    "git",
                    "apply",
                    patch_file,
                ]
                if do_subprocess_argv(cmds, cwd=dest_path) != 0:
                    print(f"Failed to apply patch for {name}")
                    return False
        _normalize_slce_line_endings(dest_path)
        record_prepare(prepare_file, name)
    return True


def platform_prepare(root, target):
    prepare_file = os.path.join(root, ".prepare")
    prepare_flag, prepare_libs = need_prepare(root, prepare_file, target)

    if not prepare_flag:
        print("No need prepare.")
        return True

    if not download_tools(root, prepare_file):
        print("Install toolchain failed.")
        return False

    if not download_sdk(root, prepare_file, prepare_libs):
        print("Download SDK failed.")
        return False

    return True


def main():
    print("================== platform_prepare ==================")
    target = "SiWx917"
    print(f"target: {target} ")
    root = os.path.dirname(os.path.abspath(__file__))
    if not platform_prepare(root, target):
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()
