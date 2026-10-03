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

        convert("eboot.self", b"\x4f\x15\x3d\x1d" + bytes([0, 0, 1]) + bytes(0x1000 - 7), "Encrypted PS4 retail SELF")
        convert("eboot_ps5.self", b"\x54\x14\xf5\xee" + bytes([0, 0, 1]) + bytes(0x1000 - 7), "Encrypted PS5 retail SELF")
        convert("eboot.pkg", b"\x7fCNT" + bytes(0x1000), "Package is protected retail content")
        convert("eboot.fpkg", b"\x7fCNT\x80\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x00\x00\x00\x10\x00" + bytes(40) + b"EP4350-CUSA00001_00-0000000000000000" + bytes(400), "requires an external extractor")
        convert("eboot.short", b"\x7fEL", "File is too small")
    print("Input magic diagnostics tests passed")


if __name__ == "__main__":
    main()
