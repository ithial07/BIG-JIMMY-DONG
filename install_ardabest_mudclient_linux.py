#!/usr/bin/env python3
"""
All-in-one Linux installer for the ArdaBest MUD Client repository:
https://github.com/ithial07/BIG-JIMMY-DONG

What it does:
  1. Detects a supported Linux package manager.
  2. Installs the C++/CMake/Qt 6 build dependencies.
  3. Downloads the latest source archive from GitHub.
  4. Builds both ardabest_client and ardabest_client_safe.
  5. Installs launchers in ~/.local/bin and a desktop-menu entry.

Run:
    python3 install_ardabest_mudclient_linux.py

Useful options:
    --yes             Answer yes to installer prompts.
    --no-launch       Do not start the client after installation.
    --safe            Start the safe client after installation.
    --skip-packages   Do not use the system package manager.
    --keep-source     Keep a copy of the downloaded source tree.
    --install-dir DIR Choose a different per-user installation directory.
    --dry-run         Print commands/actions without changing the computer.
"""

from __future__ import annotations

import argparse
import contextlib
import os
import platform
import shlex
import shutil
import stat
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import zipfile
from pathlib import Path
from typing import Iterable, Mapping, Sequence


APP_DISPLAY_NAME = "ArdaBest MUD Client"
REPOSITORY_URL = "https://github.com/ithial07/BIG-JIMMY-DONG"
ARCHIVE_URL = REPOSITORY_URL + "/archive/refs/heads/main.zip"
DEFAULT_INSTALL_DIR = Path.home() / ".local" / "share" / "ardabest-client"
DEFAULT_USER_BIN = Path.home() / ".local" / "bin"
DEFAULT_APPLICATIONS_DIR = Path.home() / ".local" / "share" / "applications"

NORMAL_BINARY = "ardabest_client"
SAFE_BINARY = "ardabest_client_safe"

PACKAGE_SETS: Mapping[str, Sequence[str]] = {
    # Debian, Ubuntu, Linux Mint, Pop!_OS and similar.
    "apt": (
        "build-essential",
        "cmake",
        "ninja-build",
        "pkg-config",
        "qt6-base-dev",
        "qt6-base-dev-tools",
    ),
    # Fedora, RHEL-family systems using DNF.
    "dnf": (
        "gcc-c++",
        "make",
        "cmake",
        "ninja-build",
        "pkgconf-pkg-config",
        "qt6-qtbase-devel",
        "mesa-libGL-devel",
    ),
    # Older RHEL-family systems using YUM.
    "yum": (
        "gcc-c++",
        "make",
        "cmake",
        "ninja-build",
        "pkgconfig",
        "qt6-qtbase-devel",
        "mesa-libGL-devel",
    ),
    # Arch Linux, EndeavourOS, Manjaro and similar.
    "pacman": (
        "base-devel",
        "cmake",
        "ninja",
        "pkgconf",
        "qt6-base",
    ),
    # openSUSE.
    "zypper": (
        "gcc-c++",
        "make",
        "cmake",
        "ninja",
        "pkg-config",
        "qt6-base-devel",
        "Mesa-libGL-devel",
    ),
    # Alpine Linux.
    "apk": (
        "build-base",
        "cmake",
        "ninja",
        "pkgconf",
        "qt6-qtbase-dev",
        "mesa-dev",
    ),
}


class InstallerError(RuntimeError):
    """Raised for a clear, user-facing installation failure."""


