#!/usr/bin/env python3
# coding=utf-8

import sys
import os
import re
import json
from script.util import (
    copy_file,
    do_subprocess, run_shell_script, get_system_name
)

try:
    from build_kconfig2slcp import kconfig2slcp
except ImportError:
    kconfig2slcp = None


def to_posix_path(path):
    '''
    Rewrite a path with forward slashes before handing it to the bash helpers.

    script/generate runs under bash, and run_slc evals its command line -- where
    a backslash is an escape character, not a separator. os.path.join produces
    backslashes on Windows, so ".build\\tuyaopen_x.slcp" reaches slc as
    ".buildtuyaopen_x.slcp" and the project fails to load. Windows accepts
    forward slashes in every API we go through, so normalize on the way out.
    '''
    return path.replace(os.sep, "/") if os.sep != "/" else path


mbr_note = """IMPORTANT: When changing M4 Flash Size, you must update the MBR (Master Boot Record)
Run the following commands to write the new MBR configuration to your SiWx917 device:

   commander manufacturing write tambr --data mbr_config.json -d SiWG917M111MGTBA
   commander manufacturing write m4mbrcf --data mbr_config.json -d SiWG917M111MGTBA

The mbr_config.json file is automatically generated during the build process.
"""


def get_config_param(config_file, param):
    if not os.path.isfile(config_file):
        print(f"Error: File not found [{config_file}].")
        return None
    try:
        with open(config_file, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if f'{param}=' in line:
                    return line.replace(f"{param}=", "").strip('"')
    except Exception as e:
        print(f"Error reading config file: [{str(e)}].")
        return None
    return None


def parse_linker(linker_file):
    """
    Parse linker script to extract all memory region definitions.

    Args:
        linker_file: Path to the linker script (.ld file)

    Returns:
        Dictionary with memory regions: {"rom": {"origin": xxx, "length": xxx}, "psram": {...}, ...}
        or None if file not found or parsing failed
    """
    if not os.path.isfile(linker_file):
        print(f"Error: Linker file not found [{linker_file}].")
        return None

    try:
        with open(linker_file, 'r', encoding='utf-8') as f:
            content = f.read()

        memory_regions = {}
        in_memory_section = False

        for line in content.split('\n'):
            line = line.strip()

            if line.startswith('MEMORY'):
                in_memory_section = True
                continue

            if in_memory_section and line == '}':
                break

            if in_memory_section and 'ORIGIN' in line and 'LENGTH' in line:
                # Parse line like: rom        (rx) : ORIGIN = 0x8202000, LENGTH = 0x1fe000
                parts = line.split(':')
                if len(parts) >= 2:
                    # Extract memory region name (left side before ':')
                    # Get first word (memory name)
                    memory_name = parts[0].strip().split()[0]

                    # Extract memory definition (right side after ':')
                    memory_def = parts[1].strip()

                    # Extract ORIGIN
                    origin = None
                    if 'ORIGIN' in memory_def:
                        origin_part = memory_def.split('ORIGIN')[1].split('=')[
                            1].split(',')[0].strip()
                        origin = origin_part

                    # Extract LENGTH
                    length = None
                    if 'LENGTH' in memory_def:
                        length_part = memory_def.split(
                            'LENGTH')[1].split('=')[1].strip()
                        length = length_part

                    if origin and length and memory_name:
                        memory_regions[memory_name] = {
                            'origin': origin,
                            'length': length,
                            'origin_int': int(origin, 16) if origin.startswith('0x') else int(origin),
                            'length_int': int(length, 16) if length.startswith('0x') else int(length)
                        }

        return memory_regions if memory_regions else None

    except Exception as e:
        print(f"Error parsing linker file: [{str(e)}].")
        return None


def set_region(linker_file, memory_name, origin, length):
    """
    Update memory region definition in linker script.

    Args:
        linker_file: Path to the linker script (.ld file)
        memory_name: Name of memory region (e.g., 'rom', 'ram', 'psram')
        origin: New origin address (integer or hex string)
        length: New length (integer or hex string)

    Returns:
        True if successful, False otherwise
    """
    if not os.path.isfile(linker_file):
        print(f"Error: Linker file not found [{linker_file}].")
        return False

    # Convert origin and length to hex strings if they're integers
    if isinstance(origin, int):
        origin_str = f"0x{origin:x}"
    else:
        origin_str = str(origin)

    if isinstance(length, int):
        length_str = f"0x{length:x}"
    else:
        length_str = str(length)

    try:
        # Read current linker file content
        with open(linker_file, 'r', encoding='utf-8') as f:
            content = f.read()

        # Create regex pattern to match the memory region line
        # Example: rom        (rx) : ORIGIN = 0x8202000, LENGTH = 0x1fe000
        import re
        pattern = rf'({memory_name}\s+\([^)]+\)\s*:\s*ORIGIN\s*=\s*)0x[0-9a-fA-F]+(\s*,\s*LENGTH\s*=\s*)0x[0-9a-fA-F]+'

        # Replacement string with new origin and length
        replacement = rf'\g<1>{origin_str}\g<2>{length_str}'

        # Perform the substitution
        updated_content = re.sub(pattern, replacement,
                                 content, flags=re.IGNORECASE)

        # Check if any changes were made
        if updated_content != content:
            # Write back to file
            with open(linker_file, 'w', encoding='utf-8') as f:
                f.write(updated_content)
            print(
                f"Linker: Updated {memory_name} - ORIGIN={origin_str}, LENGTH={length_str}")
            return True
        else:
            return True

    except Exception as e:
        print(f"Error updating linker file: [{str(e)}].")
        return False


def update_linker(slc_dir, config_file):
    m4_size = get_config_param(config_file, "CONFIG_CORE_M4_FLASH_SIZE")
    if m4_size is None:
        return False

    mbr_file = os.path.join(slc_dir, "mbr_config.json")
    linker_file = os.path.join(slc_dir, "autogen/linkerfile_psram_SoC.ld")
    if not os.path.isfile(linker_file):
        return False

    mem_info = parse_linker(linker_file)
    rom_start = mem_info["rom"]["origin_int"]
    # Calculate ROM end address, ensuring 64KB alignment
    rom_size = int(m4_size) * 1024  # Convert KB to bytes
    rom_end = rom_start + rom_size
    BLOCK_64KB = 0x10000
    if rom_end % BLOCK_64KB != 0:
        rom_end = (rom_end // BLOCK_64KB) * BLOCK_64KB
    rom_length = rom_end - rom_start

    # FreeRTOS heap in psram
    psram_heap_start = mem_info["psram_heap"]["origin_int"]

    # TEXT segment in psram
    psram_start = mem_info["psram"]["origin_int"]
    psram_length = rom_length
    # BSS segment in psram
    psram_bss_start = psram_start + psram_length
    psram_bss_length = psram_heap_start - psram_bss_start

    # Update region in linker file
    set_region(linker_file, "rom", rom_start, rom_length)
    set_region(linker_file, "psram", psram_start, psram_length)
    set_region(linker_file, "psram_bss", psram_bss_start, psram_bss_length)

    # Generate MBR config file
    # Extract the MSB (most significant byte) from ROM end address
    # For 0x84f0000, we want 0x4f (the byte at position 16-23 bits)
    m4_flash_msb = ((rom_end >> 16) & 0xFF) - 1
    print(f"MBR: M4 Flash MSB 0x{m4_flash_msb:02x} ({m4_flash_msb})")
    mbr_config = {
        "m4_common_flash_msb_addr": m4_flash_msb
    }

    try:
        with open(mbr_file, 'w', encoding='utf-8') as f:
            json.dump(mbr_config, f, indent=4)
        print(f"MBR: config written to: {mbr_file}")
    except Exception as e:
        print(f"MBR: Failed to write config: {e}")

    print(f"\033[93m{mbr_note}\033[0m")
    return True


def generate_slc_includes(project_name, build_param_path, slc_dir):
    """
    Extract include directories from SLC generated cmake file and create slc_includes.cmake
    This file will be included in platform_config.cmake for automatic include path updates
    """
    slc_cmake_file = os.path.join(
        slc_dir, f"tuyaopen_{project_name}_cmake", f"tuyaopen_{project_name}.cmake")

    if not os.path.isfile(slc_cmake_file):
        print(f"Warning: SLC cmake file not found: {slc_cmake_file}")
        return False

    try:
        with open(slc_cmake_file, 'r', encoding='utf-8') as f:
            content = f.read()

        # Get the directory containing the SLC cmake file for resolving relative paths
        slc_cmake_dir = os.path.dirname(slc_cmake_file)

        # Extract SDK_PATH
        sdk_path = None
        for line in content.split('\n'):
            if line.startswith('set(SDK_PATH'):
                sdk_path = line.split('"')[1]
                break

        if not sdk_path:
            print("Warning: SDK_PATH not found in SLC cmake file")
            return False

        # Extract include directories
        import re
        match = re.search(
            r'target_include_directories\([^\)]+\s+PUBLIC\s*(.*?)\n\s*\)', content, re.DOTALL)
        if not match:
            print("Warning: target_include_directories section not found")
            return False

        include_section = match.group(1)

        # Parse all paths
        paths = []
        for line in include_section.split('\n'):
            line = line.strip()
            if line.startswith('"') and line.endswith('"'):
                path = line.strip('"')

                # Replace ${SDK_PATH} with actual SDK path
                if '${SDK_PATH}' in path:
                    path = path.replace('${SDK_PATH}', sdk_path)

                # Convert relative paths to absolute
                if path.startswith('..'):
                    # Resolve relative path from SLC cmake directory
                    abs_path = os.path.normpath(
                        os.path.join(slc_cmake_dir, path))
                    # Convert to ${BUILD_DIR}/slc relative format
                    # slc_dir is like /path/.build/slc, so abs_path should be relative to slc_dir
                    try:
                        rel_to_slc = os.path.relpath(abs_path, slc_dir)
                        path = '${CMAKE_CURRENT_BINARY_DIR}/slc/' + \
                            rel_to_slc
                    except ValueError:
                        # Can't make relative, keep absolute
                        path = abs_path

                paths.append(path)

        if not paths:
            print("Warning: No include directories found")
            return False

        # Generate slc_includes.cmake content
        cmake_content = """# Auto-generated by build_setup.py from SLC generated cmake file
# Do not edit manually - this file is regenerated on each build
# Source: slc/tuyaopen_{project_name}_cmake/tuyaopen_{project_name}.cmake

set(SLC_INCLUDE_DIRS
""".format(project_name=project_name)

        for path in paths:
            cmake_content += f'    "{path}"\n'

        cmake_content += ')\n'

        # Write to build directory
        output_file = os.path.join(build_param_path, "slc_includes.cmake")
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(cmake_content)

        print(f"Generated: {output_file} ({len(paths)} include directories)")
        return True

    except Exception as e:
        print(f"Warning: Failed to generate slc_includes.cmake: {e}")
        return False


def generate(project_name, build_param_path):
    build_config_file = os.path.join(build_param_path, "cache/using.config")
    board = get_config_param(build_config_file, "CONFIG_BOARD_CHOICE")
    if board is None:
        print(f"Error: Board not found")
        return False
    board = board.lower()
    print(f'board: {board}')
    root = os.path.dirname(os.path.abspath(__file__))
    slcp_file = os.path.join(
        root, f"slc/platform_projects/{project_name}.slcp")
    if not os.path.isfile(slcp_file):
        slcp_file = os.path.join(
            root, f"slc/platform_projects/tuyaopen_si91x_template.slcp")
        print(f"{project_name}.slcp not found, using default {slcp_file}")
    slcp_dst = os.path.join(build_param_path, f"tuyaopen_{project_name}.slcp")
    copy_file(slcp_file, slcp_dst)
    # Read slcp_dst and replace 'project_name: tuyaopen_si91x_template' with actual project_name
    if os.path.isfile(slcp_dst):
        with open(slcp_dst, 'r', encoding='utf-8') as f:
            content = f.read()
        new_content = content.replace(
            'project_name: tuyaopen_si91x_template', f'project_name: tuyaopen_{project_name}')
        if new_content != content:
            with open(slcp_dst, 'w', encoding='utf-8') as f:
                f.write(new_content)

    # Convert kconfig to slcp
    if os.path.isfile(build_config_file) and kconfig2slcp is not None:
        try:
            print(
                f"Converting kconfig to slcp component: {build_config_file} -> {slcp_dst}")
            kconfig2slcp(build_config_file, slcp_dst)
        except Exception as e:
            print(f"Warning: Failed to convert kconfig to slcp: {e}")

    slc_generated_projects_dir = os.path.join(build_param_path, "slc")

    if run_shell_script("./script/generate", to_posix_path(slcp_dst),
                        to_posix_path(slc_generated_projects_dir),
                        board) != 0:
        print(f"Failed to generate {project_name}")
        return False

    update_linker(slc_generated_projects_dir, build_config_file)

    # Extract include directories from SLC generated cmake and create slc_includes.cmake
    generate_slc_includes(project_name, build_param_path,
                          slc_generated_projects_dir)

    return True


def main():
    '''
    Invoked from platform_config.cmake, not from tos.py: CMake already knows
    the app build directory as CMAKE_CURRENT_BINARY_DIR, so it can hand it over
    directly. Going through tos.py's build_setup hook would mean adding a fifth
    positional argument to a signature shared by every platform.
    '''
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <project_name> <build_dir>")
        sys.exit(1)
    project_name = sys.argv[1]
    builddir = sys.argv[2]
    print("================== slc_generate ==================")
    print(f'''project_name: {project_name}
builddir: {builddir}
''')

    if not generate(project_name, builddir):
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()
