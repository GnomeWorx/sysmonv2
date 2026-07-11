#!/usr/bin/env python3
"""Measure Ollama model TPS by timing a tiny inference request."""
import json
import time
import urllib.request
import sys

OLLAMA_URL = "http://127.0.0.1:11434/api/generate"
MODEL = "qwen2.5-coder:14b-65k"

payload = json.dumps({
    "model": MODEL,
    "prompt": "hi",
    "stream": False,
    "options": {"num_predict": 1}
}).encode()

start = time.time()
try:
    req = urllib.request.Request(OLLAMA_URL, data=payload, headers={"Content-Type": "application/json"})
    resp = urllib.request.urlopen(req, timeout=10)
    data = json.loads(resp.read())
    elapsed = time.time() - start
    total_duration = data.get("total_duration", 0) / 1e9  # ns to seconds
    eval_count = data.get("eval_count", 0)
    eval_duration = data.get("eval_duration", 0) / 1e9  # ns to seconds

    if eval_duration > 0:
        tps = eval_count / eval_duration
    elif elapsed > 0 and eval_count > 0:
        tps = eval_count / elapsed
    else:
        tps = 0

    print(f"{tps:.1f}")
except Exception as e:
    print("0.0")
