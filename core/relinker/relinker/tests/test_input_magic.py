"""Diagnose malformed SELF images and inputs without ELF or SELF magic."""

from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    relinker = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="anyps5-magic-") as directory:
        work = Path(directory)

        def convert(name, content, error):
            source = work / name
            source.write_bytes(content)
            output = work / (name + ".out")
            result = subprocess.run([str(relinker), "--skip-sce-module", str(source), str(output)],
                                    capture_output=True, text=True, timeout=20)
            assert result.returncode == 2 and error in result.stderr and not output.exists(), (name, result)

        convert("eboot.self", b"\x4f\x15\x3d\x1d" + bytes(0x1000), "Invalid SELF segment table")
        convert("eboot.pkg", b"\x7fCNT" + bytes(0x1000), "Input is neither a raw ELF nor a recognized SELF image")
        convert("eboot.short", b"\x7fEL", "Input is neither a raw ELF nor a recognized SELF image")
    print("Input magic diagnostics tests passed")


if __name__ == "__main__":
    main()
