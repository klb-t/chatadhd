"""Private POSIX resource launcher. No shell, providers, or parent preexec hook."""
import json
import os
import resource
import sys


def main():
    limits = json.loads(sys.argv[1])
    # The parent supplies only validated positive integers and an absolute tool.
    for key, limit in (
        (resource.RLIMIT_CPU, limits["cpu_seconds"]),
        (resource.RLIMIT_AS, limits["address_space_bytes"]),
        (resource.RLIMIT_FSIZE, limits["file_bytes"]),
        (resource.RLIMIT_CORE, 0),
    ):
        resource.setrlimit(key, (limit, limit))
    # Keep inherited environment (including credentials/proxies) out of codecs.
    env = {"PATH": os.defpath, "LC_ALL": "C", "LANG": "C",
           "OMP_NUM_THREADS": str(limits["threads"]),
           "OPENBLAS_NUM_THREADS": str(limits["threads"])}
    os.execve(sys.argv[2], sys.argv[2:], env)


if __name__ == "__main__":
    main()
