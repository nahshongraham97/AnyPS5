"""Verify Windows code analysis derives the dynamic symbol count from GNU_HASH."""

from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from test_optional_plt import fixture


def gnu_hash_fixture(symbol_index, bucket_count=1, bucket_value=1, chain_value=1):
    image = fixture(extra_tags=[(0x6FFFFEF5, 0x650), (1, 5)])
    # Keep DT_HASH and DT_OS_SYMTABSZ absent: GNU_HASH must provide the count.
    struct.pack_into("<Q", image, 0x400 + 5 * 16 + 8, 48)
    struct.pack_into("<Q", image, 0x400 + 1 * 16 + 8, 13)
    image[0x600:0x60D] = b"\0foo\0libc.so\0"
    struct.pack_into("<I", image, 0x620, 1)
    struct.pack_into("<IBBHQQ", image, 0x638, 1, 0x12, 0, 1, 0x210, 6)
    struct.pack_into("<IIII", image, 0x650, bucket_count, 1, 1, 0)
    struct.pack_into("<Q", image, 0x660, 0)
    struct.pack_into("<II", image, 0x668, bucket_value, chain_value)
    struct.pack_into("<QQq", image, 0x718, 0x308, (symbol_index << 32) | 1, 0)
    return image


def main():
    relinker = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="anyps5-reloc-symbol-") as directory:
        work = Path(directory)
        for name, symbol_index, bucket_count, bucket_value, chain_value, error in (
            ("null-symbol", 0, 1, 1, 1, "invalid relocation symbol"),
            ("past-end", 2, 1, 1, 1, "invalid relocation symbol"),
            ("bucket-overrun", 1, 0xFFFFFFFF, 1, 1, "invalid GNU hash table"),
            ("chain-overrun", 1, 1, 0xFFFFFFFF, 1, "invalid GNU hash table"),
        ):
            source = work / (name + ".elf")
            output = work / (name + ".exe")
            source.write_bytes(gnu_hash_fixture(symbol_index, bucket_count, bucket_value, chain_value))
            result = subprocess.run(
                [str(relinker), "--windows", "--windows-diagnostics", "--skip-syscall-check",
                 "--skip-sce-module",
                 str(source), str(output)],
                capture_output=True, text=True, timeout=30, cwd=work)
            assert result.returncode == 2, (name, result.stdout, result.stderr)
            assert f"Code analysis: {error}" in result.stderr, (name, result.stdout, result.stderr)
            assert not output.exists(), name
        source = work / "valid-symbol.elf"
        output = work / "valid-symbol.exe"
        source.write_bytes(gnu_hash_fixture(1))
        result = subprocess.run(
            [str(relinker), "--windows", "--windows-diagnostics", "--skip-syscall-check",
             "--skip-sce-module",
             str(source), str(output)],
            capture_output=True, text=True, timeout=30, cwd=work)
        assert result.returncode == 0, ("valid-symbol", result.stdout, result.stderr)
        assert output.read_bytes()[:2] == b"MZ", "valid-symbol"
    print("Windows relocation symbol tests passed")


if __name__ == "__main__":
    main()
