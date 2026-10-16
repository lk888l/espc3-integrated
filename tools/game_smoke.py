#!/usr/bin/env python3
"""Exercise all eight games over USB; use ESP-IDF's Python environment."""
import argparse
import json
from pathlib import Path
import time

from serial_smoke import Device


GAMES = ("Dino", "Snake", "Air Raid", "Tetris", "Pong", "Piano", "Breakout", "Flappy")
HOME, GAME_MENU = 0, 11
READY, PLAYING, PAUSED = 0, 1, 2


def healthy(state, baseline):
    assert state["heap"] > 50000, state
    assert state["ui_stack_free"] > 768, state
    for key in ("drops", "spi_errors"):
        assert state[key] == baseline[key] == 0, state


def open_game(device, index):
    state = device.status()
    assert state["ui"] == GAME_MENU, state
    for _ in range((index - state["selected"]) % len(GAMES)):
        device.key("down")
    device.key("enter")
    state = device.status()
    assert state["ui"] == 12 + index and state["game"] == index, state
    assert state["game_state"] == READY, state
    return state


def games_home(device):
    device.home()
    state = device.status()
    for _ in range((-state["selected"]) % 6):
        device.key("down")
    state = device.status()
    assert state["ui"] == HOME and state["selected"] == 0, state
    return state


def exercise(device, baseline, result, passed):
    device.open_home_card(0)
    state = device.status()
    assert state["ui"] == GAME_MENU and state["game"] == 255, state
    passed("Games home card and eight-game menu")

    for index, name in enumerate(GAMES):
        ready = open_game(device, index)
        healthy(ready, baseline)
        device.key("enter")
        started = device.status()
        assert started["game_state"] == PLAYING, started

        # All games accept the default three physical keys. Piano plays a note;
        # Breakout launches its ball and Flappy flaps with another Enter press.
        device.key("up")
        if name in ("Piano", "Breakout", "Flappy"):
            device.key("enter")
        if name == "Piano":
            played = device.status()
            assert played["piano_sequence"] > started["piano_sequence"], played
        time.sleep(0.1)
        device.key("enter", "long")
        paused = device.status()
        assert paused["game_state"] == PAUSED, paused
        time.sleep(0.3)
        still = device.status()
        assert still["game_state"] == PAUSED, still
        for key in ("game_steps", "game_score", "game_lives", "piano_sequence"):
            assert still[key] == paused[key], (key, paused, still)
        healthy(still, baseline)

        device.key("enter")
        resumed = device.status()
        assert resumed["game_state"] == PLAYING, resumed
        device.key("back")
        state = device.status()
        assert state["ui"] == GAME_MENU and state["game"] == 255, state
        reopened = open_game(device, index)
        assert reopened["game_score"] == ready["game_score"], reopened
        assert reopened["game_steps"] == ready["game_steps"], reopened
        assert reopened["game_lives"] == ready["game_lives"], reopened
        device.key("back")
        result["games"].append({"name": name, "ready": ready, "paused": paused,
                                "resumed": resumed, "reopened": reopened})
        passed(name + " start, input, pause, resume, exit and restart")

    device.home()
    before = device.status()
    for _ in range(40):
        device.key("down")
    after = device.status()
    assert after["events"] == before["events"] + 40, (before, after)
    assert after["frames"] > before["frames"], (before, after)
    healthy(after, baseline)
    passed("40 menu inputs during rendering without dropped events or SPI errors")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--output", type=Path, default=Path("build/hardware"))
    args = parser.parse_args()
    result = {"ok": False, "checks": [], "games": []}
    device = None

    def passed(name):
        result["checks"].append(name)
        print("PASS " + name, flush=True)

    try:
        device = Device(args.port, args.output / "game-smoke.log")
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
        passed("Returned to Games home card; settings and Wi-Fi preserved")
        result["ok"] = True
    except Exception as error:
        result["error"] = str(error)
        raise
    finally:
        if device:
            try:
                result["final"] = games_home(device)
            except Exception as error:
                result["cleanup_error"] = str(error)
                result["ok"] = False
            finally:
                device.close()
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / "game-smoke.json").write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    assert result["ok"], result


if __name__ == "__main__":
    main()
