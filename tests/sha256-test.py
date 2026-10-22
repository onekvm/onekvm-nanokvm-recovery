"""Compare SHA-256 SIMD scheduling and scalar fallback against hashlib."""
import ctypes
import hashlib
import os
import random
import sys


class Context(ctypes.Structure):
    _fields_ = [
        ("state", ctypes.c_uint32 * 8),
        ("bit_len", ctypes.c_uint64),
        ("buffer", ctypes.c_ubyte * 64),
        ("buffer_len", ctypes.c_size_t),
    ]


lib = ctypes.CDLL(sys.argv[1])
lib.recovery_sha256_init.argtypes = [ctypes.POINTER(Context)]
lib.recovery_sha256_update.argtypes = [ctypes.POINTER(Context), ctypes.c_void_p, ctypes.c_size_t]
lib.recovery_sha256_final.argtypes = [ctypes.POINTER(Context), ctypes.c_void_p]
payload = random.Random(0).randbytes(65536 + 257)
cases = list(range(261)) + [511, 512, 513, 4095, 4096, 4097, 65536, len(payload)]
for scalar in (False, True):
    if scalar:
        os.environ["ONEKVM_SHA256_SCALAR"] = "1"
    else:
        os.environ.pop("ONEKVM_SHA256_SCALAR", None)
    for size in cases:
        data = payload[:size]
        for chunk_size in (1, 63, 64, 65, 255, 256, 4096, max(size, 1)):
            ctx = Context()
            digest = (ctypes.c_ubyte * 32)()
            lib.recovery_sha256_init(ctypes.byref(ctx))
            for offset in range(0, size, chunk_size):
                chunk = data[offset:offset + chunk_size]
                lib.recovery_sha256_update(ctypes.byref(ctx), chunk, len(chunk))
            lib.recovery_sha256_final(ctypes.byref(ctx), digest)
            assert bytes(digest) == hashlib.sha256(data).digest(), (scalar, size, chunk_size)
print("SHA-256 vector/scalar and fragmented updates match hashlib")
