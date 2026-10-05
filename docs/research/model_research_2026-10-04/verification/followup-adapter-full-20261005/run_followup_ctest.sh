#!/usr/bin/env bash
set -o pipefail
env -u PYTHONPATH -u TMPDIR LOOM_CANDIDATE_GRAPH_NATIVE_TOOL=/workspace/scratch/407fe6f6ff42/build/loom_candidate_graph_native_tool /root/.local/bin/ctest --test-dir /workspace/scratch/407fe6f6ff42/build -j2 --output-on-failure --test-output-size-passed 10485760 --test-output-size-failed 10485760 --output-junit /workspace/scratch/407fe6f6ff42/native-logs/followup-adapter-final/ctest.junit.xml | tee /workspace/scratch/407fe6f6ff42/native-logs/followup-adapter-final/ctest.raw.log
ctest_status=$?
python3 /workspace/scratch/407fe6f6ff42/native-logs/audit_followup_adapter.py finish --exit-code "$ctest_status"
audit_status=$?
if [ "$ctest_status" -ne 0 ]; then exit "$ctest_status"; fi
exit "$audit_status"
