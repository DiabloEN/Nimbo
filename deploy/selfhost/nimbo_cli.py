#!/usr/bin/python3 -I
"""Forward a host-wide nimbo command to a registered instance's own CLI."""

import json
import os
from pathlib import Path
import sys
import tempfile

REGISTRY = Path("/var/lib/nimbo-installer/instances.json")
COMMAND = Path("/usr/local/bin/nimbo")


def install_launcher(destination=COMMAND):
    source = Path(__file__).read_bytes()
    destination.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
    output = tempfile.NamedTemporaryFile(dir=destination.parent, delete=False)
    temporary = Path(output.name)
    try:
        with output:
            output.write(source)
        temporary.chmod(0o755)
        try:
            os.link(temporary, destination)
        except FileExistsError:
            return (
                not destination.is_symlink()
                and destination.is_file()
                and destination.read_bytes() == source
                and os.access(destination, os.X_OK)
            )
    finally:
        temporary.unlink()
    return True


def resolve_command(arguments, records):
    if not records:
        raise ValueError("Установок Nimbo. в реестре нет.")
    if arguments[:1] == ["--instance"]:
        if len(arguments) < 2 or arguments[1] not in records:
            raise ValueError("Выберите установку: " + ", ".join(sorted(records)))
        project = arguments[1]
        arguments = arguments[2:]
    elif len(records) == 1:
        project = next(iter(records))
    else:
        names = sorted(records)
        message = "Установки: " + ", ".join(names)
        message += f"\nУкажите нужную, например: sudo nimbo --instance {names[0]} status"
        if arguments in ([], ["help"]):
            print(message)
            return None
        raise ValueError(message)
    if len(arguments) > 1:
        raise ValueError("Укажите одну команду. Список команд: sudo nimbo help")
    return [str(Path(records[project]["directory"]) / "nimbo"), *(arguments or ["help"])]


def main():
    if os.geteuid() != 0:
        raise ValueError("Запустите команду через sudo.")
    if sys.argv[1:] == ["--install"]:
        if not install_launcher():
            raise ValueError(f"Путь {COMMAND} уже занят другой командой.")
        return
    records = json.loads(REGISTRY.read_text()) if REGISTRY.exists() else {}
    command = resolve_command(sys.argv[1:], records)
    if command:
        executable = Path(command[0])
        for path in (*reversed(executable.parents), executable):
            info = path.stat()
            if path.is_symlink() or info.st_uid != 0 or info.st_mode & 0o022:
                raise ValueError(f"Небезопасный путь к команде установки: {path}")
        os.execv(executable, command)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as error:
        sys.exit(str(error))
