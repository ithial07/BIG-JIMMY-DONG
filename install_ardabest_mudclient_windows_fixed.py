#!/usr/bin/env python3
"""
All-in-one Windows installer for ArdaBest MUD Client:
https://github.com/ithial07/BIG-JIMMY-DONG

The user only needs to run this one Python file. It will:
  1. Find or install MSYS2.
  2. Install the UCRT64 C++ compiler, CMake, Ninja, and Qt 6 packages.
  3. Download the latest repository source from GitHub.
  4. Build ardabest_client.exe and ardabest_client_safe.exe.
  5. Package the Qt and MinGW runtime DLLs with the client.
  6. Install the finished portable client under the current Windows account.
  7. Create Start Menu and Desktop shortcuts.
  8. Launch the client, unless --no-launch is used.

Normal use:
    py install_ardabest_mudclient_windows.py

Useful options:
    --yes                 Do not ask the initial confirmation question.
    --no-launch           Install without starting the client afterward.
    --safe                Start the safe fallback client after installation.
    --skip-msys2-install  Do not install MSYS2 if it is missing.
    --skip-packages       Do not install/update MSYS2 build packages.
    --keep-source         Keep the downloaded source under the install folder.
    --no-desktop          Do not create a Desktop shortcut.
    --no-shortcuts        Do not create Start Menu or Desktop shortcuts.
    --install-dir DIR     Choose another client installation folder.
    --msys2-root DIR      Preferred folder for a direct MSYS2 installation.
    --dry-run             Print intended actions without changing the computer.

Requirements:
    Windows 10 or Windows 11, 64-bit
    Python 3.9 or newer
    Internet access

This script uses only Python's standard library. No pip modules are needed.

Fix in this edition:
    MSYS2/pacman is forced into non-interactive mode, stale locks are repaired,
    progress bars are disabled, and the optional CheckSpace scan is disabled
    in a temporary pacman configuration. This avoids an apparent freeze at
    'checking available disk space' and never requires typing Y.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath
from typing import Iterable, Mapping, Sequence


APP_NAME = "ArdaBest MUD Client"
APP_FOLDER_NAME = "ArdaBestClient"
REPOSITORY_URL = "https://github.com/ithial07/BIG-JIMMY-DONG"
SOURCE_ARCHIVE_URL = REPOSITORY_URL + "/archive/refs/heads/main.zip"
MSYS2_RELEASE_API = "https://api.github.com/repos/msys2/msys2-installer/releases/latest"

NORMAL_EXE = "ardabest_client.exe"
SAFE_EXE = "ardabest_client_safe.exe"
NORMAL_LAUNCHER = "RUN-ARDABEST-CLIENT.bat"
SAFE_LAUNCHER = "RUN-SAFE-MODE-ONLY.bat"

MSYS2_PACKAGES = (
    "base-devel",
    "mingw-w64-ucrt-x86_64-gcc",
    "mingw-w64-ucrt-x86_64-cmake",
    "mingw-w64-ucrt-x86_64-ninja",
    "mingw-w64-ucrt-x86_64-qt6-base",
    "mingw-w64-ucrt-x86_64-qt6-tools",
)

HTTP_HEADERS = {
    "User-Agent": "ArdaBest-Windows-Python-Installer/1.1",
    "Accept": "application/vnd.github+json",
}


class InstallerError(RuntimeError):
    """A clear, user-facing installation failure."""


def default_install_dir() -> Path:
    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        return Path(local_app_data) / APP_FOLDER_NAME
    return Path.home() / "AppData" / "Local" / APP_FOLDER_NAME


def default_msys2_root() -> Path:
    system_drive = os.environ.get("SystemDrive", "C:")
    return Path(system_drive + "\\msys64")


def powershell_single_quote(value: str | Path) -> str:
    return "'" + str(value).replace("'", "''") + "'"


class Installer:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.install_dir = Path(args.install_dir).expanduser()
        self.preferred_msys2_root = Path(args.msys2_root).expanduser()
        self.env = os.environ.copy()
        self.env["MSYSTEM"] = "UCRT64"
        self.env["CHERE_INVOKING"] = "1"
        self.env["MSYS2_PATH_TYPE"] = "inherit"
        self.env.setdefault("CMAKE_BUILD_PARALLEL_LEVEL", str(max(1, os.cpu_count() or 2)))

    @staticmethod
    def info(message: str = "") -> None:
        print(message, flush=True)

    @staticmethod
    def command_text(command: Sequence[str | Path]) -> str:
        if os.name == "nt":
            return subprocess.list2cmdline([str(part) for part in command])
        return " ".join(shlex.quote(str(part)) for part in command)

    def run(
        self,
        command: Sequence[str | Path],
        *,
        cwd: Path | None = None,
        env: Mapping[str, str] | None = None,
        check: bool = True,
        capture: bool = False,
        creationflags: int = 0,
    ) -> subprocess.CompletedProcess[str]:
        command = [str(part) for part in command]
        location = f"  (in {cwd})" if cwd else ""
        self.info(f"> {self.command_text(command)}{location}")
        if self.args.dry_run:
            return subprocess.CompletedProcess(command, 0, stdout="", stderr="")

        merged_env = self.env.copy()
        if env:
            merged_env.update({str(k): str(v) for k, v in env.items()})

        try:
            return subprocess.run(
                command,
                cwd=str(cwd) if cwd else None,
                env=merged_env,
                check=check,
                text=True,
                stdout=subprocess.PIPE if capture else None,
                stderr=subprocess.PIPE if capture else None,
                creationflags=creationflags,
            )
        except FileNotFoundError as exc:
            raise InstallerError(f"Required command was not found: {command[0]}") from exc
        except subprocess.CalledProcessError as exc:
            details = ""
            if capture:
                output = (exc.stderr or exc.stdout or "").strip()
                if output:
                    details = "\n" + output
            raise InstallerError(
                f"Command failed with exit code {exc.returncode}:\n"
                f"  {self.command_text(command)}{details}"
            ) from exc

    def confirm(self, prompt: str) -> bool:
        if self.args.yes:
            return True
        try:
            return input(f"{prompt} [y/N] ").strip().lower() in {"y", "yes"}
        except EOFError:
            return False

    def require_supported_windows(self) -> None:
        if sys.version_info < (3, 9):
            raise InstallerError("Python 3.9 or newer is required.")

        if sys.platform != "win32":
            if self.args.dry_run:
                self.info("DRY RUN: non-Windows host accepted for installer inspection.")
                return
            raise InstallerError("This installer is for 64-bit Windows 10 or Windows 11 only.")

        if ctypes.sizeof(ctypes.c_void_p) < 8:
            raise InstallerError(
                "A 64-bit Python installation is required. Install 64-bit Python and run this again."
            )

        self.info(f"Detected Windows: {platform.platform()}")
        self.info(f"Python: {platform.python_version()} ({platform.machine() or 'unknown architecture'})")

    def print_intro(self) -> None:
        self.info("=" * 68)
        self.info("ArdaBest MUD Client - all-in-one Windows installer")
        self.info("=" * 68)
        self.info(f"Repository: {REPOSITORY_URL}")
        self.info(f"Install folder: {self.install_dir}")
        self.info()
        self.info("This may install MSYS2 and a Qt/C++ build toolchain, then compile")
        self.info("and package both the full client and its safe fallback client.")
        self.info("MSYS2 questions are answered automatically; do not type Y during setup.")
        self.info()

    @staticmethod
    def is_msys2_bash(candidate: Path) -> bool:
        if not candidate.is_file():
            return False
        # bash.exe and pacman.exe normally both live in MSYS2's usr\bin.
        return (candidate.parent / "pacman.exe").is_file()

    def candidate_msys2_bashes(self) -> Iterable[Path]:
        seen: set[str] = set()

        def emit(path: Path | None) -> Iterable[Path]:
            if path is None:
                return ()
            key = os.path.normcase(os.path.abspath(str(path)))
            if key in seen:
                return ()
            seen.add(key)
            return (path,)

        explicit_root = os.environ.get("MSYS2_ROOT")
        roots = [
            Path(explicit_root) if explicit_root else None,
            self.preferred_msys2_root,
            Path(r"C:\msys64"),
            Path(os.environ.get("LOCALAPPDATA", "")) / "Programs" / "MSYS2"
            if os.environ.get("LOCALAPPDATA")
            else None,
            Path(os.environ.get("LOCALAPPDATA", "")) / "Programs" / "msys2"
            if os.environ.get("LOCALAPPDATA")
            else None,
            Path(os.environ.get("LOCALAPPDATA", "")) / "msys64"
            if os.environ.get("LOCALAPPDATA")
            else None,
            Path(os.environ.get("ProgramFiles", "")) / "MSYS2"
            if os.environ.get("ProgramFiles")
            else None,
            Path(os.environ.get("ProgramFiles(x86)", "")) / "MSYS2"
            if os.environ.get("ProgramFiles(x86)")
            else None,
            Path.home() / "msys64",
        ]
        for root in roots:
            if root is not None:
                yield from emit(root / "usr" / "bin" / "bash.exe")

        found_bash = shutil.which("bash.exe") or shutil.which("bash")
        if found_bash:
            yield from emit(Path(found_bash))

    def find_msys2_bash(self) -> Path | None:
        for candidate in self.candidate_msys2_bashes():
            if self.is_msys2_bash(candidate):
                return candidate.resolve()
        return None

    def install_msys2_with_winget(self) -> bool:
        winget = shutil.which("winget.exe") or shutil.which("winget")
        if not winget:
            return False

        self.info("MSYS2 was not found. Installing it with Windows Package Manager...")
        result = self.run(
            [
                winget,
                "install",
                "--exact",
                "--id",
                "MSYS2.MSYS2",
                "--accept-source-agreements",
                "--accept-package-agreements",
                "--disable-interactivity",
                "--silent",
            ],
            check=False,
        )
        if self.args.dry_run:
            return True
        if result.returncode != 0:
            self.info(f"winget returned exit code {result.returncode}; trying direct MSYS2 download.")
            return False
        return True

    def fetch_json(self, url: str) -> object:
        request = urllib.request.Request(url, headers=HTTP_HEADERS)
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                return json.load(response)
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
            raise InstallerError(f"Could not read {url}: {exc}") from exc

    def latest_msys2_installer_url(self) -> tuple[str, str]:
        data = self.fetch_json(MSYS2_RELEASE_API)
        if not isinstance(data, dict):
            raise InstallerError("The MSYS2 release response was not in the expected format.")

        assets = data.get("assets")
        if not isinstance(assets, list):
            raise InstallerError("The latest MSYS2 release did not contain an asset list.")

        pattern = re.compile(
            r"^msys2-x86_64-(?:latest|[0-9].*)\.exe$", re.IGNORECASE
        )
        for asset in assets:
            if not isinstance(asset, dict):
                continue
            name = str(asset.get("name", ""))
            url = str(asset.get("browser_download_url", ""))
            if pattern.match(name) and url:
                return name, url

        raise InstallerError("Could not find the 64-bit MSYS2 GUI installer in its latest release.")

    def download_file(self, url: str, destination: Path, label: str) -> None:
        self.info(f"Downloading {label}:")
        self.info(f"  {url}")
        if self.args.dry_run:
            return

        request = urllib.request.Request(url, headers=HTTP_HEADERS)
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_suffix(destination.suffix + ".part")
        try:
            with urllib.request.urlopen(request, timeout=120) as response, temporary.open("wb") as out:
                total_raw = response.headers.get("Content-Length")
                total = int(total_raw) if total_raw and total_raw.isdigit() else 0
                copied = 0
                last_report = -1
                while True:
                    chunk = response.read(1024 * 1024)
                    if not chunk:
                        break
                    out.write(chunk)
                    copied += len(chunk)
                    if total:
                        percent = int(copied * 100 / total)
                        bucket = percent // 10
                        if bucket != last_report:
                            self.info(f"  {percent:3d}% ({copied // (1024 * 1024)} MB)")
                            last_report = bucket
                out.flush()
            temporary.replace(destination)
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            try:
                temporary.unlink(missing_ok=True)
            except OSError:
                pass
            raise InstallerError(f"Could not download {label}: {exc}") from exc

    def install_msys2_directly(self, work_dir: Path) -> None:
        name, url = self.latest_msys2_installer_url()
        installer_path = work_dir / name
        self.download_file(url, installer_path, "the official MSYS2 installer")

        root = self.preferred_msys2_root
        self.info(f"Installing MSYS2 into: {root}")
        self.info("Windows may display an administrator-permission prompt.")
        self.run(
            [
                installer_path,
                "in",
                "--confirm-command",
                "--accept-messages",
                "--accept-licenses",
                "--root",
                root.as_posix(),
            ]
        )

    def ensure_msys2(self, work_dir: Path) -> Path:
        existing = self.find_msys2_bash()
        if existing:
            self.info(f"Found MSYS2: {existing}")
            return existing

        if self.args.skip_msys2_install:
            raise InstallerError(
                "MSYS2 was not found and --skip-msys2-install was selected. "
                "Install MSYS2, then rerun this installer."
            )

        winget_attempted = self.install_msys2_with_winget()
        if winget_attempted and not self.args.dry_run:
            # Give winget a moment to finish registering filesystem changes.
            for _ in range(10):
                found = self.find_msys2_bash()
                if found:
                    self.info(f"Installed MSYS2: {found}")
                    return found
                time.sleep(1)

        if not self.args.dry_run:
            self.install_msys2_directly(work_dir)
            found = self.find_msys2_bash()
            if found:
                self.info(f"Installed MSYS2: {found}")
                return found
            raise InstallerError(
                "MSYS2 installation finished, but usr\\bin\\bash.exe could not be found. "
                "Restart Windows or rerun this installer as Administrator."
            )

        return self.preferred_msys2_root / "usr" / "bin" / "bash.exe"

    def bash_run(
        self,
        bash: Path,
        shell_command: str,
        *,
        extra_env: Mapping[str, str] | None = None,
        check: bool = True,
        capture: bool = False,
    ) -> subprocess.CompletedProcess[str]:
        env = {
            "MSYSTEM": "UCRT64",
            "CHERE_INVOKING": "1",
            "MSYS2_PATH_TYPE": "inherit",
        }
        if extra_env:
            env.update({str(k): str(v) for k, v in extra_env.items()})
        return self.run(
            [bash, "-lc", shell_command],
            env=env,
            check=check,
            capture=capture,
        )

    def pacman_process_running(self) -> bool:
        """Return True when another Windows pacman.exe process is still active."""
        if self.args.dry_run or os.name != "nt":
            return False
        tasklist = shutil.which("tasklist.exe") or shutil.which("tasklist")
        if not tasklist:
            return False
        result = self.run(
            [tasklist, "/NH", "/FO", "CSV"],
            check=False,
            capture=True,
        )
        output = (result.stdout or "").lower()
        return any(
            line.lstrip().startswith('"pacman.exe"')
            for line in output.splitlines()
        )

    def prepare_noninteractive_pacman(self, bash: Path) -> None:
        """Prepare a temporary pacman configuration that avoids the disk-space stall."""
        if self.pacman_process_running():
            raise InstallerError(
                "Another pacman.exe process is still running, probably from the previous "
                "installer window. Close that window or press Ctrl+C in it, wait a few "
                "seconds, and then run this fixed installer again."
            )

        self.info("Preparing pacman for unattended installation...")
        shell_command = (
            'export PATH="/ucrt64/bin:/usr/bin:$PATH"; '
            'set -e; '
            'if [[ -e /var/lib/pacman/db.lck ]]; then '
            'echo "Removing a stale pacman database lock..."; '
            'rm -f /var/lib/pacman/db.lck; fi; '
            'PACMAN_CONF=/tmp/ardabest-pacman.conf; '
            "sed -E 's/^[[:space:]]*CheckSpace[[:space:]]*$/# CheckSpace disabled temporarily by ArdaBest installer/' "
            '/etc/pacman.conf > "$PACMAN_CONF"; '
            "if ! grep -Eq '^[[:space:]]*NoProgressBar([[:space:]]|$)' \"$PACMAN_CONF\"; then "
            "sed -i '/^\\[options\\]/a NoProgressBar' \"$PACMAN_CONF\"; fi; "
            'echo "Pacman will answer all confirmation questions automatically."'
        )
        self.bash_run(bash, shell_command)

    def install_build_packages(self, bash: Path) -> None:
        if self.args.skip_packages:
            self.info("Skipping MSYS2 package installation (--skip-packages).")
            return

        self.prepare_noninteractive_pacman(bash)
        common = (
            'export PATH="/ucrt64/bin:/usr/bin:$PATH"; '
            'PACMAN_CONF=/tmp/ardabest-pacman.conf; '
        )

        self.info("Updating the MSYS2 package database...")
        self.info("You do not need to type Y; the installer answers automatically.")
        self.bash_run(
            bash,
            common
            + "printf 'Y\\n' | pacman --config \"$PACMAN_CONF\" "
              "--color never --noconfirm --disable-download-timeout -Sy",
        )

        self.info("Installing the compiler, CMake, Ninja, and Qt 6...")
        self.info("The package list may take several minutes, but it will not wait for input.")
        package_text = " ".join(shlex.quote(package) for package in MSYS2_PACKAGES)
        self.bash_run(
            bash,
            common
            + "printf 'Y\\n' | pacman --config \"$PACMAN_CONF\" "
              "--color never --noconfirm --disable-download-timeout "
              f"-S --needed {package_text}",
        )

    def verify_build_environment(self, bash: Path) -> None:
        self.info("Checking the Windows build environment...")
        result = self.bash_run(
            bash,
            'export PATH="/ucrt64/bin:/usr/bin:$PATH"; '
            'printf "cmake="; cmake --version | head -n 1; '
            'printf "ninja="; ninja --version; '
            'printf "g++="; g++ --version | head -n 1; '
            'printf "qt="; (qmake6 -query QT_VERSION 2>/dev/null || qmake -query QT_VERSION)',
            capture=True,
        )
        if not self.args.dry_run and result.stdout:
            self.info(result.stdout.strip())

    @staticmethod
    def safe_extract_zip(archive: Path, destination: Path) -> None:
        destination_resolved = destination.resolve()
        with zipfile.ZipFile(archive) as zf:
            for member in zf.infolist():
                # GitHub ZIP names always use POSIX separators. Reject absolute and .. paths.
                relative = PurePosixPath(member.filename)
                if relative.is_absolute() or ".." in relative.parts:
                    raise InstallerError(f"Unsafe path found in downloaded ZIP: {member.filename}")
                target = (destination / Path(*relative.parts)).resolve()
                try:
                    target.relative_to(destination_resolved)
                except ValueError as exc:
                    raise InstallerError(
                        f"Unsafe path found in downloaded ZIP: {member.filename}"
                    ) from exc
            zf.extractall(destination)

    def download_source(self, work_dir: Path) -> Path:
        archive = work_dir / "BIG-JIMMY-DONG-main.zip"
        extract_dir = work_dir / "source-extracted"
        self.download_file(SOURCE_ARCHIVE_URL, archive, "the latest MUD client source")

        self.info("Extracting the source code...")
        if self.args.dry_run:
            return extract_dir / "BIG-JIMMY-DONG-main"

        extract_dir.mkdir(parents=True, exist_ok=True)
        try:
            self.safe_extract_zip(archive, extract_dir)
        except (zipfile.BadZipFile, OSError) as exc:
            raise InstallerError(f"Could not extract the downloaded source archive: {exc}") from exc
        candidates = [
            path
            for path in extract_dir.iterdir()
            if path.is_dir() and (path / "CMakeLists.txt").is_file()
        ]
        if len(candidates) != 1:
            raise InstallerError(
                "The GitHub archive did not contain exactly one recognizable source folder."
            )
        return candidates[0]

    def build_and_package(self, bash: Path, source_dir: Path) -> Path:
        build_script = source_dir / "tools" / "msys2-install-and-build.sh"
        if not self.args.dry_run and not build_script.is_file():
            raise InstallerError(
                "The repository is missing tools\\msys2-install-and-build.sh, so its Windows "
                "packaging procedure could not be run."
            )

        self.info("Building and packaging ArdaBest MUD Client...")
        shell_command = (
            'export PATH="/ucrt64/bin:/usr/bin:$PATH"; '
            'cd "$(cygpath -u \"$PROJECT_DIR\")"; '
            'bash tools/msys2-install-and-build.sh'
        )
        self.bash_run(
            bash,
            shell_command,
            extra_env={"PROJECT_DIR": str(source_dir)},
        )

        package_dir = source_dir / "package-windows"
        self.validate_package(package_dir)
        return package_dir

    def validate_package(self, package_dir: Path) -> None:
        if self.args.dry_run:
            return

        required = [
            package_dir / NORMAL_EXE,
            package_dir / SAFE_EXE,
            package_dir / NORMAL_LAUNCHER,
            package_dir / SAFE_LAUNCHER,
            package_dir / "platforms" / "qwindows.dll",
        ]
        missing = [path for path in required if not path.is_file()]
        qt_runtime_found = any(package_dir.glob("Qt6Widgets.dll")) and any(
            package_dir.glob("Qt6Network.dll")
        )
        if not qt_runtime_found:
            missing.append(package_dir / "Qt6Widgets.dll and Qt6Network.dll")

        if missing:
            missing_text = "\n".join(f"  - {path}" for path in missing)
            raise InstallerError(
                "The build completed, but the portable Windows package is incomplete:\n"
                + missing_text
            )

    @staticmethod
    def copy_tree_contents(source: Path, destination: Path) -> None:
        destination.mkdir(parents=True, exist_ok=True)
        for item in source.iterdir():
            target = destination / item.name
            if item.is_dir():
                shutil.copytree(item, target, dirs_exist_ok=True)
            else:
                shutil.copy2(item, target)

    def install_package(self, package_dir: Path, source_dir: Path) -> None:
        self.info(f"Installing the portable client into:\n  {self.install_dir}")
        if self.args.dry_run:
            return

        self.install_dir.parent.mkdir(parents=True, exist_ok=True)
        staging_dir = self.install_dir.parent / f".{APP_FOLDER_NAME}-staging-{os.getpid()}"
        backup_dir = self.install_dir.parent / f".{APP_FOLDER_NAME}-backup-{os.getpid()}"

        shutil.rmtree(staging_dir, ignore_errors=True)
        shutil.rmtree(backup_dir, ignore_errors=True)
        staging_dir.mkdir(parents=True)
        self.copy_tree_contents(package_dir, staging_dir)

        # Keep profiles, custom maps, logs, and other user-created files from an older install.
        if self.install_dir.exists():
            for preserved_name in (
                "profile",
                "profiles",
                "ardabest_startup_log.txt",
                "custom_map.json",
            ):
                old_item = self.install_dir / preserved_name
                new_item = staging_dir / preserved_name
                if not old_item.exists() or new_item.exists():
                    continue
                if old_item.is_dir():
                    shutil.copytree(old_item, new_item, dirs_exist_ok=True)
                else:
                    shutil.copy2(old_item, new_item)

        readme = staging_dir / "INSTALLED-BY-PYTHON.txt"
        readme.write_text(
            "ArdaBest MUD Client\n"
            "===================\n\n"
            "Run RUN-ARDABEST-CLIENT.bat for the full client with automatic safe-mode fallback.\n"
            "Run RUN-SAFE-MODE-ONLY.bat to open only the safe client.\n\n"
            f"Source repository: {REPOSITORY_URL}\n"
            "The executable, Qt plugins, and runtime DLLs must remain together in this folder.\n",
            encoding="utf-8",
        )

        try:
            if self.install_dir.exists():
                self.install_dir.replace(backup_dir)
            staging_dir.replace(self.install_dir)
        except OSError as exc:
            # Try to put the old install back if replacement failed.
            if not self.install_dir.exists() and backup_dir.exists():
                try:
                    backup_dir.replace(self.install_dir)
                except OSError:
                    pass
            raise InstallerError(
                "Could not replace the existing installation. Close ArdaBest Client and any "
                f"Explorer window using {self.install_dir}, then run the installer again.\n{exc}"
            ) from exc
        finally:
            shutil.rmtree(staging_dir, ignore_errors=True)

        shutil.rmtree(backup_dir, ignore_errors=True)

        if self.args.keep_source:
            kept_source = self.install_dir / "source"
            shutil.rmtree(kept_source, ignore_errors=True)
            shutil.copytree(
                source_dir,
                kept_source,
                ignore=shutil.ignore_patterns(
                    "build-windows-msys2",
                    "package-windows",
                    ".git",
                ),
            )

    @staticmethod
    def known_folder(csidl: int, fallback: Path) -> Path:
        if os.name != "nt":
            return fallback
        buffer = ctypes.create_unicode_buffer(32768)
        # SHGFP_TYPE_CURRENT = 0
        result = ctypes.windll.shell32.SHGetFolderPathW(None, csidl, None, 0, buffer)
        return Path(buffer.value) if result == 0 and buffer.value else fallback

    def create_shortcut(
        self,
        shortcut_path: Path,
        target_path: Path,
        working_dir: Path,
        icon_path: Path,
        description: str,
    ) -> None:
        if not self.args.dry_run:
            shortcut_path.parent.mkdir(parents=True, exist_ok=True)
        ps_script = (
            "$ws = New-Object -ComObject WScript.Shell; "
            f"$s = $ws.CreateShortcut({powershell_single_quote(shortcut_path)}); "
            f"$s.TargetPath = {powershell_single_quote(target_path)}; "
            f"$s.WorkingDirectory = {powershell_single_quote(working_dir)}; "
            f"$s.IconLocation = {powershell_single_quote(str(icon_path) + ',0')}; "
            f"$s.Description = {powershell_single_quote(description)}; "
            "$s.Save()"
        )
        powershell = shutil.which("powershell.exe") or shutil.which("powershell")
        if not powershell and not self.args.dry_run:
            raise InstallerError("Windows PowerShell was not found, so shortcuts could not be created.")
        self.run(
            [
                powershell or "powershell.exe",
                "-NoProfile",
                "-NonInteractive",
                "-ExecutionPolicy",
                "Bypass",
                "-Command",
                ps_script,
            ]
        )

    def create_shortcuts(self) -> None:
        if self.args.no_shortcuts:
            self.info("Skipping shortcuts (--no-shortcuts).")
            return

        self.info("Creating Windows shortcuts...")
        programs_fallback = (
            Path(os.environ.get("APPDATA", str(Path.home() / "AppData" / "Roaming")))
            / "Microsoft"
            / "Windows"
            / "Start Menu"
            / "Programs"
        )
        desktop_fallback = Path.home() / "Desktop"
        programs_dir = self.known_folder(0x0002, programs_fallback)  # CSIDL_PROGRAMS
        desktop_dir = self.known_folder(0x0010, desktop_fallback)  # CSIDL_DESKTOPDIRECTORY

        normal_launcher = self.install_dir / NORMAL_LAUNCHER
        safe_launcher = self.install_dir / SAFE_LAUNCHER
        normal_icon = self.install_dir / NORMAL_EXE
        safe_icon = self.install_dir / SAFE_EXE

        start_menu_folder = programs_dir / APP_NAME
        self.create_shortcut(
            start_menu_folder / f"{APP_NAME}.lnk",
            normal_launcher,
            self.install_dir,
            normal_icon,
            "Open ArdaBest MUD Client",
        )
        self.create_shortcut(
            start_menu_folder / f"{APP_NAME} - Safe Mode.lnk",
            safe_launcher,
            self.install_dir,
            safe_icon,
            "Open the safe fallback ArdaBest MUD Client",
        )

        if not self.args.no_desktop:
            self.create_shortcut(
                desktop_dir / f"{APP_NAME}.lnk",
                normal_launcher,
                self.install_dir,
                normal_icon,
                "Open ArdaBest MUD Client",
            )

    def launch(self) -> None:
        if self.args.no_launch:
            return
        launcher = self.install_dir / (SAFE_LAUNCHER if self.args.safe else NORMAL_LAUNCHER)
        self.info(f"Launching: {launcher}")
        if self.args.dry_run:
            return
        if not launcher.is_file():
            raise InstallerError(f"Launcher was not found after installation: {launcher}")
        try:
            os.startfile(str(launcher))  # type: ignore[attr-defined]
        except OSError as exc:
            raise InstallerError(f"The client was installed but could not be launched: {exc}") from exc

    def run_installer(self) -> None:
        self.require_supported_windows()
        self.print_intro()
        if not self.confirm("Continue with setup and installation?"):
            raise InstallerError("Installation was cancelled.")

        if self.args.dry_run:
            work_dir = Path(tempfile.gettempdir()) / "ardabest-installer-dry-run"
            self._run_workflow(work_dir)
            return

        with tempfile.TemporaryDirectory(prefix="ardabest-installer-") as temp:
            self._run_workflow(Path(temp))

    def _run_workflow(self, work_dir: Path) -> None:
        self.info("[1/7] Finding or installing MSYS2")
        bash = self.ensure_msys2(work_dir)

        self.info("\n[2/7] Installing Windows build dependencies")
        self.install_build_packages(bash)
        self.verify_build_environment(bash)

        self.info("\n[3/7] Downloading the latest client source")
        source_dir = self.download_source(work_dir)

        self.info("\n[4/7] Compiling and collecting runtime files")
        package_dir = self.build_and_package(bash, source_dir)

        self.info("\n[5/7] Installing the portable client")
        self.install_package(package_dir, source_dir)

        self.info("\n[6/7] Creating launch shortcuts")
        self.create_shortcuts()

        self.info("\n[7/7] Installation complete")
        self.info("=" * 68)
        self.info("SUCCESS")
        self.info(f"Installed folder: {self.install_dir}")
        self.info(f"Normal launcher: {self.install_dir / NORMAL_LAUNCHER}")
        self.info(f"Safe launcher:   {self.install_dir / SAFE_LAUNCHER}")
        self.info("=" * 68)
        self.launch()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Download, build, package, and install ArdaBest MUD Client on Windows."
    )
    parser.add_argument("--yes", action="store_true", help="Skip the initial confirmation.")
    parser.add_argument("--no-launch", action="store_true", help="Do not launch after install.")
    parser.add_argument("--safe", action="store_true", help="Launch safe mode after install.")
    parser.add_argument(
        "--skip-msys2-install",
        action="store_true",
        help="Fail instead of installing MSYS2 when it is missing.",
    )
    parser.add_argument(
        "--skip-packages",
        action="store_true",
        help="Do not install or update MSYS2 build packages.",
    )
    parser.add_argument(
        "--keep-source",
        action="store_true",
        help="Keep a copy of the downloaded source in the install folder.",
    )
    parser.add_argument(
        "--no-desktop", action="store_true", help="Do not create a Desktop shortcut."
    )
    parser.add_argument(
        "--no-shortcuts", action="store_true", help="Do not create any Windows shortcuts."
    )
    parser.add_argument(
        "--install-dir",
        type=Path,
        default=default_install_dir(),
        help="Per-user client installation folder.",
    )
    parser.add_argument(
        "--msys2-root",
        type=Path,
        default=default_msys2_root(),
        help="Preferred folder used by the direct MSYS2 installer fallback.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print actions and commands without changing the computer.",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    installer = Installer(args)
    try:
        installer.run_installer()
        return 0
    except KeyboardInterrupt:
        print("\nInstallation cancelled by the user.", file=sys.stderr)
        return 130
    except InstallerError as exc:
        print("\n" + "=" * 68, file=sys.stderr)
        print("INSTALLATION FAILED", file=sys.stderr)
        print(str(exc), file=sys.stderr)
        print("=" * 68, file=sys.stderr)
        if sys.platform == "win32" and not args.dry_run:
            print(
                "Close ArdaBest Client, then rerun this file. If MSYS2 installation "
                "failed, right-click Command Prompt and choose Run as administrator.",
                file=sys.stderr,
            )
            if sys.stdin is not None and sys.stdin.isatty():
                try:
                    input("Press Enter to close this installer...")
                except (EOFError, KeyboardInterrupt):
                    pass
        return 1
    except Exception as exc:  # Last-resort readable failure instead of a silent window close.
        print("\nUnexpected installer error:", file=sys.stderr)
        print(f"{type(exc).__name__}: {exc}", file=sys.stderr)
        if sys.platform == "win32" and not args.dry_run:
            if sys.stdin is not None and sys.stdin.isatty():
                try:
                    input("Press Enter to close this installer...")
                except (EOFError, KeyboardInterrupt):
                    pass
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
