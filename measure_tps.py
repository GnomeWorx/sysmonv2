#!/usr/bin/env python3
"""Measure llama-server TPS via its own timing report.

Hits the local llama.cpp /completion endpoint and prints the server's
reported generation speed (tokens/sec) from the response timings. Falls
back to 0.0 on any failure.
"""
import json
import sys
import urllib.request

# llama.cpp server (NOT OpenWebUI on 8080)
LLAMA_URL = "http://127.0.0.1:8081/completion"

payload = json.dumps({
    "prompt": "say the word hello",
    "n_predict": 12,
    "temperature": 0,
}).encode()

try:
    req = urllib.request.Request(
        LLAMA_URL, data=payload,
        headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req, timeout=20) as resp:
        data = json.loads(resp.read())
    timings = data.get("timings", {})
    tps = timings.get("predicted_per_second", 0.0)
    print(f"{tps:.1f}")
except Exception:
    print("0.0")
