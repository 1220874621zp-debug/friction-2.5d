#!/usr/bin/env python3
"""
Friction 2.5D - Model Context Protocol (MCP) Stdio Bridge
Allows external AI Agents (Claude Desktop, Cursor, Antigravity, OpenCode, AutoGPT)
to interact with and control Friction 2.5D via standard MCP protocol over stdio.
"""

import sys
import json
import socket
import argparse
import urllib.request
import urllib.error
import os

DEFAULT_HTTP_PORT = 9527
DEFAULT_SOCKET_PATH = r"\\.\pipe\friction_mcp" if sys.platform == "win32" else "/tmp/friction_mcp.sock"

def send_via_socket(socket_path: str, req_obj: dict) -> dict:
    if sys.platform == "win32":
        with open(socket_path, "r+b", buffering=0) as pipe:
            pipe.write((json.dumps(req_obj) + "\n").encode("utf-8"))
            res_line = pipe.readline().decode("utf-8")
            return json.loads(res_line)
    else:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.connect(socket_path)
            s.sendall((json.dumps(req_obj) + "\n").encode("utf-8"))
            res_data = b""
            while True:
                chunk = s.recv(4096)
                if not chunk:
                    break
                res_data += chunk
                if b"\n" in res_data:
                    break
            return json.loads(res_data.decode("utf-8").strip())

def send_via_http(port: int, req_obj: dict) -> dict:
    url = f"http://127.0.0.1:{port}/mcp"
    req_data = json.dumps(req_obj).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=req_data,
        headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req, timeout=10) as resp:
        return json.loads(resp.read().decode("utf-8"))

def forward_request(req_obj: dict, port: int, socket_path: str) -> dict:
    # 1. Try local Unix socket / Windows pipe first
    if os.path.exists(socket_path) or sys.platform == "win32":
        try:
            return send_via_socket(socket_path, req_obj)
        except Exception:
            pass

    # 2. Try HTTP endpoint fallback
    try:
        return send_via_http(port, req_obj)
    except Exception as e:
        req_id = req_obj.get("id")
        return {
            "jsonrpc": "2.0",
            "id": req_id,
            "error": {
                "code": -32000,
                "message": f"Cannot connect to Friction 2.5D instance: {str(e)}. Please ensure Friction is running with AI Server enabled in Preferences."
            }
        }

def main():
    parser = argparse.ArgumentParser(description="Friction 2.5D MCP Stdio Bridge")
    parser.add_argument("--port", type=int, default=DEFAULT_HTTP_PORT, help="Friction TCP HTTP port")
    parser.add_argument("--socket", type=str, default=DEFAULT_SOCKET_PATH, help="Friction IPC socket path")
    args = parser.parse_args()

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req_obj = json.loads(line)
        except Exception:
            continue

        resp_obj = forward_request(req_obj, args.port, args.socket)
        sys.stdout.write(json.dumps(resp_obj) + "\n")
        sys.stdout.flush()

if __name__ == "__main__":
    main()
