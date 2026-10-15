#!/usr/bin/env python3
"""Verify Tetris Enter gestures over USB without resetting the device."""
import argparse
import json
from pathlib import Path

from serial_smoke import Device


HOME, GAME_MENU, TETRIS = 0, 11, 15
READY, PLAYING, PAUSED = 0, 1, 2


def healthy(state, baseline):
    assert state["heap"] > 50000, state
    for key in ("drops", "spi_errors"):
        assert state[key] == baseline[key] == 0, (key, baseline, state)


def games_home(device):
    device.home()
    state = device.status()
    for _ in range((-state["selected"]) % 5):
        device.key("down")
    state = device.status()
    assert state["ui"] == HOME and state["selected"] == 0, state
    return state


def exercise(device, baseline, result, passed):
    def snapshot(label, game_state, rotation, piece=None):
        state = device.status()
        result["states"].append({"step": label, **state})
        healthy(state, baseline)
        assert state["ui"] == TETRIS and state["game_state"] == game_state, state
        assert state["tetris_rotation"] == rotation, (label, state)
        assert 0 <= state["tetris_piece"] <= 6, state
        if piece is not None:
            assert state["tetris_piece"] == piece, (label, piece, state)
        return state

    device.open_home_card(0)
    menu = device.status()
    assert menu["ui"] == GAME_MENU, menu
    for _ in range((3 - menu["selected"]) % 8):
        device.key("down")
    device.key("enter")
    ready = snapshot("ready", READY, 0)
    piece = ready["tetris_piece"]

    # The console injects individual events; it does not recognize gestures.
    # Send each physical press/release and its final click/double/long event.
    for gesture in ("press", "release", "click"):
        device.key("enter", gesture)
        snapshot("start " + gesture, PLAYING, 0, piece)
    passed("Start consumes its trailing release and click without rotating")

    for gesture in ("press", "release"):
        device.key("enter", gesture)
        snapshot("single " + gesture, PLAYING, 0, piece)
    device.key("enter", "click")
    rotated = snapshot("single click", PLAYING, 1, piece)
    passed("A single click rotates exactly once after release")

    for tap in (1, 2):
        for gesture in ("press", "release"):
            device.key("enter", gesture)
            snapshot(f"double tap {tap} {gesture}", PLAYING, 1, piece)
    device.key("enter", "double")
    dropped = snapshot("double complete", PLAYING, 0)
    assert dropped["game_score"] > rotated["game_score"], (rotated, dropped)
    passed("Double click drops without an intermediate rotation or piece change")

    piece = dropped["tetris_piece"]
    device.key("enter", "press")
    snapshot("long press", PLAYING, 0, piece)
    device.key("enter", "long")
    paused = snapshot("long recognized", PAUSED, 0, piece)
    device.key("enter", "release")
    released = snapshot("long release", PAUSED, 0, piece)
    assert released["game_score"] == paused["game_score"], (paused, released)
    passed("Long press pauses without rotating; release leaves the game paused")

    for gesture in ("press", "release", "click"):
        device.key("enter", gesture)
        snapshot("resume " + gesture, PLAYING, 0, piece)
    passed("Resume consumes its trailing release and click without rotating")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--output", type=Path, default=Path("build/hardware"))
    args = parser.parse_args()
    result = {"ok": False, "checks": [], "states": []}
    device = None
    initial = None

    def passed(name):
        result["checks"].append(name)
        print("PASS " + name, flush=True)

    try:
        device = Device(args.port, args.output / "tetris-smoke.log")
        initial = device.status()
        result["initial"] = initial
        assert not initial["client"], "Close the active DAP client before running this test"
        healthy(initial, initial)
        exercise(device, initial, result, passed)
        final = games_home(device)
        healthy(final, initial)
        for key in ("motion", "brightness", "header", "wifi"):
            assert final[key] == initial[key], (key, initial, final)
        result["final"] = final
        passed("Returned to Games home card with settings, Wi-Fi and runtime health preserved")
        result["ok"] = True
    except Exception as error:
        result["error"] = str(error)
        raise
    finally:
        if device:
            try:
                final = games_home(device)
                result["final"] = final
                if initial:
                    healthy(final, initial)
                    for key in ("motion", "brightness", "header", "wifi"):
                        assert final[key] == initial[key], (key, initial, final)
            except Exception as error:
                result["cleanup_error"] = str(error)
                result["ok"] = False
            finally:
                device.close()
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / "tetris-smoke.json").write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    assert result["ok"], result


if __name__ == "__main__":
    main()
