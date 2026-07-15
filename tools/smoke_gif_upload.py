#!/usr/bin/env python3
"""Hardware regression test for compact two-frame GIF uploads."""

import argparse
import base64
import http.client
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid


COMPACT_TWO_FRAME_GIF = base64.b64decode(
    "R0lGODlhEAAQAIEAABC0WgAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQADAAAACwAAAAAEAAQAAAIHQABCBxIsKDBgwgTKlzIsKHDhxAjSpxIsaLFgQEBACH5BAAMAAAALAAAAAAQABAAgfBaUAAAAAAAAAAAAAgdAAEIHEiwoMGDCBMqXMiwocOHECNKnEixosWBAQEAOw=="
)
CODEX_FRAME_BYTES = 120 * 120 * 2


def request(base_url, path, method="GET", body=None, content_type=None, timeout=30,
            allowed_statuses=()):
    headers = {}
    if content_type:
        headers["Content-Type"] = content_type
    req = urllib.request.Request(
        f"{base_url.rstrip('/')}/{path.lstrip('/')}",
        data=body,
        headers=headers,
        method=method,
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as exc:
        payload = exc.read()
        if exc.code in allowed_statuses:
            return exc.code, payload
        detail = payload.decode("utf-8", errors="replace")
        raise RuntimeError(f"{path} returned HTTP {exc.code}: {detail}") from exc


def upload_body(gif_data):
    boundary = f"aiclock-smoke-{uuid.uuid4().hex}"
    body = (
        f"--{boundary}\r\n"
        'Content-Disposition: form-data; name="file"; filename="compact.gif"\r\n'
        "Content-Type: image/gif\r\n\r\n"
    ).encode("ascii")
    body += gif_data + f"\r\n--{boundary}--\r\n".encode("ascii")
    return body, f"multipart/form-data; boundary={boundary}"


def get_info(base_url):
    _, payload = request(base_url, "/api/info", timeout=5)
    return json.loads(payload)


def get_codex_raw(base_url, attempts=3):
    last_error = None
    for attempt in range(attempts):
        try:
            _, payload = request(base_url, "/sprite/codex/raw", timeout=30)
            return payload
        except (http.client.IncompleteRead, urllib.error.URLError,
                TimeoutError, ConnectionError) as exc:
            last_error = exc
            if attempt + 1 < attempts:
                time.sleep(0.5)
    raise RuntimeError(f"raw animation stream failed after {attempts} attempts: {last_error}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--cas-only", action="store_true")
    parser.add_argument("--confirm-device-mutation", action="store_true")
    args = parser.parse_args()
    if not args.confirm_device_mutation:
        parser.error("--confirm-device-mutation is required because this test uploads and resets the Codex sprite")
    base_url = args.host if args.host.startswith("http") else f"http://{args.host}"

    before = get_info(base_url)
    codex_custom = before.get("codex", {}).get("custom_sprite")
    claude_custom = before.get("claude", {}).get("custom_sprite")
    before_revision = before.get("sprite_rev")
    if codex_custom is not False:
        raise RuntimeError("Codex custom animation state is missing or already enabled")
    if not isinstance(claude_custom, bool):
        raise RuntimeError("Claude custom animation state is missing")
    if not isinstance(before_revision, int):
        raise RuntimeError("sprite revision is missing")

    for invalid_revision in ("", "-1", "abc", "4294967296"):
        invalid_body = urllib.parse.urlencode({"expected_rev": invalid_revision}).encode("ascii")
        status, _ = request(
            base_url,
            "/sprite/codex/reset",
            "POST",
            invalid_body,
            "application/x-www-form-urlencoded",
            timeout=10,
            allowed_statuses=(400,),
        )
        if status != 400:
            raise RuntimeError(f"invalid revision {invalid_revision!r} returned HTTP {status}")
    after_invalid = get_info(base_url)
    if after_invalid.get("sprite_rev") != before_revision \
            or after_invalid.get("codex", {}).get("custom_sprite") is not False \
            or after_invalid.get("claude", {}).get("custom_sprite") != claude_custom:
        raise RuntimeError("invalid revision changed animation state")

    cas_probe = urllib.parse.urlencode({"expected_rev": before_revision}).encode("ascii")
    request(
        base_url,
        "/sprite/codex/reset",
        "POST",
        cas_probe,
        "application/x-www-form-urlencoded",
        timeout=10,
    )
    after_probe = get_info(base_url)
    if after_probe.get("sprite_rev") != before_revision + 1 \
            or after_probe.get("codex", {}).get("custom_sprite") is not False \
            or after_probe.get("claude", {}).get("custom_sprite") != claude_custom:
        raise RuntimeError("CAS probe reset changed unexpected animation state")
    status, _ = request(
        base_url,
        "/sprite/codex/reset",
        "POST",
        cas_probe,
        "application/x-www-form-urlencoded",
        timeout=10,
        allowed_statuses=(409,),
    )
    if status != 409:
        raise RuntimeError(f"stale CAS probe was accepted with HTTP {status}")
    before = get_info(base_url)
    before_revision = before.get("sprite_rev")
    if before_revision != after_probe.get("sprite_rev"):
        raise RuntimeError("rejected CAS probe changed the animation revision")
    if args.cas_only:
        print("PASS sprite reset CAS: invalid revisions rejected, stale revision returned 409")
        return

    body, content_type = upload_body(COMPACT_TWO_FRAME_GIF)
    owned_raw = None
    owned_revision = before_revision + 1
    cleanup_skipped = None
    try:
        request(base_url, "/sprite/codex", "POST", body, content_type, timeout=60)
        raw = get_codex_raw(base_url)
        expected_size = 1 + 2 * CODEX_FRAME_BYTES
        if len(raw) != expected_size or raw[0] != 2:
            raise RuntimeError(
                f"decoded animation mismatch: frames={raw[0] if raw else 0} "
                f"bytes={len(raw)} expected={expected_size}"
            )
        owned_raw = raw
        uploaded = get_info(base_url)
        if uploaded.get("sprite_rev") != owned_revision \
                or uploaded.get("codex", {}).get("custom_sprite") is not True:
            raise RuntimeError("uploaded animation revision/state mismatch")
        bad_body, bad_content_type = upload_body(b"not a gif")
        try:
            request(base_url, "/sprite/codex", "POST", bad_body, bad_content_type, timeout=60)
            raise RuntimeError("invalid GIF was accepted")
        except RuntimeError as exc:
            if "HTTP 500" not in str(exc):
                raise
        raw_after_failure = get_codex_raw(base_url)
        if raw_after_failure != raw:
            raise RuntimeError("failed upload replaced the previous valid animation")
        if get_info(base_url).get("sprite_rev") != owned_revision:
            raise RuntimeError("failed upload changed the animation revision")

        stale_reset = urllib.parse.urlencode({"expected_rev": before_revision}).encode("ascii")
        status, _ = request(
            base_url,
            "/sprite/codex/reset",
            "POST",
            stale_reset,
            "application/x-www-form-urlencoded",
            timeout=10,
            allowed_statuses=(409,),
        )
        if status != 409:
            raise RuntimeError(f"stale animation reset was accepted with HTTP {status}")
        stale_state = get_info(base_url)
        raw_after_stale_reset = get_codex_raw(base_url)
        if stale_state.get("sprite_rev") != owned_revision \
                or stale_state.get("codex", {}).get("custom_sprite") is not True \
                or raw_after_stale_reset != owned_raw:
            raise RuntimeError("stale reset changed the active animation")
    finally:
        if owned_raw is not None:
            current = get_info(base_url)
            current_raw = get_codex_raw(base_url)
            if current.get("sprite_rev") == owned_revision \
                    and current.get("codex", {}).get("custom_sprite") is True \
                    and current_raw == owned_raw:
                reset_body = urllib.parse.urlencode({"expected_rev": owned_revision}).encode("ascii")
                status, _ = request(
                    base_url,
                    "/sprite/codex/reset",
                    "POST",
                    reset_body,
                    "application/x-www-form-urlencoded",
                    timeout=10,
                    allowed_statuses=(409,),
                )
                if status == 409:
                    cleanup_skipped = "animation changed concurrently; reset was rejected"
            else:
                cleanup_skipped = "animation changed concurrently; reset was skipped"

    if cleanup_skipped:
        raise RuntimeError(cleanup_skipped)
    after = get_info(base_url)
    if after.get("codex", {}).get("custom_sprite") is not False:
        raise RuntimeError("Codex animation did not reset")
    if after.get("sprite_rev") != owned_revision + 1:
        raise RuntimeError("successful reset did not advance the animation revision once")
    if after.get("claude", {}).get("custom_sprite") != claude_custom:
        raise RuntimeError("Claude animation changed during Codex smoke test")
    print("PASS compact GIF: stale reset rejected, 2 frames preserved, Codex reset, Claude preserved")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        sys.exit(1)