class Installer:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.install_dir = args.install_dir.expanduser().resolve()
        self.user_bin = DEFAULT_USER_BIN.expanduser().resolve()
        self.applications_dir = DEFAULT_APPLICATIONS_DIR.expanduser().resolve()
        self.env = os.environ.copy()
        self.env.setdefault("CMAKE_BUILD_PARALLEL_LEVEL", str(max(1, os.cpu_count() or 2)))

    @staticmethod
    def info(message: str = "") -> None:
        print(message, flush=True)

    @staticmethod
    def command_text(command: Sequence[str]) -> str:
        return " ".join(shlex.quote(str(part)) for part in command)

    def run(
        self,
        command: Sequence[str],
        *,
        cwd: Path | None = None,
        check: bool = True,
        capture: bool = False,
    ) -> subprocess.CompletedProcess[str]:
        command = [str(part) for part in command]
        location = f"  (in {cwd})" if cwd else ""
        self.info(f"$ {self.command_text(command)}{location}")
        if self.args.dry_run:
            return subprocess.CompletedProcess(command, 0, stdout="", stderr="")
        try:
            return subprocess.run(
                command,
                cwd=str(cwd) if cwd else None,
                env=self.env,
                check=check,
                text=True,
                stdout=subprocess.PIPE if capture else None,
                stderr=subprocess.PIPE if capture else None,
            )
        except FileNotFoundError as exc:
            raise InstallerError(f"Required command was not found: {command[0]}") from exc
        except subprocess.CalledProcessError as exc:
            details = ""
            if capture:
                details = "\n" + (exc.stderr or exc.stdout or "").strip()
            raise InstallerError(
                f"Command failed with exit code {exc.returncode}:\n"
                f"  {self.command_text(command)}{details}"
            ) from exc

    def require_linux(self) -> None:
        if sys.platform != "linux":
            raise InstallerError(
                "This installer is for Linux only. The repository has separate Windows "
                "and macOS build scripts."
            )
        if sys.version_info < (3, 8):
            raise InstallerError("Python 3.8 or newer is required.")
        self.info(f"Detected Linux: {platform.platform()}")

    @staticmethod
    def find_package_manager() -> str | None:
        for manager in ("apt-get", "dnf", "yum", "pacman", "zypper", "apk"):
            if shutil.which(manager):
                return "apt" if manager == "apt-get" else manager
        return None

    def elevated(self, command: Sequence[str]) -> list[str]:
        if hasattr(os, "geteuid") and os.geteuid() == 0:
            return [str(x) for x in command]
        sudo = shutil.which("sudo")
        if not sudo:
            raise InstallerError(
                "Installing system packages requires root access, but sudo was not found. "
                "Install the dependencies as root, then rerun with --skip-packages."
            )
        return [sudo, *[str(x) for x in command]]

    def confirm(self, prompt: str) -> bool:
        if self.args.yes:
            return True
        try:
            answer = input(f"{prompt} [y/N] ").strip().lower()
        except EOFError:
            return False
        return answer in {"y", "yes"}

    def install_system_packages(self) -> None:
        if self.args.skip_packages:
            self.info("Skipping system package installation (--skip-packages).")
            return

        manager = self.find_package_manager()
        if not manager:
            raise InstallerError(
                "No supported package manager was detected. Install CMake 3.21+, Ninja or "
                "Make, a C++23-capable compiler, pkg-config, and the Qt 6 Widgets/Network "
                "development packages; then rerun with --skip-packages."
            )

        packages = list(PACKAGE_SETS[manager])
        self.info(f"Package manager: {manager}")
        self.info("Build dependencies: " + ", ".join(packages))
        if not self.confirm("Install/verify these system packages now?"):
            raise InstallerError(
                "Package installation was declined. Rerun with --skip-packages after "
                "installing the dependencies manually."
            )

        if manager == "apt":
            self.run(self.elevated(["apt-get", "update"]))
            self.run(self.elevated(["apt-get", "install", "-y", *packages]))
        elif manager in {"dnf", "yum"}:
            self.run(self.elevated([manager, "install", "-y", *packages]))
        elif manager == "pacman":
            self.run(self.elevated(["pacman", "-Syu", "--needed", "--noconfirm", *packages]))
        elif manager == "zypper":
            self.run(self.elevated(["zypper", "--non-interactive", "install", *packages]))
        elif manager == "apk":
            self.run(self.elevated(["apk", "add", *packages]))

    def check_build_tools(self) -> None:
        missing = [name for name in ("cmake", "c++") if not shutil.which(name)]
        if missing and not self.args.dry_run:
            raise InstallerError("Missing required build tools: " + ", ".join(missing))

        if self.args.dry_run:
            return

        result = self.run(["cmake", "--version"], capture=True)
        first_line = (result.stdout or "").splitlines()[0] if result.stdout else "unknown version"
        self.info(f"Using {first_line}")

    def download_archive(self, destination: Path) -> None:
        self.info(f"Downloading latest source from:\n  {ARCHIVE_URL}")
        if self.args.dry_run:
            return

        request = urllib.request.Request(
            ARCHIVE_URL,
            headers={
                "User-Agent": "ArdaBest-Linux-Installer/1.0",
                "Accept": "application/zip,application/octet-stream;q=0.9,*/*;q=0.1",
            },
        )
        try:
            with contextlib.closing(urllib.request.urlopen(request, timeout=90)) as response:
                content_length = response.headers.get("Content-Length")
                expected = int(content_length) if content_length and content_length.isdigit() else None
                downloaded = 0
                with destination.open("wb") as output:
                    while True:
                        chunk = response.read(1024 * 1024)
                        if not chunk:
                            break
                        output.write(chunk)
                        downloaded += len(chunk)
                        if expected:
                            percent = min(100, downloaded * 100 // expected)
                            print(f"\rDownloaded {percent:3d}%", end="", flush=True)
                if expected:
                    print()
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            raise InstallerError(f"Could not download the GitHub source archive: {exc}") from exc

        if not destination.exists() or destination.stat().st_size < 1024:
            raise InstallerError("The downloaded source archive is missing or unexpectedly small.")
        if not zipfile.is_zipfile(destination):
            raise InstallerError("GitHub did not return a valid ZIP archive.")

    @staticmethod
    def safe_extract_zip(archive: Path, destination: Path) -> Path:
        destination = destination.resolve()
        with zipfile.ZipFile(archive) as zf:
            members = zf.infolist()
            if not members:
                raise InstallerError("The downloaded ZIP archive is empty.")
            for member in members:
                target = (destination / member.filename).resolve()
                try:
                    target.relative_to(destination)
                except ValueError as exc:
                    raise InstallerError(
                        f"Unsafe path found in the downloaded archive: {member.filename}"
                    ) from exc
            zf.extractall(destination)

        roots = [path for path in destination.iterdir() if path.is_dir()]
        candidates = [path for path in roots if (path / "CMakeLists.txt").is_file()]
        if len(candidates) != 1:
            raise InstallerError("Could not locate the extracted project source directory.")
        return candidates[0]

    def validate_source(self, source_dir: Path) -> None:
        required = (
            source_dir / "CMakeLists.txt",
            source_dir / "src" / "main.cpp",
            source_dir / "src" / "main_safe.cpp",
            source_dir / "resources" / "ardabest.json",
        )
        missing = [str(path.relative_to(source_dir)) for path in required if not path.is_file()]
        if missing:
            raise InstallerError(
                "The downloaded repository is missing required files: " + ", ".join(missing)
            )

    def configure_and_build(self, source_dir: Path, work_dir: Path) -> Path:
        build_dir = work_dir / "build"
        stage_dir = work_dir / "stage"
        configure = [
            "cmake",
            "-S",
            str(source_dir),
            "-B",
            str(build_dir),
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_INSTALL_PREFIX={stage_dir}",
        ]
        if shutil.which("ninja") or self.args.dry_run:
            configure.extend(["-G", "Ninja"])

        self.info("\nConfiguring the Qt/C++ project...")
        self.run(configure)
        self.info("\nBuilding the normal and safe clients...")
        self.run(["cmake", "--build", str(build_dir), "--parallel", str(max(1, os.cpu_count() or 2))])
        self.info("\nStaging the built executables...")
        self.run(["cmake", "--install", str(build_dir), "--prefix", str(stage_dir)])

        if self.args.dry_run:
            return stage_dir

        # The repository installs the binaries into the prefix root. Fall back to
        # the build directory in case a future CMake update changes staging behavior.
        normal = stage_dir / NORMAL_BINARY
        safe = stage_dir / SAFE_BINARY
        if not normal.is_file() or not safe.is_file():
            build_normal = build_dir / NORMAL_BINARY
            build_safe = build_dir / SAFE_BINARY
            if build_normal.is_file() and build_safe.is_file():
                stage_dir.mkdir(parents=True, exist_ok=True)
                shutil.copy2(build_normal, normal)
                shutil.copy2(build_safe, safe)
            else:
                raise InstallerError(
                    "The build finished, but one or both client executables were not produced."
                )
        return stage_dir

    @staticmethod
    def write_executable(path: Path, content: str, dry_run: bool) -> None:
        if dry_run:
            print(f"Would write executable: {path}")
            return
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    def install_files(self, stage_dir: Path, source_dir: Path) -> tuple[Path, Path]:
        bin_dir = self.install_dir / "bin"
        docs_dir = self.install_dir / "share" / "doc"
        icon_dir = self.install_dir / "share" / "icons"

        self.info(f"\nInstalling into: {self.install_dir}")
        if not self.args.dry_run:
            bin_dir.mkdir(parents=True, exist_ok=True)
            docs_dir.mkdir(parents=True, exist_ok=True)
            icon_dir.mkdir(parents=True, exist_ok=True)

        installed_binaries: list[Path] = []
        for binary_name in (NORMAL_BINARY, SAFE_BINARY):
            source_binary = stage_dir / binary_name
            target_binary = bin_dir / binary_name
            if self.args.dry_run:
                self.info(f"Would copy {source_binary} -> {target_binary}")
            else:
                shutil.copy2(source_binary, target_binary)
                target_binary.chmod(
                    target_binary.stat().st_mode
                    | stat.S_IXUSR
                    | stat.S_IXGRP
                    | stat.S_IXOTH
                )
            installed_binaries.append(target_binary)

        for doc_name in ("README.md", "LICENSE_NOTES.md", "SECURITY.md"):
            source_doc = source_dir / doc_name
            if source_doc.is_file():
                if self.args.dry_run:
                    self.info(f"Would copy {source_doc} -> {docs_dir / doc_name}")
                else:
                    shutil.copy2(source_doc, docs_dir / doc_name)

        icon_source = (
            source_dir
            / "resources"
            / "icons"
            / "angry_orc_warrior_avatar_icon.png"
        )
        installed_icon = icon_dir / "ardabest-client.png"
        if icon_source.is_file():
            if self.args.dry_run:
                self.info(f"Would copy {icon_source} -> {installed_icon}")
            else:
                shutil.copy2(icon_source, installed_icon)

        if self.args.keep_source:
            source_copy = self.install_dir / "source"
            if self.args.dry_run:
                self.info(f"Would keep source at {source_copy}")
            else:
                if source_copy.exists():
                    shutil.rmtree(source_copy)
                shutil.copytree(source_dir, source_copy, symlinks=True)

        return installed_binaries[0], installed_binaries[1]

    def create_launchers(self, normal_binary: Path, safe_binary: Path) -> tuple[Path, Path]:
        normal_launcher = self.user_bin / "ardabest-client"
        safe_launcher = self.user_bin / "ardabest-client-safe"

        self.write_executable(
            normal_launcher,
            "#!/usr/bin/env sh\n"
            f'exec {shlex.quote(str(normal_binary))} "$@"\n',
            self.args.dry_run,
        )
        self.write_executable(
            safe_launcher,
            "#!/usr/bin/env sh\n"
            f'exec {shlex.quote(str(safe_binary))} "$@"\n',
            self.args.dry_run,
        )
        return normal_launcher, safe_launcher

    def create_desktop_entry(self, normal_binary: Path, source_dir: Path) -> Path:
        desktop_path = self.applications_dir / "ardabest-client.desktop"
        installed_icon = self.install_dir / "share" / "icons" / "ardabest-client.png"
        icon_value = str(installed_icon) if installed_icon.exists() or self.args.dry_run else "utilities-terminal"
        desktop = (
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Version=1.0\n"
            f"Name={APP_DISPLAY_NAME}\n"
            "Comment=Qt-based MUD client with triggers and an automapper\n"
            f"Exec={normal_binary}\n"
            f"Icon={icon_value}\n"
            "Terminal=false\n"
            "Categories=Game;Network;\n"
            "StartupNotify=true\n"
        )
        if self.args.dry_run:
            self.info(f"Would write desktop entry: {desktop_path}")
        else:
            desktop_path.parent.mkdir(parents=True, exist_ok=True)
            desktop_path.write_text(desktop, encoding="utf-8")
            desktop_path.chmod(0o644)
            update_database = shutil.which("update-desktop-database")
            if update_database:
                self.run([update_database, str(self.applications_dir)], check=False)
        return desktop_path

    def launch(self, normal_binary: Path, safe_binary: Path) -> None:
        if self.args.no_launch:
            return
        chosen = safe_binary if self.args.safe else normal_binary
        if self.args.dry_run:
            self.info(f"Would launch: {chosen}")
            return

        if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
            self.info("No graphical desktop session was detected, so the client was not launched.")
            return

        self.info(f"Launching {'safe' if self.args.safe else 'normal'} client...")
        try:
            subprocess.Popen(
                [str(chosen)],
                cwd=str(self.install_dir),
                env=self.env,
                start_new_session=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        except OSError as exc:
            self.info(f"The installation succeeded, but automatic launch failed: {exc}")

    def perform(self) -> None:
        self.info("=" * 68)
        self.info(f"{APP_DISPLAY_NAME} — Linux all-in-one installer")
        self.info("=" * 68)
        self.require_linux()
        self.install_system_packages()
        self.check_build_tools()

        with tempfile.TemporaryDirectory(prefix="ardabest-installer-") as temp_name:
            temp_dir = Path(temp_name)
            archive = temp_dir / "source.zip"
            extract_dir = temp_dir / "extract"
            extract_dir.mkdir(parents=True, exist_ok=True)

            self.download_archive(archive)
            if self.args.dry_run:
                source_dir = extract_dir / "BIG-JIMMY-DONG-main"
            else:
                self.info("Extracting source...")
                source_dir = self.safe_extract_zip(archive, extract_dir)
                self.validate_source(source_dir)

            stage_dir = self.configure_and_build(source_dir, temp_dir)
            normal_binary, safe_binary = self.install_files(stage_dir, source_dir)
            normal_launcher, safe_launcher = self.create_launchers(normal_binary, safe_binary)
            desktop_entry = self.create_desktop_entry(normal_binary, source_dir)

        self.info("\n" + "=" * 68)
        self.info("INSTALLATION COMPLETE")
        self.info("=" * 68)
        self.info(f"Normal client: {normal_launcher}")
        self.info(f"Safe client:   {safe_launcher}")
        self.info(f"Desktop entry: {desktop_entry}")
        self.info("\nRun from a terminal with:")
        self.info("  ardabest-client")
        self.info("or:")
        self.info("  ardabest-client-safe")
        if str(self.user_bin) not in os.environ.get("PATH", "").split(os.pathsep):
            self.info(
                f"\nNote: {self.user_bin} is not currently in PATH. You can run the full "
                f"launcher path above, or log out and back in after adding it to PATH."
            )
        self.launch(normal_binary, safe_binary)


def parse_arguments(argv: Iterable[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download, build, and install the ArdaBest MUD Client on Linux.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--install-dir",
        type=Path,
        default=DEFAULT_INSTALL_DIR,
        help="Per-user directory for the compiled client",
    )
    parser.add_argument("--yes", "-y", action="store_true", help="Accept installer prompts")
    parser.add_argument(
        "--skip-packages",
        action="store_true",
        help="Do not invoke the Linux package manager",
    )
    parser.add_argument(
        "--keep-source",
        action="store_true",
        help="Keep the downloaded source under the installation directory",
    )
    parser.add_argument(
        "--safe",
        action="store_true",
        help="Launch ardabest_client_safe instead of the normal client",
    )
    parser.add_argument(
        "--no-launch",
        action="store_true",
        help="Install the client without starting it",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print planned actions and commands without changing the computer",
    )
    return parser.parse_args(argv)


def main(argv: Iterable[str] | None = None) -> int:
    args = parse_arguments(argv)
    try:
        Installer(args).perform()
        return 0
    except KeyboardInterrupt:
        print("\nInstallation cancelled.", file=sys.stderr)
        return 130
    except InstallerError as exc:
        print(f"\nERROR: {exc}", file=sys.stderr)
        return 1
    except Exception as exc:  # Defensive final error boundary with a useful message.
        print(f"\nUNEXPECTED ERROR: {type(exc).__name__}: {exc}", file=sys.stderr)
        print("No user profile data was intentionally removed.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
