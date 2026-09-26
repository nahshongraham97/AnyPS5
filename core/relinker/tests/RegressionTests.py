import argparse
import os
from pathlib import Path
import shutil
import signal
import struct
import subprocess
import tempfile
import unittest


def elf(code=b"\x90\xc3", phoff=64, segment_offset=120, segment_size=None):
    size = len(code) if segment_size is None else segment_size
    ident = b"\x7fELF\x02\x01\x01" + bytes(9)
    header = struct.pack("<16sHHIQQQIHHHHHH", ident, 3, 62, 1,
                         0x1000, phoff, 0, 0, 64, 56, 1, 64, 0, 0)
    segment = struct.pack("<IIQQQQQQ", 1, 5, segment_offset, 0x1000,
                          0x1000, size, size, 1)
    return header + segment + code


def dynamic_elf(overrides=None, code=b"\x31\xc0\xc3"):
    tags = {
        0x61000027: 0x800,
        0x6100002D: 0,
        0x61000035: 0x900,
        0x61000037: 1,
        0x61000039: 0x920,
        0x6100003B: 24,
        0x6100002B: 7,
        0x61000029: 0x980,
        0x6100002F: 0x980,
        0x61000031: 0,
        0x61000033: 24,
    }
    tags.update(overrides or {})
    dynamic = b"".join(struct.pack("<qQ", tag, value) for tag, value in tags.items())
    dynamic += bytes(16)
    data = bytearray(0x1000 + len(code))
    data[:64] = struct.pack("<16sHHIQQQIHHHHHH",
                            b"\x7fELF\x02\x01\x01" + bytes(9),
                            0xFE10, 62, 1, 0x1000, 64, 0, 0, 64, 56, 8, 64, 0, 0)
    headers = [
        (1, 6, 0, 0, 0, 0x1000, 0x1000, 0x1000),
        (1, 5, 0x1000, 0x1000, 0x1000, len(code), len(code), 0x1000),
        (2, 6, 0x600, 0x600, 0x600, len(dynamic), len(dynamic), 8),
    ] + [(0x61000000, 0, 0, 0, 0, 0, 0, 1)] * 5
    for index, header in enumerate(headers):
        struct.pack_into("<IIQQQQQQ", data, 64 + index * 56, *header)
    data[0x600:0x600 + len(dynamic)] = dynamic
    data[0x1000:] = code
    return data


class RegressionTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="anyps5-regression-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def convert(self, data, *options):
        source = self.root / "input.elf"
        output = self.root / "output.bin"
        source.write_bytes(data)
        result = subprocess.run([RELINKER, *options, str(source), str(output)],
                                capture_output=True, text=True, timeout=20)
        return result, output

    def assert_rejected(self, data, *options):
        result, output = self.convert(data, *options)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("FAIL:", result.stderr)
        self.assertFalse(output.exists(), result.stdout + result.stderr)

    def test_intel_passthrough_exits_successfully(self):
        data = elf()
        result, output = self.convert(data, "--to-intel")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(output.read_bytes(), data)
        self.assertNotIn("System: Linux", result.stdout)

    def test_intel_rejects_placeholder_replacements(self):
        for code in (b"\x0f\x01\xfa", b"\x0f\x01\xfb", b"\x0f\x01\xfc",
                     b"\x0f\x01\xfd", b"\xf3\x0f\x01\xfa"):
            with self.subTest(instruction=code.hex()):
                self.assert_rejected(elf(code + b"\xc3"), "--to-intel")

    def test_intel_rejects_sse4a(self):
        for code in (b"\x66\x0f\x78\xc0\x01\x02", b"\xf2\x0f\x79\xc0",
                     b"\xf3\x0f\x2b\x00", b"\xf2\x0f\x2b\x00"):
            with self.subTest(instruction=code.hex()):
                self.assert_rejected(elf(code + b"\xc3"), "--to-intel")

    def test_rejection_preserves_existing_output(self):
        output = self.root / "output.bin"
        output.write_bytes(b"previous output")
        result, output = self.convert(elf(b"\x0f\x01\xfc\xc3"), "--to-intel")
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertEqual(output.read_bytes(), b"previous output")

    def test_program_header_offset_overflow(self):
        self.assert_rejected(elf(phoff=2**64 - 1))

    def test_program_header_table_truncated(self):
        data = bytearray(elf())
        struct.pack_into("<H", data, 56, 2)
        self.assert_rejected(data)

    def test_program_header_stride_too_small(self):
        data = bytearray(elf())
        struct.pack_into("<H", data, 54, 1)
        self.assert_rejected(data, "--to-intel")

    def test_intel_segment_outside_file(self):
        for offset, size in ((4096, 16), (2**64 - 1, 16), (120, 2**64 - 1)):
            with self.subTest(offset=offset, size=size):
                self.assert_rejected(elf(segment_offset=offset, segment_size=size), "--to-intel")

    def test_intel_truncated_instruction(self):
        self.assert_rejected(elf(b"\x0f"), "--to-intel")

    def test_unsupported_elf_format(self):
        for offset, value in ((4, 1), (5, 2), (6, 0), (18, 3)):
            with self.subTest(offset=offset):
                data = bytearray(elf())
                data[offset] = value
                self.assert_rejected(data, "--to-intel")

    def test_dynamic_segment_outside_file(self):
        data = dynamic_elf()
        struct.pack_into("<Q", data, 64 + 2 * 56 + 8, 2**64 - 1)
        self.assert_rejected(data)

    def test_dynamic_segment_missing_terminator(self):
        data = dynamic_elf()
        struct.pack_into("<Q", data, 64 + 2 * 56 + 32, 16)
        self.assert_rejected(data)

    def test_relocation_range_overflow(self):
        self.assert_rejected(dynamic_elf({0x6100002F: 2**64 - 1, 0x61000031: 24}))

    def test_partial_relocation(self):
        self.assert_rejected(dynamic_elf({0x61000031: 1}))

    def test_string_table_outside_file(self):
        self.assert_rejected(dynamic_elf({0x61000035: 2**64 - 1, 0x61000037: 2}))

    def test_symbol_offset_overflow(self):
        data = dynamic_elf({0x61000039: 2**64 - 1, 0x61000031: 24})
        struct.pack_into("<QQq", data, 0x980, 0x800, 6, 0)
        self.assert_rejected(data)

    def test_linux_conversion_smoke(self):
        result, output = self.convert(dynamic_elf())
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        data = output.read_bytes()
        self.assertEqual(data[:4], b"\x7fELF")
        self.assertEqual(struct.unpack_from("<H", data, 16)[0], 3)
        self.assertGreater(len(data), 0x1003)

    def test_windows_conversion_smoke(self):
        result, output = self.convert(dynamic_elf(), "--windows")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        data = output.read_bytes()
        self.assertEqual(data[:2], b"MZ")
        pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
        self.assertEqual(data[pe_offset:pe_offset + 4], b"PE\0\0")
        self.assertEqual(struct.unpack_from("<H", data, pe_offset + 4)[0], 0x8664)

    def test_linux_requires_all_synthetic_header_slots(self):
        data = dynamic_elf()
        struct.pack_into("<H", data, 56, 6)
        self.assert_rejected(data)

    def run_child(self, name, env=None):
        child = self.root / (name + Path(AUTORUN_PROBE).suffix)
        shutil.copy2(AUTORUN_PROBE, child)
        return subprocess.run([AUTORUN_PROBE, str(child)], input="\n\n", env=env,
                              capture_output=True, text=True, timeout=20)

    def test_autorun_preserves_exit_status(self):
        result = self.run_child("child")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_autorun_literal_filename(self):
        result = self.run_child("child $(echo wrong) %PATH% with spaces")
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    @unittest.skipIf(os.name == "nt", "POSIX signal exit status")
    def test_autorun_signal_status(self):
        env = dict(os.environ, ANYPS5_TEST_CHILD_SIGNAL="1")
        result = self.run_child("signal-child", env=env)
        self.assertEqual(result.returncode, 128 + signal.SIGTERM, result.stdout + result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--relinker", required=True)
    parser.add_argument("--autorun-probe", required=True)
    options, remaining = parser.parse_known_args()
    RELINKER = str(Path(options.relinker).resolve())
    AUTORUN_PROBE = str(Path(options.autorun_probe).resolve())
    unittest.main(argv=[__file__, *remaining])
